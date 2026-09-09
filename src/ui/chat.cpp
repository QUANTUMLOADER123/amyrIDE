#include "chat.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "app.hpp"
#include "core/string.hpp"
#include "core/tokens.hpp"
#include "platform/shell.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
    constexpr int composer_rows = 5;
    constexpr float reveal_bytes_per_second = 3200.0f;
    constexpr float reveal_catchup_bytes = 2400.0f;
    constexpr float card_appear_seconds = 0.30f;

    const char* tool_icon(const std::string& name)
    {
        if (name == "list_files" || name == "list_dir")
            return icon_folder_open;
        if (name == "workspace_info")
            return icon_database;
        if (name == "read_file")
            return icon_eye;
        if (name == "file_info")
            return icon_circle_info;
        if (name == "write_file" || name == "append_file")
            return icon_square_plus;
        if (name == "edit_file" || name == "replace_lines" || name == "insert_lines" || name == "delete_lines")
            return icon_pen_to_square;
        if (name == "create_directory")
            return icon_folder_plus;
        if (name == "copy_path")
            return icon_copy;
        if (name == "delete_path")
            return icon_trash_can;
        if (name == "move_path")
            return icon_scissors;
        if (name == "search_files" || name == "search_text")
            return icon_magnifying_glass;
        if (name == "set_plan")
            return icon_list_check;
        if (name == "run_command" || name == "run_powershell")
            return icon_terminal;
        return icon_bolt;
    }

    const char* tool_label(const std::string& name, bool running)
    {
        if (name == "list_files")
            return running ? "изучает файлы" : "изучил файлы";
        if (name == "list_dir")
            return running ? "смотрит папку" : "посмотрел папку";
        if (name == "workspace_info")
            return running ? "смотрит структуру проекта" : "изучил проект";
        if (name == "read_file")
            return running ? "читает файл" : "прочитал файл";
        if (name == "file_info")
            return running ? "смотрит файл" : "посмотрел файл";
        if (name == "write_file")
            return running ? "создаёт файл" : "создал файл";
        if (name == "append_file")
            return running ? "дописывает файл" : "дописал файл";
        if (name == "edit_file")
            return running ? "правит файл" : "изменил файл";
        if (name == "replace_lines")
            return running ? "переписывает строки" : "переписал строки";
        if (name == "insert_lines")
            return running ? "вставляет строки" : "вставил строки";
        if (name == "delete_lines")
            return running ? "удаляет строки" : "удалил строки";
        if (name == "create_directory")
            return running ? "создаёт папку" : "создал папку";
        if (name == "copy_path")
            return running ? "копирует" : "скопировал";
        if (name == "delete_path")
            return running ? "удаляет" : "удалил";
        if (name == "move_path")
            return running ? "перемещает" : "переместил";
        if (name == "search_files")
            return running ? "ищет файлы" : "поиск файлов";
        if (name == "search_text")
            return running ? "ищет в коде" : "поиск в коде";
        if (name == "set_plan")
            return running ? "строит план" : "обновил план";
        if (name == "run_command")
            return running ? "выполняет команду" : "команда выполнена";
        if (name == "run_powershell")
            return running ? "запускает powershell" : "powershell выполнен";
        return running ? "работает" : "готово";
    }

    std::string tool_subtitle(const std::string& name, const std::string& arguments_json)
    {
        json_t args;
        if (!json_t::parse(arguments_json.empty() ? "{}" : arguments_json, args) || !args.is_object())
            return "";
        std::string path = args["path"].as_string("");
        std::string from = args["from"].as_string("");
        std::string pattern = args["pattern"].as_string("");
        std::string query = args["query"].as_string("");
        std::string command = args["command"].as_string("");
        std::string script = args["script"].as_string("");
        std::string steps = args["steps"].as_string("");

        std::string value;
        if (name == "run_command")
            value = command;
        else if (name == "run_powershell")
        {
            std::string first_line = str::trim(str::split(script, '\n').empty() ? script : str::split(script, '\n').front());
            value = first_line;
        }
        else if (name == "search_files")
            value = pattern;
        else if (name == "search_text")
            value = query;
        else if (name == "move_path" || name == "copy_path")
            value = from + " → " + args["to"].as_string("");
        else if (name == "set_plan")
            return "";
        else if (!path.empty())
            value = path;

        value = str::trim(value);
        size_t cut = value.find('\n');
        if (cut != std::string::npos)
            value = value.substr(0, cut);
        return str::truncate_middle(value, 52);
    }

    struct card_status_t
    {
        bool ok = true;
        bool has_output = false;
        int added = 0;
        int removed = 0;
        bool has_stats = false;
        std::string details;
    };

    card_status_t parse_tool_output(const std::string* output)
    {
        card_status_t status;
        if (!output)
            return status;
        status.has_output = true;
        std::string first_line = output->substr(0, output->find('\n'));
        status.details = output->size() > first_line.size() + 1 ? output->substr(first_line.size() + 1) : "";
        if (first_line.starts_with("ERR"))
        {
            status.ok = false;
            return status;
        }
        size_t scan = first_line.starts_with("OK") ? 2 : 0;
        while (scan < first_line.size() && first_line[scan] == ' ')
            ++scan;
        if (scan < first_line.size() && first_line[scan] == '+')
        {
            size_t end = first_line.find(' ', scan);
            status.added = std::atoi(first_line.substr(scan + 1, end == std::string::npos ? end : end - scan - 1).c_str());
            status.has_stats = true;
            scan = end == std::string::npos ? first_line.size() : end;
            while (scan < first_line.size() && first_line[scan] == ' ')
                ++scan;
            if (scan < first_line.size() && first_line[scan] == '-')
            {
                size_t remove_end = first_line.find(' ', scan);
                status.removed = std::atoi(first_line.substr(scan + 1, remove_end == std::string::npos ? remove_end : remove_end - scan - 1).c_str());
                scan = remove_end == std::string::npos ? first_line.size() : remove_end;
            }
        }
        return status;
    }

    size_t utf8_boundary(const std::string& text, size_t position)
    {
        if (position >= text.size())
            return text.size();
        size_t cut = position;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;
        return cut;
    }

    std::string localize_hint(const std::string& hint)
    {
        if (hint.empty() || hint == "thinking")
            return "думает";
        if (hint == "preparing context")
            return "готовлю контекст";
        if (hint == "compacting context")
            return "сжимаю контекст";
        return hint;
    }

    std::string reveal_content(const std::string& content, message_view_t& view, float now, float dt)
    {
        float target = static_cast<float>(content.size());
        if (view.reveal_bytes < target - reveal_catchup_bytes)
            view.reveal_bytes = target - reveal_catchup_bytes;
        view.reveal_bytes = std::min(target, view.reveal_bytes + reveal_bytes_per_second * dt);
        if (view.reveal_bytes >= target)
            return content;
        return content.substr(0, utf8_boundary(content, static_cast<size_t>(view.reveal_bytes)));
    }
}

