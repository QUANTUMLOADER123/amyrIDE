#include "ai_client.hpp"

#include <algorithm>

#include "ai/prompts.hpp"
#include "core/string.hpp"
#include "core/tokens.hpp"
#include "platform/http.hpp"

namespace
{
    constexpr int max_tool_rounds = 25;
    constexpr int compact_transcript_char_limit = 90000;
    constexpr int max_tokens_per_completion = 16384;

    std::string trim_copy(std::string_view text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && static_cast<unsigned char>(text[begin]) <= ' ')
            ++begin;
        while (end > begin && static_cast<unsigned char>(text[end - 1]) <= ' ')
            --end;
        return std::string(text.substr(begin, end - begin));
    }

    bool extract_error_message(const std::string& body, std::string& out)
    {
        json_t data;
        if (json_t::parse(body, data))
        {
            const json_t* error = data.find("error");
            if (error)
            {
                if (const json_t* message = error->find("message");
                    message && message->is_string())
                {
                    out = message->as_string();
                    return true;
                }
                if (error->is_string())
                {
                    out = error->as_string();
                    return true;
                }
            }
        }
        out = trim_copy(body);
        return !out.empty();
    }

    bool error_mentions_tools(const std::string& text)
    {
        std::string lowered = str::to_lower(text);
        return lowered.find("tool") != std::string::npos || lowered.find("function") != std::string::npos;
    }
}

c_ai_client::c_ai_client(c_config& config_ref, c_tool_registry& registry_ref)
    : config(config_ref)
    , registry(registry_ref)
{
}

message_t* c_ai_client::push_message(message_t message)
{
    int estimate = tokens::estimate(message.content);
    for (const tool_call_t& call : message.tool_calls)
        estimate += tokens::estimate(call.arguments) + 16;
    message.token_estimate = std::max(estimate, 4);
    history.push_back(std::move(message));
    token_total += history.back().token_estimate;
    return &history.back();
}

void c_ai_client::recompute_tokens()
{
    token_total = 0;
    for (const message_t& message : history)
        token_total += message.token_estimate;
    token_total += tokens::estimate(conversation_summary);
}

bool c_ai_client::needs_compaction() const
{
    return token_total > config.context_limit - config.response_reserve;
}

void c_ai_client::reset_error()
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (state == ai_state::error)
    {
        state = ai_state::idle;
        error_message.clear();
    }
}

void c_ai_client::truncate_from(int message_index)
{
    std::lock_guard<std::mutex> guard(history_mutex);
    if (message_index < 0 || message_index >= static_cast<int>(history.size()))
        return;
    history.erase(history.begin() + static_cast<long>(message_index), history.end());
    recompute_tokens();
    session_dirty = true;
    error_message.clear();
    state = ai_state::idle;
}

void c_ai_client::clear_history()
{
    if (busy())
        cancel();
    std::lock_guard<std::mutex> guard(history_mutex);
    history.clear();
    conversation_summary.clear();
    context_generation++;
    token_total = 0;
    session_dirty = true;
}

void c_ai_client::cancel()
{
    cancel_flag = true;
}

void c_ai_client::shutdown()
{
    cancel_flag = true;
    if (worker_thread.joinable())
        worker_thread.detach();
}

std::vector<message_t> c_ai_client::history_snapshot() const
{
    std::lock_guard<std::mutex> guard(history_mutex);
    return history;
}

active_tool_t c_ai_client::active_tool_snapshot() const
{
    std::lock_guard<std::mutex> guard(state_mutex);
    return active_tool;
}

void c_ai_client::send_user_message(const std::string& text, const std::string& context_block)
{
    if (busy())
        return;
    cancel_flag = false;

    std::string full_content = text;
    if (!context_block.empty())
        full_content += "\n\n" + context_block;

    {
        std::lock_guard<std::mutex> guard(history_mutex);
        message_t message;
        message.role = "user";
        message.content = full_content;
        push_message(std::move(message));
        session_dirty = true;
    }

    if (worker_thread.joinable())
        worker_thread.join();
    worker_thread = std::thread([this]() { worker_loop(); });
}

void c_ai_client::request_compaction()
{
    if (busy())
        return;
    cancel_flag = false;
    if (worker_thread.joinable())
        worker_thread.join();
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::compacting;
        hint_text = "compacting context";
    }
    worker_thread = std::thread([this]() {
        compact_history();
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::idle;
        hint_text.clear();
    });
}

