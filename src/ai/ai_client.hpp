#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/tools.hpp"
#include "core/config.hpp"

enum class ai_state
{
    idle,
    preparing,
    streaming,
    executing,
    compacting,
    error
};

struct tool_call_t
{
    std::string id;
    std::string name;
    std::string arguments;
};

struct message_t
{
    std::string role;
    std::string content;
    std::vector<tool_call_t> tool_calls;
    std::string tool_call_id;
    std::string tool_name;
    bool internal_note = false;
    int token_estimate = 0;
};

struct active_tool_t
{
    bool running = false;
    std::string id;
    std::string name;
    std::string arguments;
};

class c_tool_registry;

class c_ai_client
{
public:
    c_ai_client(c_config& config, c_tool_registry& registry);

    void send_user_message(const std::string& text, const std::string& context_block);
    void request_compaction();
    void cancel();
    void clear_history();
    void reset_error();
    void shutdown();

    bool busy() const { return state != ai_state::idle && state != ai_state::error; }
    ai_state current_state() const { return state; }
    const std::string& error_text() const { return error_message; }
    const std::string& stream_hint() const { return hint_text; }

    std::vector<message_t> history_snapshot() const;
    active_tool_t active_tool_snapshot() const;

    int total_tokens() const { return token_total; }
    bool needs_compaction() const;
    int context_generation_value() const { return context_generation; }

    std::string serialize_session() const;
    void restore_session(const std::string& json_text);
    bool session_dirty = false;

private:
    enum class stream_outcome { completed, cancelled, failed };

    void worker_loop();
    stream_outcome stream_request_round(std::vector<tool_call_t>& out_calls, std::string& out_error, bool with_tools);
    void append_tool_outputs(const std::vector<tool_call_t>& calls);
    bool compact_history();
    std::string build_payload(bool with_tools) const;
    message_t* push_message(message_t message);
    void recompute_tokens();

    c_config& config;
    c_tool_registry& registry;
    std::vector<message_t> history;
    std::string conversation_summary;
    mutable std::mutex history_mutex;

    ai_state state = ai_state::idle;
    std::string error_message;
    std::string hint_text;
    active_tool_t active_tool;
    mutable std::mutex state_mutex;

    std::thread worker_thread;
    std::atomic<bool> cancel_flag{ false };
    int context_generation = 0;
    int token_total = 0;
};
