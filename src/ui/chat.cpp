#include "chat.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "app.hpp"
#include "core/string.hpp"
#include "core/tokens.hpp"
#include "platform/shell.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
    constexpr int composer_rows = 3;
    constexpr float reveal_bytes_per_second = 850.0f;
    constexpr float reveal_catchup_bytes = 1100.0f;
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
            return running ? "Изучает файлы" : "Изучил файлы";
        if (name == "list_dir")
            return running ? "Смотрит папку" : "Посмотрел папку";
        if (name == "workspace_info")
            return running ? "Смотрит проект" : "Изучил проект";
        if (name == "read_file")
            return running ? "Читает файл" : "Прочитал файл";
        if (name == "file_info")
            return running ? "Смотрит файл" : "Посмотрел файл";
        if (name == "write_file")
            return running ? "Создаёт файл" : "Создал файл";
        if (name == "append_file")
            return running ? "Дописывает файл" : "Дописал файл";
        if (name == "edit_file")
            return running ? "Правит файл" : "Изменил файл";
        if (name == "replace_lines")
            return running ? "Переписывает строки" : "Переписал строки";
        if (name == "insert_lines")
            return running ? "Вставляет строки" : "Вставил строки";
        if (name == "delete_lines")
            return running ? "Удаляет строки" : "Удалил строки";
        if (name == "create_directory")
            return running ? "Создаёт папку" : "Создал папку";
        if (name == "copy_path")
            return running ? "Копирует" : "Скопировал";
        if (name == "delete_path")
            return running ? "Удаляет" : "Удалил";
        if (name == "move_path")
            return running ? "Перемещает" : "Переместил";
        if (name == "search_files")
            return running ? "Ищет файлы" : "Нашёл файлы";
        if (name == "search_text")
            return running ? "Ищет в коде" : "Поиск в коде";
        if (name == "set_plan")
            return running ? "Строит план" : "Обновил план";
        if (name == "run_command")
            return running ? "Выполняет команду" : "Выполнил команду";
        if (name == "run_powershell")
            return running ? "Запускает PowerShell" : "Выполнил PowerShell";
        return running ? "Работает" : "Готово";
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
    if (editing_index >= 0)
        below_height += line_height + 14.0f * unit;

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

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase);
    ImGui::TextColored(colors.text, "%s", "Nimbus");
    ImGui::PopFont();

    float control_height = ImGui::GetTextLineHeight() + 10.0f * unit;
    float gear_size = ImGui::GetTextLineHeight() + 8.0f * unit;
    float right = ImGui::GetContentRegionMax().x;
    ImGui::SameLine(right - gear_size);
    if (icon_button(theme, icon_gear, "##chat_settings", "настройки (ctrl+,)"))
        app.settings.visible = true;

    float ring_radius = ImGui::GetTextLineHeight() * 0.52f;
    float ring_x = right - gear_size - 26.0f * unit - ring_radius;
    float row_center = ImGui::GetCursorScreenPos().y + control_height * 0.5f;
    ImVec2 ring_center(ring_x, row_center);
    double used = static_cast<double>(app.ai.total_tokens());
    double limit = static_cast<double>(std::max(app.config.context_limit, 1));
    double fraction = std::clamp(used / limit, 0.0, 1.0);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(ring_center, ring_radius, ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.border, 0.9f)), 28, 3.0f * unit);
    if (fraction > 0.003)
    {
        float sweep = -1.5707963f + static_cast<float>(fraction) * 6.2831853f;
        ImVec4 ring_color = fraction > 0.92 ? colors.danger : fraction > 0.8 ? colors.warning : colors.accent;
        draw->PathArcTo(ring_center, ring_radius, -1.5707963f, sweep, 30);
        draw->PathStroke(ImGui::ColorConvertFloat4ToU32(ring_color), 0, 3.0f * unit);
    }
    ImVec2 ring_min(ring_x - ring_radius - 4.0f * unit, row_center - ring_radius - 4.0f * unit);
    ImVec2 ring_max(ring_x + ring_radius + 4.0f * unit, row_center + ring_radius + 4.0f * unit);
    ImGui::InvisibleButton("##context_ring", ImVec2(ring_max.x - ring_min.x, control_height));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("контекст: %s / %s токенов\nосталось: %s\nмодель: %s", str::format_count(app.ai.total_tokens()).c_str(), str::format_count(app.config.context_limit).c_str(), str::format_count(std::max(app.config.context_limit - app.ai.total_tokens(), 0)).c_str(), app.config.model.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));
    float meter_width = ImGui::GetContentRegionAvail().x;
    token_meter(theme, app.ai.total_tokens(), app.config.context_limit, meter_width);

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

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * unit, 6.0f * unit));
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

    bool busy = app.ai.busy();
    bool stick = ImGui::GetScrollY() + ImGui::GetWindowHeight() >= ImGui::GetScrollMaxY() - 90.0f;
    float stream_progress = static_cast<float>(history.size()) + (busy ? 1.0f : 0.0f);
    if (stream_progress != last_stream_size && stick)
        autoscroll = true;
    last_stream_size = stream_progress;
    if (!busy)
        autoscroll = false;
    if (autoscroll && busy && stick)
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
        ImGui::BeginChild("##note", ImVec2(wrap_width, 0.0f), ImGuiChildFlags_AutoResizeY);
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
        float indent = wrap_width * 0.14f;
        float bubble_width = wrap_width - indent;
        ImGui::Indent(indent);
        ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.82f);
        ImGui::TextColored(colors.text_faint, "%s", "ты");
        ImGui::PopFont();
        if (!app.ai.busy())
        {
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24.0f * unit);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75f);
            if (icon_button(theme, icon_pen, "##edit_msg", "редактировать и переотправить", 0.0f))
            {
                input = message.content;
                editing_index = message_index;
                input_focus_requested = true;
            }
            ImGui::PopStyleVar();
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.accent, 0.10f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * unit);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * unit, 12.0f * unit));
        ImGui::BeginChild("##user_bubble", ImVec2(bubble_width, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        render_markdown(app, parse_markdown(message.content), bubble_width - 34.0f * unit);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Unindent(indent);
        ImGui::Dummy(ImVec2(0.0f, 12.0f * unit));
        ImGui::PopID();
        return;
    }

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.82f);
    ImGui::TextColored(theme.accent(), "%s", "nimbus");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 8.0f * unit);
    if (message.tool_calls.empty() && !message.content.empty())
        ImGui::TextColored(colors.text_faint, "%d токенов", message.token_estimate);
    if (!message.tool_calls.empty())
        ImGui::TextColored(colors.text_faint, "%zu %s", message.tool_calls.size(), message.tool_calls.size() > 1 ? "инструментов" : "инструмент");

    if (!message.content.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.panel, 0.72f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * unit);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * unit, 12.0f * unit));
        ImGui::BeginChild("##ai_bubble", ImVec2(wrap_width, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        std::string shown;
        if (app.config.animations)
        {
            message_view_t& view = message_views[message_index];
            shown = reveal_content(message.content, view, now, dt);
            if (shown.size() < message.content.size())
                shown += " ▍";
        }
        else
            shown = message.content;
        render_markdown(app, parse_markdown(shown), wrap_width - 34.0f * unit);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
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

    float card_height = ImGui::GetTextLineHeight() + 18.0f * unit;
    ImVec2 card_min = ImGui::GetCursorScreenPos();
    if (app.config.animations)
        card_min.y += (1.0f - appear) * 8.0f * unit;
    ImGui::SetCursorScreenPos(card_min);

    bool clickable = status.has_output && !status.details.empty();
    float card_width = wrap_width - 14.0f * unit;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.panel, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 11.0f * unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * unit, 8.0f * unit));
    ImGui::BeginChild("##tool_card", ImVec2(card_width, card_height), ImGuiChildFlags_Borders);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float right_local = ImGui::GetWindowWidth() - 13.0f * unit;

    ImVec2 icon_pos = ImGui::GetCursorScreenPos();
    float icon_line = ImGui::GetTextLineHeight();
    float icon_draw = icon_line * 0.95f;
    ImVec2 glyph = theme.font_main->CalcTextSizeA(icon_draw, 32768.0f, 0.0f, icon);
    float box_round = 8.0f * unit;
    draw->AddRectFilled(ImVec2(icon_pos.x - 5.0f * unit, icon_pos.y - 4.0f * unit), ImVec2(icon_pos.x + icon_line + 5.0f * unit, icon_pos.y + icon_line + 4.0f * unit), running || !status.has_output ? theme.accent_u32(0.16f) : ImGui::ColorConvertFloat4ToU32(theme.with_alpha(status.ok ? colors.success : colors.danger, 0.15f)), box_round);
    draw->AddText(theme.font_main, icon_draw, ImVec2(icon_pos.x + (icon_line - glyph.x) * 0.5f, icon_pos.y + (icon_line - glyph.y) * 0.5f), ImGui::ColorConvertFloat4ToU32(running || !status.has_output ? theme.accent() : status.ok ? colors.success : colors.danger), icon);
    ImGui::Dummy(ImVec2(icon_line + 10.0f * unit, icon_line));
    ImGui::SameLine(0.0f, 10.0f * unit);

    float label_size = ImGui::CalcTextSize(label.c_str()).x;
    float path_size = subtitle.empty() ? 0.0f : ImGui::CalcTextSize(subtitle.c_str()).x + ImGui::CalcTextSize(" â ").x;
    float tail_width = 26.0f * unit;
    if (status.has_stats)
        tail_width = ImGui::CalcTextSize("+000 −000").x + 20.0f * unit;
    float space_left = right_local - ImGui::GetCursorPosX() - tail_width;
    std::string shown_path = subtitle;
    if (path_size > space_left && !subtitle.empty())
    {
        while (!shown_path.empty() && ImGui::CalcTextSize(shown_path.c_str()).x + ImGui::CalcTextSize(" â ...").x > space_left)
            shown_path.pop_back();
        shown_path = str::trim(shown_path) + "...";
    }

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.90f);
    ImGui::TextColored(status.ok || running ? colors.text : colors.danger, "%s", label);
    ImGui::PopFont();
    if (!shown_path.empty())
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(colors.text_dim, " â %s", shown_path.c_str());
        ImGui::PopFont();
    }

    if (running || !status.has_output)
    {
        ImVec2 center(ImGui::GetWindowPos().x + right_local - 8.0f * unit, ImGui::GetWindowPos().y + card_height * 0.5f);
        float phase = std::fmod(now * 2.2f, 1.0f);
        float angle = phase * 6.28318f;
        draw->AddCircle(center, 6.5f * unit, theme.accent_u32(0.22f), 24, 2.0f * unit);
        draw->AddLine(ImVec2(center.x + std::cos(angle) * 6.5f * unit, center.y + std::sin(angle) * 6.5f * unit), ImVec2(center.x + std::cos(angle + 1.9f) * 6.5f * unit, center.y + std::sin(angle + 1.9f) * 6.5f * unit), theme.accent_u32(0.95f), 2.0f * unit);
    }
    else if (status.has_stats)
    {
        std::string plus = str::format("+%d", status.added);
        std::string minus = str::format("−%d", status.removed);
        float stats_width = ImGui::CalcTextSize(plus.c_str()).x + ImGui::CalcTextSize(minus.c_str()).x + 6.0f * unit;
        ImGui::SameLine(right_local - stats_width);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(colors.success, "%s", plus.c_str());
        ImGui::SameLine(0.0f, 6.0f * unit);
        ImGui::TextColored(colors.danger, "%s", minus.c_str());
        ImGui::PopFont();
    }
    else if (clickable)
    {
        ImGui::SameLine(right_local - 14.0f * unit);
        ImGui::TextColored(colors.text_faint, "%s", state.expanded ? icon_arrow_up : icon_arrow_down);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (clickable && ImGui::IsItemClicked())
        state.expanded = !state.expanded;
    if (ImGui::IsItemHovered() && clickable)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (state.expanded && clickable)
    {
        float line_height = ImGui::GetTextLineHeight();
        float detail_height = std::min(240.0f * unit, line_height * (static_cast<float>(str::split(status.details, '\n').size()) + 0.6f) + 18.0f * unit);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.background, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * unit);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.80f);
        ImGui::BeginChild("##tool_details", ImVec2(card_width, detail_height), ImGuiChildFlags_Borders);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::InputTextMultiline("##details", &status.details, ImVec2(0.0f, 0.0f), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
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

    if (editing_index >= 0)
    {
        ImGui::PushID("editing_chip");
        if (chip(theme, str::format("%s редактирование сообщения — отправка перепишет диалог с этого места (клик — отмена)", icon_pen).c_str(), true))
            editing_index = -1;
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
    }

    float button_size = ImGui::GetTextLineHeight() + 12.0f * unit;
    float inner_height = composer_height - 20.0f * unit;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_size * 2.0f - 24.0f * unit);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    bool submitted = ImGui::InputTextMultiline("##chat_input", &input, ImVec2(0.0f, inner_height), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();
    if (input.empty() && !ImGui::IsItemActive())
    {
        ImVec2 input_min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(input_min.x + 7.0f * unit, input_min.y + 7.0f * unit), ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.text_faint, 0.85f)), "спроси что-нибудь о проекте...");
    }
    if (input_focus_requested)
    {
        ImGui::SetKeyboardFocusHere(-1);
        input_focus_requested = false;
    }
    bool enter_pressed = submitted || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiMod_Shift));

    ImGui::SameLine(0.0f, 12.0f * unit);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + inner_height - button_size);
    if (icon_button(theme, icon_plus, "##chat_attach", "прикрепить файлы", button_size))
    {
        for (const std::string& picked : shell::pick_files())
            attach_context_file(picked);
    }

    ImGui::SameLine(0.0f, 12.0f * unit);
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
            if (editing_index >= 0)
            {
                app.ai.truncate_from(editing_index);
                editing_index = -1;
            }
            std::string context_block = build_context_block(app);
            input.clear();
            attached_files.clear();
            autoscroll = true;
            ImGui::ClearActiveID();
            app.send_chat_message(message_text, context_block);
        }
    }

    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
    ImGui::TextColored(colors.text_faint, "enter — отправить · shift+enter — новая строка · [+] прикрепить файлы или перетащи их в окно");
}