std::string c_ai_client::build_payload(bool with_tools) const
{
    const c_workspace* space = nullptr;
    if (registry.hooks.workspace)
        space = registry.hooks.workspace();

    json_t messages = json_t::array();
    messages.push(json_t::object_t{
        { "role", "system" },
        { "content", build_system_prompt(space, "", "") }
    });

    std::lock_guard<std::mutex> guard(history_mutex);
    if (!conversation_summary.empty())
    {
        messages.push(json_t::object_t{
            { "role", "system" },
            { "content", "Summary of earlier conversation:\n" + conversation_summary }
        });
    }
    for (const message_t& message : history)
    {
        if (message.role == "assistant" && message.content.empty() && message.tool_calls.empty())
            continue;
        json_t entry = json_t::object_t{
            { "role", message.role },
            { "content", message.content }
        };
        if (!message.tool_calls.empty())
        {
            json_t calls = json_t::array();
            for (const tool_call_t& call : message.tool_calls)
            {
                calls.push(json_t::object_t{
                    { "id", call.id },
                    { "type", "function" },
                    { "function", json_t::object_t{
                        { "name", call.name },
                        { "arguments", call.arguments.empty() ? std::string("{}") : call.arguments }
                    } }
                });
            }
            entry["tool_calls"] = std::move(calls);
            if (message.content.empty())
                entry["content"] = json_t(nullptr);
        }
        if (message.role == "tool")
        {
            entry["tool_call_id"] = message.tool_call_id;
            entry["name"] = message.tool_name;
        }
        messages.push(std::move(entry));
    }

    json_t body = json_t::object_t{
        { "model", config.model },
        { "messages", std::move(messages) },
        { "stream", true },
        { "max_tokens", max_tokens_per_completion }
    };
    if (with_tools)
    {
        body["tools"] = registry.build_tools_schema();
        body["tool_choice"] = "auto";
    }
    return body.dump();
}