void c_chat_panel::attach_context_file(const std::string& path)
{
    for (const std::string& existing : attached_files)
    {
        if (existing == path)
            return;
    }
    attached_files.push_back(path);
}

void c_chat_panel::draft_message(const std::string& text)
{
    input = text;
    input_focus_requested = true;
}

std::string c_chat_panel::build_context_block(c_ide_app& app) const
{
    std::string block;
    for (const std::string& path : attached_files)
    {
        std::string content = app.read_file_for_context(path);
        if (content.empty())
            continue;
        block += "[file: " + path + "]\n" + content + "\n\n";
    }
    return block;
}

void c_chat_panel::render(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();
    std::vector<plan_step_t> plan = app.tools.plan_snapshot();
    float line_height = ImGui::GetTextLineHeight();
    float composer_height = line_height * static_cast<float>(composer_rows) + 24.0f * unit;
    float hint_height = line_height + 8.0f * unit;
    float plan_height = 0.0f;
    if (!plan.empty())
    {
        plan_height += line_height + 20.0f * unit + 8.0f * unit;
        if (plan_open)
            plan_height += line_height * static_cast<float>(plan.size()) + 24.0f * unit + 4.0f * unit;
    }
    below_height = composer_height + hint_height + plan_height;

    render_header(app);
    render_history(app);
    render_plan_strip(app);
    render_composer(app);
}