c_ai_client::stream_outcome c_ai_client::stream_request_round(std::vector<tool_call_t>& out_calls, std::string& out_error, bool with_tools)
{
    out_calls.clear();
    out_error.clear();

    size_t assistant_index = 0;
    {
        std::lock_guard<std::mutex> guard(history_mutex);
        assistant_index = history.size();
        message_t placeholder;
        placeholder.role = "assistant";
        push_message(std::move(placeholder));
    }

    {
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::streaming;
        hint_text = "thinking";
    }

    std::string body = build_payload(with_tools);
    c_http_client client;
    client.set_insecure_skip_verify(config.skip_tls_verify);

    struct assembly_t
    {
        std::string buffer;
        bool plain_json = false;
        bool mode_detected = false;
        bool done = false;
        std::string content;
        std::vector<tool_call_t> tool_calls;
        std::string finish_reason;
        c_ai_client* self = nullptr;
        size_t assistant_index = 0;
    } assembly;
    assembly.self = this;
    assembly.assistant_index = assistant_index;

    auto apply_delta = [&assembly](const json_t& delta)
    {
        if (const json_t* piece = delta.find("content"); piece && piece->is_string())
        {
            assembly.content += piece->as_string();
            std::lock_guard<std::mutex> guard(assembly.self->history_mutex);
            if (assembly.assistant_index < assembly.self->history.size())
                assembly.self->history[assembly.assistant_index].content = assembly.content;
        }
        if (const json_t* calls = delta.find("tool_calls"); calls && calls->is_array())
        {
            for (size_t i = 0; i < calls->size(); ++i)
            {
                const json_t& entry = calls->at(i);
                size_t index = static_cast<size_t>(std::max(0, entry["index"].as_int(static_cast<int>(assembly.tool_calls.size()))));
                while (assembly.tool_calls.size() <= index)
                    assembly.tool_calls.emplace_back();
                tool_call_t& call = assembly.tool_calls[index];
                if (const json_t* id = entry.find("id"); id && id->is_string() && !id->as_string().empty())
                    call.id = id->as_string();
                if (const json_t* function = entry.find("function"))
                {
                    if (const json_t* name = function->find("name"); name && name->is_string() && !name->as_string().empty())
                        call.name = name->as_string();
                    if (const json_t* arguments = function->find("arguments"); arguments && arguments->is_string())
                        call.arguments += arguments->as_string();
                }
            }
        }
    };

    auto handle_sse_line = [&assembly, &apply_delta](std::string_view line_view) -> bool
    {
        std::string line = trim_copy(line_view);
        if (line.empty() || !line.starts_with("data:"))
            return true;
        std::string payload = trim_copy(std::string_view(line).substr(5));
        if (payload == "[DONE]")
        {
            assembly.done = true;
            return true;
        }
        json_t chunk;
        if (!json_t::parse(payload, chunk))
            return true;
        if (const json_t* error = chunk.find("error"))
        {
            std::string message = payload;
            if (error->is_string())
                message = error->as_string();
            else if (const json_t* nested = error->find("message"); nested && nested->is_string())
                message = nested->as_string();
            assembly.content += "\n[stream error] " + message;
            return true;
        }
        const json_t* choices = chunk.find("choices");
        if (!choices || !choices->is_array() || choices->size() == 0)
            return true;
        const json_t& first = choices->at(0);
        if (const json_t* delta = first.find("delta"))
            apply_delta(*delta);
        if (const json_t* message_field = first.find("message"))
            apply_delta(*message_field);
        if (const json_t* reason = first.find("finish_reason"); reason && reason->is_string())
            assembly.finish_reason = reason->as_string();
        return true;
    };

    http_result_t result;
    bool transport_ok = client.post_streamed(config.completion_url(), {
        { "Authorization", "Bearer " + config.api_key },
        { "Content-Type", "application/json" },
        { "Accept", "text/event-stream" }
    }, body, [&](const char* data, unsigned int size) -> bool
    {
        assembly.buffer.append(data, size);
        if (!assembly.mode_detected)
        {
            std::string trimmed = trim_copy(assembly.buffer);
            if (trimmed.empty())
                return !cancel_flag;
            assembly.plain_json = trimmed[0] == '{';
            assembly.mode_detected = true;
        }
        if (assembly.plain_json)
            return !cancel_flag;

        size_t newline;
        while ((newline = assembly.buffer.find('\n')) != std::string::npos)
        {
            std::string_view line(assembly.buffer.data(), newline);
            if (!handle_sse_line(line))
            {
                assembly.buffer.clear();
                return false;
            }
            assembly.buffer.erase(0, newline + 1);
        }
        return !cancel_flag;
    }, result);

    if (!cancel_flag && transport_ok && assembly.plain_json)
    {
        json_t whole;
        if (json_t::parse(assembly.buffer, whole))
        {
            const json_t* choices = whole.find("choices");
            if (choices && choices->is_array() && choices->size() > 0)
            {
                if (const json_t* message_field = choices->at(0).find("message"))
                {
                    if (const json_t* piece = message_field->find("content"); piece && piece->is_string())
                        assembly.content += piece->as_string();
                    if (const json_t* calls = message_field->find("tool_calls"); calls && calls->is_array())
                    {
                        for (size_t i = 0; i < calls->size(); ++i)
                        {
                            const json_t& entry = calls->at(i);
                            tool_call_t call;
                            call.id = entry["id"].as_string("call_" + std::to_string(i));
                            if (const json_t* function = entry.find("function"))
                            {
                                call.name = function->operator[]("name").as_string("");
                                call.arguments = function->operator[]("arguments").as_string("");
                            }
                            if (!call.name.empty())
                                assembly.tool_calls.push_back(std::move(call));
                        }
                    }
                }
            }
        }
    }

    if (!transport_ok && !cancel_flag)
    {
        std::lock_guard<std::mutex> guard(history_mutex);
        if (assistant_index < history.size() && history[assistant_index].content.empty())
            history.erase(history.begin() + static_cast<long>(assistant_index));
        std::string detail = result.error_text.empty() ? "connection failed" : result.error_text;
        if (result.status_code > 0)
            out_error = "HTTP " + std::to_string(result.status_code) + ": " + detail;
        else
            out_error = detail;
        if (result.status_code == 401 || result.status_code == 403)
            out_error += " (check the API key in settings)";
        return stream_outcome::failed;
    }

    {
        std::lock_guard<std::mutex> guard(history_mutex);
        if (assistant_index < history.size())
        {
            message_t& assistant = history[assistant_index];
            assistant.content = assembly.content;
            assistant.tool_calls = assembly.tool_calls;
            int estimate = tokens::estimate(assistant.content);
            for (const tool_call_t& call : assistant.tool_calls)
                estimate += tokens::estimate(call.arguments) + 16;
            assistant.token_estimate = std::max(estimate, 4);
            token_total += assistant.token_estimate;
            if (cancel_flag)
                assistant.content += "\n\n*[interrupted]*";
        }
    }

    if (cancel_flag)
        return stream_outcome::cancelled;

    if (assembly.tool_calls.empty() && assembly.content.empty())
    {
        out_error = "empty response from " + config.model;
        return stream_outcome::failed;
    }

    out_calls = std::move(assembly.tool_calls);
    return stream_outcome::completed;
}