void c_chat_panel::render_header(c_ide_app& app)
{
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float row_height = ImGui::GetTextLineHeight() + 6.0f * unit;

    ImVec2 logo_center(ImGui::GetCursorScreenPos().x + 12.0f * unit, ImGui::GetCursorScreenPos().y + row_height * 0.5f);
    float pulse = 0.5f + 0.5f * std::sin(anim::time_now() * 1.8f);
    glow_circle(ImGui::GetWindowDrawList(), logo_center, 7.0f * unit, colors.accent);
    ImGui::GetWindowDrawList()->AddCircleFilled(logo_center, 7.0f * unit + pulse * 1.2f * unit, theme.accent_u32(0.14f), 24);
    ImGui::GetWindowDrawList()->AddCircleFilled(logo_center, 7.0f * unit, theme.accent_u32(0.85f), 24);

    ImGui::SetCursorScreenPos(ImVec2(logo_center.x + 16.0f * unit, logo_center.y - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase);
    ImGui::TextColored(colors.text, "%s", "amyr");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(theme.accent(), "%s", "IDE");
    ImGui::PopFont();

    if (app.workspace.valid)
    {
        ImGui::SameLine(0.0f, 10.0f * unit);
        ImGui::TextColored(colors.text_faint, "%s", icon_folder_open);
        ImGui::SameLine(0.0f, 5.0f * unit);
        ImGui::TextColored(colors.text_dim, "%s", app.workspace.display_name().c_str());
    }

    ImGui::SameLine(ImGui::GetContentRegionMax().x - (4.0f * 33.0f + 152.0f) * unit);
    ImGui::SetNextItemWidth(146.0f * unit);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colors.background);
    if (ImGui::BeginCombo("##model_pick", app.config.model.c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int i = 0; i < model_list_size; ++i)
        {
            bool selected = app.config.model == model_list[i];
            if (ImGui::Selectable(model_list[i], selected))
            {
                app.config.model = model_list[i];
                app.config.save();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 6.0f * unit);
    if (icon_button(theme, icon_xmark, "##chat_clear", "очистить диалог"))
        app.clear_conversation();
    ImGui::SameLine(0.0f, 2.0f * unit);
    if (icon_button(theme, icon_download, "##chat_export", "экспорт диалога"))
        app.export_conversation();
    ImGui::SameLine(0.0f, 2.0f * unit);
    if (icon_button(theme, icon_folder_open, "##chat_project", "сменить проект"))
        app.open_workspace_dialog();
    ImGui::SameLine(0.0f, 2.0f * unit);
    if (icon_button(theme, icon_gear, "##chat_settings", "настройки (ctrl+,)"))
    {
        app.settings.visible = !app.settings.visible;
    }

    ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
    float meter_width = ImGui::GetContentRegionAvail().x - 150.0f * unit;
    token_meter(theme, app.ai.total_tokens(), app.config.context_limit, meter_width);
    ImGui::SameLine(0.0f, 8.0f * unit);
    ImGui::TextColored(colors.text_faint, "контекст");
    if (app.ai.total_tokens() > app.config.context_limit - app.config.response_reserve)
    {
        ImGui::SameLine(0.0f, 6.0f * unit);
        if (ghost_button(theme, "сжать", ImVec2(0.0f, ImGui::GetTextLineHeight() + 6.0f * unit)))
            app.ai.request_compaction();
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
    ImVec2 line_min = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(line_min, ImVec2(line_min.x + ImGui::GetContentRegionAvail().x, line_min.y + 1.0f), ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.border, 0.55f)));
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));
}

void c_chat_panel::render_history(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();
    std::vector<message_t> history = app.ai.history_snapshot();
    active_tool_t running_tool = app.ai.active_tool_snapshot();

    float available = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##history", ImVec2(0.0f, available - below_height), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (history.empty())
    {
        const char* suggestions[4] = {
            "расскажи, что это за проект",
            "найди потенциальные баги и исправь",
            "добавь тесты для ключевого модуля",
            "отрефактори самый большой файл"
        };
        float chip_width = ImGui::GetContentRegionAvail().x * 0.5f - 6.0f * unit;
        for (int i = 0; i < 4; ++i)
        {
            ImGui::PushID(str::format("suggest_%d", i).c_str());
            if (chip(theme, str::format("%s  %s", icon_wand_magic_sparkles, suggestions[i]).c_str(), false))
            {
                draft_message(suggestions[i]);
                input_focus_requested = true;
            }
            if (i % 2 == 0 && i + 1 < 4)
                ImGui::SameLine(0.0f, 12.0f * unit);
            ImGui::PopID();
        }
        ImGui::EndChild();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * unit, 4.0f * unit));
    float wrap_width = ImGui::GetContentRegionAvail().x - 22.0f * unit;
    for (size_t i = 0; i < history.size(); ++i)
    {
        const message_t& message = history[i];
        render_message(app, message, static_cast<int>(i), wrap_width);

        if (message.role == "assistant" && !message.tool_calls.empty())
        {
            for (size_t c = 0; c < message.tool_calls.size(); ++c)
            {
                const tool_call_t& call = message.tool_calls[c];
                const message_t* output = nullptr;
                for (size_t j = i + 1; j < history.size(); ++j)
                {
                    if (history[j].role == "tool" && history[j].tool_call_id == call.id)
                    {
                        output = &history[j];
                        break;
                    }
                }
                std::string output_text = output ? output->content : "";
                if (output_text.starts_with("ERROR: "))
                    output_text = "ERR " + output_text.substr(7);
                render_tool_card(app, call, static_cast<int>(i), static_cast<int>(c), false, output ? &output_text : nullptr, wrap_width);
            }
        }
    }

    if (app.ai.busy())
    {
        if (running_tool.running)
            render_tool_card(app, tool_call_t{ running_tool.id, running_tool.name, running_tool.arguments }, static_cast<int>(history.size()), 0, true, nullptr, wrap_width);
        else
        {
            ImGui::Indent(4.0f * unit);
            spinner(theme, 6.5f * unit, 2.0f * unit);
            ImGui::SameLine(0.0f, 10.0f * unit);
            std::string label = localize_hint(app.ai.stream_hint());
            float dots = std::fmod(anim::time_now() * 2.2f, 3.0f);
            ImGui::TextColored(theme.palette().text_faint, "%s%s%s%s", label.c_str(), dots > 1.0f ? "." : "", dots > 2.0f ? "." : "", dots > 2.5f ? "." : "");
            ImGui::Unindent(4.0f * unit);
        }
    }
    ImGui::PopStyleVar();

    bool stick = ImGui::GetScrollY() + ImGui::GetWindowHeight() >= ImGui::GetScrollMaxY() - 90.0f;
    float stream_progress = static_cast<float>(history.size()) + (app.ai.busy() ? 1.0f : 0.0f);
    if (stream_progress != last_stream_size)
    {
        autoscroll = stick;
        last_stream_size = stream_progress;
    }
    else if (stick)
        autoscroll = true;
    else if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 90.0f)
        autoscroll = false;
    if (autoscroll)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void c_chat_panel::render_plan_strip(c_ide_app& app)
{
    std::vector<plan_step_t> plan = app.tools.plan_snapshot();
    if (plan.empty())
        return;
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();

    int done = 0;
    for (const plan_step_t& step : plan)
    {
        if (step.done)
            ++done;
    }
    float progress = static_cast<float>(done) / static_cast<float>(plan.size());
    float strip_height = ImGui::GetTextLineHeight() + 16.0f * unit;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.accent, 0.06f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * unit);
    ImGui::BeginChild("##plan_strip", ImVec2(0.0f, strip_height), ImGuiChildFlags_Borders);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * unit);
    ImGui::Indent(12.0f * unit);
    ImGui::TextColored(theme.accent(), "%s", icon_list_check);
    ImGui::SameLine(0.0f, 8.0f * unit);
    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.9f);
    ImGui::TextColored(colors.text_dim, "план");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 8.0f * unit);
    ImGui::TextColored(colors.text_faint, "%d / %d", done, static_cast<int>(plan.size()));

    float bar_right = ImGui::GetContentRegionAvail().x - 30.0f * unit;
    ImVec2 bar_min = ImGui::GetCursorScreenPos();
    float bar_y = bar_min.y + ImGui::GetTextLineHeight() * 0.5f - 2.0f * unit;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar_min.x, bar_y), ImVec2(bar_min.x + bar_right, bar_y + 4.0f * unit), ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.border, 0.8f)), 2.0f * unit);
    float fill = bar_right * progress * (0.4f + 0.6f * anim::ease_out(progress));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar_min.x, bar_y), ImVec2(bar_min.x + fill, bar_y + 4.0f * unit), theme.accent_u32(0.9f), 2.0f * unit);

    ImGui::SameLine(ImGui::GetCursorPosX() + bar_right + 10.0f * unit);
    ImGui::TextColored(colors.text_faint, "%s", plan_open ? icon_chevron_down : icon_chevron_right);
    ImGui::Unindent(12.0f * unit);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked())
        plan_open = !plan_open;
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (!plan_open)
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * unit);
    ImGui::BeginChild("##plan_body", ImVec2(0.0f, ImGui::GetTextLineHeight() * static_cast<float>(plan.size()) + 20.0f * unit), ImGuiChildFlags_Borders);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * unit);
    ImGui::Indent(12.0f * unit);
    for (size_t i = 0; i < plan.size(); ++i)
    {
        const plan_step_t& step = plan[i];
        if (step.done)
        {
            ImGui::TextColored(colors.success, "%s", icon_check);
            ImGui::SameLine(0.0f, 8.0f * unit);
            ImGui::TextColored(colors.text_faint, "%s", step.text.c_str());
        }
        else
        {
            ImGui::TextColored(colors.text_faint, "%s", icon_circle);
            ImGui::SameLine(0.0f, 8.0f * unit);
            ImGui::TextColored(colors.text_dim, "%s", step.text.c_str());
        }
    }
    ImGui::Unindent(12.0f * unit);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
}