void c_ai_client::append_tool_outputs(const std::vector<tool_call_t>& calls)
{
    for (const tool_call_t& call : calls)
    {
        if (cancel_flag)
        {
            std::lock_guard<std::mutex> guard(history_mutex);
            message_t message;
            message.role = "tool";
            message.tool_call_id = call.id;
            message.tool_name = call.name;
            message.content = "[interrupted by user]";
            push_message(std::move(message));
            continue;
        }

        {
            std::lock_guard<std::mutex> guard(state_mutex);
            state = ai_state::executing;
            hint_text = call.name;
            active_tool.running = true;
            active_tool.id = call.id;
            active_tool.name = call.name;
            active_tool.arguments = call.arguments;
        }

        tool_result_t result = registry.execute(call.name, call.arguments);

        {
            std::lock_guard<std::mutex> guard(history_mutex);
            message_t message;
            message.role = "tool";
            message.tool_call_id = call.id;
            message.tool_name = call.name;
            message.content = result.content;
            push_message(std::move(message));
        }

        std::lock_guard<std::mutex> guard(state_mutex);
        active_tool = active_tool_t{};
    }
}

void c_ai_client::worker_loop()
{
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::preparing;
        hint_text = "preparing context";
    }

    if (config.auto_compact && needs_compaction())
        compact_history();

    bool with_tools = true;

    for (int round = 0; round < max_tool_rounds && !cancel_flag; ++round)
    {
        std::vector<tool_call_t> calls;
        std::string error;
        stream_outcome outcome = stream_request_round(calls, error, with_tools);

        if (outcome == stream_outcome::cancelled)
            break;

        if (outcome == stream_outcome::failed)
        {
            if (with_tools && error_mentions_tools(error) && (error.find("400") != std::string::npos || error.find("422") != std::string::npos))
            {
                with_tools = false;
                continue;
            }
            std::lock_guard<std::mutex> guard(state_mutex);
            state = ai_state::error;
            error_message = error;
            hint_text.clear();
            return;
        }

        if (calls.empty())
            break;

        append_tool_outputs(calls);
    }

    {
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::idle;
        hint_text.clear();
    }
    session_dirty = true;
}