void c_chat_panel::render_message(c_ide_app& app, const message_t& message, int message_index, float wrap_width)
{
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float dt = ImGui::GetIO().DeltaTime;
    float now = anim::time_now();

    if (message.internal_note && message.role == "user")
    {
        ImGui::PushID(str::format("note_%d", message_index).c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.accent, 0.05f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f * unit);
        ImGui::BeginChild("##note", ImVec2(wrap_width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PushFont(theme.font_main, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(colors.text_faint, "%s", icon_compress);
        ImGui::SameLine(0.0f, 8.0f * unit);
        ImGui::TextColored(colors.text_faint, "%s", message.content.c_str());
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 8.0f * unit));
        return;
    }

    if (message.role == "tool")
        return;

    bool is_user = message.role == "user";
    if (!is_user)
    {
        message_view_t& view = message_views[message_index];
        if (view.born_time <= 0.0f)
            view.born_time = now;
    }

    ImGui::PushID(str::format("msg_%d", message_index).c_str());
    if (is_user)
    {
        std::string label = "ты";
        float label_width = ImGui::CalcTextSize(label.c_str()).x;
        float indent = wrap_width - wrap_width * 0.82f;
        ImGui::Indent(indent);
        ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.82f);
        ImGui::TextColored(colors.text_faint, "%s", label.c_str());
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.accent, 0.10f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * unit);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * unit, 9.0f * unit));
        ImGui::BeginChild("##user_bubble", ImVec2(wrap_width - indent, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysAutoResize);
        render_markdown(app, parse_markdown(message.content), wrap_width - indent - 28.0f * unit);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Unindent(indent);
        ImGui::Dummy(ImVec2(0.0f, 10.0f * unit));
        ImGui::PopID();
        return;
    }

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.82f);
    ImGui::TextColored(theme.accent(), "%s", "amyr");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 8.0f * unit);
    if (message.tool_calls.empty() && !message.content.empty())
        ImGui::TextColored(colors.text_faint, "%d токенов", message.token_estimate);
    if (!message.tool_calls.empty())
        ImGui::TextColored(colors.text_faint, "%zu %s", message.tool_calls.size(), message.tool_calls.size() > 1 ? "инструмента" : "инструмент");

    if (!message.content.empty())
    {
        message_view_t& view = message_views[message_index];
        std::string shown = reveal_content(message.content, view, now, dt);
        bool revealing = shown.size() < message.content.size();
        if (revealing)
            shown += " ▍";
        ImGui::Indent(2.0f * unit);
        render_markdown(app, parse_markdown(shown), wrap_width - 4.0f * unit);
        ImGui::Unindent(2.0f * unit);
    }

    if (!message.content.empty())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) ? 1.0f : 0.45f);
        if (icon_button(theme, icon_copy, "##copy_msg", "копировать ответ", 0.0f))
            ImGui::SetClipboardText(message.content.c_str());
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, 12.0f * unit));
}

void c_chat_panel::render_tool_card(c_ide_app& app, const tool_call_t& call, int message_index, int call_index, bool running, const std::string* output, float wrap_width)
{
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float now = anim::time_now();

    std::string card_id = call.id.empty() ? str::format("msg%d_call%d", message_index, call_index) : call.id;
    tool_card_state_t& state = tool_cards[card_id];
    if (state.appear_time <= 0.0f)
        state.appear_time = now;
    float appear = anim::ease_out((now - state.appear_time) / card_appear_seconds);

    card_status_t status = parse_tool_output(output);
    const char* icon = tool_icon(call.name);
    std::string label = tool_label(call.name, running || !status.has_output);
    std::string subtitle = tool_subtitle(call.name, call.arguments);

    ImGui::PushID(card_id.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, app.config.animations ? appear : 1.0f);
    ImGui::Indent(14.0f * unit);

    float card_height = ImGui::GetTextLineHeight() + 12.0f * unit;
    ImVec2 card_min = ImGui::GetCursorScreenPos();
    if (app.config.animations)
        card_min.y += (1.0f - appear) * 8.0f * unit;
    ImGui::SetCursorScreenPos(card_min);

    bool clickable = status.has_output && !status.details.empty();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.panel, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * unit);
    ImGui::BeginChild("##tool_card", ImVec2(wrap_width - 14.0f * unit, card_height), ImGuiChildFlags_Borders);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * unit);
    ImGui::Indent(11.0f * unit);

    ImVec2 icon_pos = ImGui::GetCursorScreenPos();
    float icon_line = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(icon_pos.x - 5.0f * unit, icon_pos.y - 4.0f * unit), ImVec2(icon_pos.x + icon_line + 5.0f * unit, icon_pos.y + icon_line + 4.0f * unit), running || !status.has_output ? theme.accent_u32(0.14f) : ImGui::ColorConvertFloat4ToU32(theme.with_alpha(status.ok ? colors.success : colors.danger, 0.13f)), 7.0f * unit);
    ImGui::TextColored(running || !status.has_output ? theme.accent() : status.ok ? colors.success : colors.danger, "%s", icon);
    ImGui::SameLine(0.0f, 10.0f * unit);
    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.88f);
    ImGui::TextColored(status.ok ? colors.text_dim : colors.danger, "%s", label);
    ImGui::PopFont();

    if (!subtitle.empty())
    {
        ImGui::SameLine(0.0f, 8.0f * unit);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.80f);
        ImGui::TextColored(colors.text_faint, "%s", subtitle.c_str());
        ImGui::PopFont();
    }

    if (status.has_stats)
    {
        std::string plus = str::format("+%d", status.added);
        std::string minus = str::format("−%d", status.removed);
        float stats_width = ImGui::CalcTextSize(plus.c_str()).x + ImGui::CalcTextSize(minus.c_str()).x + 14.0f * unit + (clickable ? 16.0f * unit : 0.0f);
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - stats_width);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.82f);
        ImGui::TextColored(colors.success, "%s", plus.c_str());
        ImGui::SameLine(0.0f, 6.0f * unit);
        ImGui::TextColored(colors.danger, "%s", minus.c_str());
        ImGui::PopFont();
    }
    else if (clickable)
    {
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.0f * unit);
        ImGui::TextColored(colors.text_faint, "%s", state.expanded ? icon_arrow_up : icon_arrow_down);
    }

    if (running || !status.has_output)
    {
        float spin_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 18.0f * unit;
        ImVec2 center(ImGui::GetWindowPos().x + spin_x, ImGui::GetCursorScreenPos().y + icon_line * 0.5f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float phase = std::fmod(now * 2.2f, 1.0f);
        draw->AddCircle(center, 6.0f * unit, theme.accent_u32(0.25f), 24, 2.0f * unit);
        float angle = phase * 6.28318f;
        draw->AddLine(ImVec2(center.x + std::cos(angle) * 6.0f * unit, center.y + std::sin(angle) * 6.0f * unit), ImVec2(center.x + std::cos(angle + 1.8f) * 6.0f * unit, center.y + std::sin(angle + 1.8f) * 6.0f * unit), theme.accent_u32(0.95f), 2.0f * unit);
    }

    ImGui::Unindent(11.0f * unit);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (clickable && ImGui::IsItemClicked())
        state.expanded = !state.expanded;
    if (ImGui::IsItemHovered() && clickable)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (state.expanded && clickable)
    {
        std::vector<std::string> detail_lines;
        detail_lines = str::split(status.details, '\n');
        float line_height = ImGui::GetTextLineHeight();
        float max_detail = 220.0f * unit;
        float detail_height = std::min(max_detail, line_height * static_cast<float>(detail_lines.size()) + 16.0f * unit);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.background, 0.9f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.78f);
        ImGui::BeginChild("##tool_details", ImVec2(wrap_width - 14.0f * unit, detail_height), ImGuiChildFlags_Borders);
        ImGui::TextColored(colors.text_dim, "%s", status.details.c_str());
        ImGui::EndChild();
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::Unindent(14.0f * unit);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));
    ImGui::PopID();
}