bool c_ai_client::compact_history()
{
    size_t keep = static_cast<size_t>(std::max(2, config.keep_recent_messages));
    size_t split = 0;
    {
        std::lock_guard<std::mutex> guard(history_mutex);
        if (history.size() <= keep)
            return true;
        split = history.size() - keep;
        while (split < history.size() && history[split].role == "tool")
            ++split;
        if (split >= history.size())
            return true;
    }

    {
        std::lock_guard<std::mutex> guard(state_mutex);
        state = ai_state::compacting;
        hint_text = "compacting context";
    }

    std::string transcript;
    {
        std::lock_guard<std::mutex> guard(history_mutex);
        for (size_t i = 0; i < split; ++i)
            transcript += history[i].role + ": " + str::truncate_middle(history[i].content, 2500) + "\n";
    }
    transcript = str::truncate_middle(transcript, compact_transcript_char_limit);

    json_t messages = json_t::array();
    messages.push(json_t::object_t{
        { "role", "system" },
        { "content", "You compress engineering conversations into dense handoff notes. Preserve goals, decisions, changed file paths and next steps." }
    });
    messages.push(json_t::object_t{
        { "role", "user" },
        { "content", build_compaction_prompt(transcript) }
    });

    json_t body = json_t::object_t{
        { "model", config.model },
        { "messages", std::move(messages) },
        { "stream", true },
        { "max_tokens", 2048 }
    };

    std::string summary;
    std::string sse_buffer;
    http_result_t compaction_result;
    c_http_client client;
    client.set_insecure_skip_verify(config.skip_tls_verify);

    bool ok = client.post_streamed(config.completion_url(), {
        { "Authorization", "Bearer " + config.api_key },
        { "Content-Type", "application/json" }
    }, body.dump(), [&](const char* data, unsigned int size) -> bool
    {
        sse_buffer.append(data, size);
        size_t newline;
        while ((newline = sse_buffer.find('\n')) != std::string::npos)
        {
            std::string line = str::trim(sse_buffer.substr(0, newline));
            sse_buffer.erase(0, newline + 1);
            if (!line.starts_with("data:"))
                continue;
            std::string payload = str::trim(std::string_view(line).substr(5));
            if (payload == "[DONE]")
                return true;
            json_t chunk;
            if (!json_t::parse(payload, chunk))
                continue;
            const json_t* choices = chunk.find("choices");
            if (!choices || choices->size() == 0)
                continue;
            if (const json_t* delta = choices->at(0).find("delta"))
            {
                if (const json_t* piece = delta->find("content"); piece && piece->is_string())
                    summary += piece->as_string();
            }
        }
        return !cancel_flag;
    }, compaction_result);

    {
        std::lock_guard<std::mutex> guard(history_mutex);
        history.erase(history.begin(), history.begin() + static_cast<long>(split));
        message_t note;
        note.role = "user";
        note.internal_note = true;
        if (ok && !summary.empty())
        {
            if (!conversation_summary.empty())
                conversation_summary += "\n\n---\n\n";
            conversation_summary += summary;
            note.content = "[amyrIDE] Earlier conversation was compacted into a summary to stay inside the context budget.";
        }
        else
        {
            note.content = "[amyrIDE] Earlier conversation was trimmed to stay inside the context budget.";
        }
        push_message(std::move(note));
        context_generation++;
        recompute_tokens();
        session_dirty = true;
    }
    return true;
}

std::string c_ai_client::serialize_session() const
{
    std::lock_guard<std::mutex> guard(history_mutex);
    json_t messages = json_t::array();
    for (const message_t& message : history)
    {
        json_t entry = json_t::object_t{
            { "role", message.role },
            { "content", message.content },
            { "internal_note", message.internal_note }
        };
        if (!message.tool_calls.empty())
        {
            json_t calls = json_t::array();
            for (const tool_call_t& call : message.tool_calls)
            {
                calls.push(json_t::object_t{
                    { "id", call.id },
                    { "name", call.name },
                    { "arguments", call.arguments }
                });
            }
            entry["tool_calls"] = std::move(calls);
        }
        if (!message.tool_call_id.empty())
        {
            entry["tool_call_id"] = message.tool_call_id;
            entry["tool_name"] = message.tool_name;
        }
        messages.push(std::move(entry));
    }
    return json_t(json_t::object_t{
        { "model", config.model },
        { "summary", conversation_summary },
        { "messages", std::move(messages) }
    }).dump(true);
}

void c_ai_client::restore_session(const std::string& json_text)
{
    json_t data;
    if (!json_t::parse(json_text, data))
        return;
    std::lock_guard<std::mutex> guard(history_mutex);
    history.clear();
    conversation_summary = data["summary"].as_string("");
    const json_t* messages = data.find("messages");
    if (!messages)
        return;
    for (size_t i = 0; i < messages->size(); ++i)
    {
        const json_t& entry = messages->at(i);
        message_t message;
        message.role = entry["role"].as_string("user");
        message.content = entry["content"].as_string("");
        message.internal_note = entry["internal_note"].as_bool(false);
        message.tool_call_id = entry["tool_call_id"].as_string("");
        message.tool_name = entry["tool_name"].as_string("");
        if (const json_t* calls = entry.find("tool_calls"); calls && calls->is_array())
        {
            for (size_t c = 0; c < calls->size(); ++c)
            {
                const json_t& call_entry = calls->at(c);
                tool_call_t call;
                call.id = call_entry["id"].as_string("");
                call.name = call_entry["name"].as_string("");
                call.arguments = call_entry["arguments"].as_string("");
                message.tool_calls.push_back(std::move(call));
            }
        }
        push_message(std::move(message));
    }
}