void c_chat_panel::render_composer(c_ide_app& app)
{
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    bool busy = app.ai.busy();

    if (app.ai.current_state() == ai_state::error)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.danger, 0.10f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * unit);
        ImGui::BeginChild("##error_card", ImVec2(0.0f, ImGui::GetTextLineHeight() + 22.0f * unit), ImGuiChildFlags_Borders);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7.0f * unit);
        ImGui::TextColored(colors.danger, "%s", icon_circle_xmark);
        ImGui::SameLine(0.0f, 9.0f * unit);
        std::string error_text = app.ai.error_text();
        ImGui::PushFont(theme.font_main, ImGui::GetStyle().FontSizeBase * 0.88f);
        ImGui::TextColored(colors.text_dim, "%s", str::truncate_middle(error_text, 160).c_str());
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 26.0f * unit);
        if (icon_button(theme, icon_xmark, "##err_close", "скрыть"))
            app.ai.reset_error();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));
    }

    if (!attached_files.empty())
    {
        for (size_t i = 0; i < attached_files.size(); ++i)
        {
            ImGui::PushID(str::format("attach_%d", static_cast<int>(i)).c_str());
            std::string name = std::filesystem::path(shell::to_wide(attached_files[i])).filename().string();
            chip(theme, str::format("%s %s", icon_file_lines, name).c_str(), true);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
            {
                attached_files.erase(attached_files.begin() + static_cast<long>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::SameLine(0.0f, 6.0f * unit);
        ImGui::TextColored(colors.text_faint, "правый клик по чипу — убрать");
        ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
    }

    float composer_height = ImGui::GetTextLineHeight() * static_cast<float>(composer_rows) + 24.0f * unit;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.elevated);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * unit, 10.0f * unit));
    ImGui::BeginChild("##composer", ImVec2(0.0f, composer_height), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    float button_size = ImGui::GetTextLineHeight() + 12.0f * unit;
    float inner_height = composer_height - 20.0f * unit;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_size - 12.0f * unit);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    bool submitted = ImGui::InputTextMultiline("##chat_input", &input, ImVec2(0.0f, inner_height), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();
    if (input_focus_requested)
    {
        ImGui::SetKeyboardFocusHere(-1);
        input_focus_requested = false;
    }
    bool enter_pressed = submitted || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiMod_Shift));

    ImGui::SameLine(0.0f, 12.0f * unit);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + inner_height - button_size);
    if (busy)
    {
        if (icon_button(theme, icon_stop, "##chat_stop", "остановить", button_size, colors.danger))
            app.ai.cancel();
    }
    else
    {
        bool can_send = !str::trim(input).empty();
        float pulse = can_send ? 0.75f + 0.25f * std::sin(anim::time_now() * 3.0f) : 0.0f;
        ImVec4 send_tint = can_send ? ImVec4(theme.accent().x * pulse + colors.elevated.x * (1.0f - pulse), theme.accent().y * pulse + colors.elevated.y * (1.0f - pulse), theme.accent().z * pulse + colors.elevated.z * (1.0f - pulse), 1.0f) : ImVec4(0, 0, 0, 0);
        ImGui::BeginDisabled(!can_send);
        bool clicked = icon_button(theme, icon_paper_plane, "##chat_send", "отправить (enter)", button_size, send_tint);
        ImGui::EndDisabled();
        if ((clicked && can_send) || (enter_pressed && can_send))
        {
            std::string message_text = str::trim(input);
            std::string context_block = build_context_block(app);
            input.clear();
            attached_files.clear();
            autoscroll = true;
            app.send_chat_message(message_text, context_block);
        }
    }

    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
    ImGui::TextColored(colors.text_faint, "enter — отправить · shift+enter — новая строка · перетащи файл сюда, чтобы приложить его");
}
