#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/string.hpp"
#include "core/utf8.hpp"
#include "platform/shell.hpp"

namespace
{
    constexpr float titlebar_height = 44.0f;
    constexpr float statusbar_height = 26.0f;
    constexpr float background_fade_seconds = 0.6f;

    std::string localize_hint(const std::string& hint)
    {
        if (hint == "thinking")
            return "думает";
        if (hint == "preparing context")
            return "готовлю контекст";
        if (hint == "compacting context")
            return "сжимаю контекст";
        return hint;
    }

    std::string read_whole_file(const std::string& path)
    {
        std::ifstream file(std::filesystem::path(shell::to_wide(path)), std::ios::binary);
        if (!file.good())
            return "";
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return utf8::ensure_utf8(buffer.str());
    }
}

c_ide_app::c_ide_app()
    : tools(tool_hooks_t{}),
      ai(config, tools)
{
}

void c_ide_app::initialize(void* hwnd)
{
    window_handle = hwnd;
    theme.load_fonts(shell::window_dpi_scale(hwnd));
    theme.accent_index = config.accent_index;
    theme.apply_style();

    tools.hooks = tool_hooks_t{
        .workspace = [this]() -> const c_workspace* { return &workspace; },
        .context_generation = [this]() { return ai.context_generation_value(); },
        .on_files_changed = [this]() { on_files_changed_external(); },
        .on_file_written = [this](const std::string& path, const std::string&, const std::string&) {
            toasts.push("файл изменён: " + path, icon_pen_to_square, theme.palette().accent);
        },
        .commands_allowed = [this]() { return config.allow_commands; }
    };

    config.load();
    theme.accent_index = config.accent_index;
    if (!config.workspace_path.empty() && shell::path_exists(config.workspace_path))
        set_workspace(config.workspace_path);
    else
        new_chat();

    startup_time = anim::time_now();
}

void c_ide_app::shutdown()
{
    ai.shutdown();
    config.workspace_path = workspace.valid ? workspace.root.string() : "";
    config.save();
    save_session();
    theme.shutdown();
}

void c_ide_app::set_workspace(const std::string& path)
{
    save_session();
    workspace.set_root(std::filesystem::path(shell::to_wide(path)));
    config.workspace_path = path;
    config.save();
    on_files_changed_external();
    scan_chats();
    load_last_chat();
    toasts.push("проект: " + workspace.display_name(), icon_folder_open, theme.palette().accent);
}

std::vector<chat_meta_t>& c_ide_app::chats()
{
    return chat_list;
}

std::string c_ide_app::chats_dir() const
{
    unsigned long long hash = 1469598103934665603ull;
    for (char symbol : workspace.root.string())
    {
        hash ^= static_cast<unsigned char>(symbol);
        hash *= 1099511628211ull;
    }
    return shell::appdata_dir() + str::format("\\chats\\%016llx", hash);
}

std::string c_ide_app::chat_file(const std::string& id) const
{
    return chats_dir() + "\\chat_" + id + ".json";
}

void c_ide_app::scan_chats()
{
    chat_list.clear();
    std::string directory = chats_dir();
    std::error_code error;
    if (!std::filesystem::is_directory(std::filesystem::path(shell::to_wide(directory)), error))
        return;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(std::filesystem::path(shell::to_wide(directory))))
    {
        std::string name = entry.path().filename().string();
        if (!name.starts_with("chat_") || !name.ends_with(".json"))
            continue;
        chat_meta_t meta;
        meta.id = name.substr(5, name.size() - 10);
        std::ifstream file(entry.path(), std::ios::binary);
        if (file.good())
        {
            std::ostringstream buffer;
            buffer << file.rdbuf();
            json_t state;
            if (json_t::parse(buffer.str(), state))
            {
                const json_t* messages = state.find("messages");
                if (messages && messages->is_array())
                {
                    for (size_t i = 0; i < messages->size(); ++i)
                    {
                        const json_t& message = messages->at(i);
                        if (message["role"].as_string() == "user" && !message["internal_note"].as_bool(false))
                        {
                            meta.title = str::truncate_middle(str::trim(message["content"].as_string()), 46);
                            break;
                        }
                    }
                }
            }
        }
        if (meta.title.empty())
            meta.title = "новый диалог";
        chat_list.push_back(std::move(meta));
    }
    std::sort(chat_list.begin(), chat_list.end(), [](const chat_meta_t& left, const chat_meta_t& right) { return left.id > right.id; });
}

void c_ide_app::load_last_chat()
{
    if (chat_list.empty())
    {
        new_chat();
        return;
    }
    std::ifstream file(std::filesystem::path(shell::to_wide(chat_file(chat_list.front().id))), std::ios::binary);
    if (!file.good())
    {
        new_chat();
        return;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ai.restore_session(buffer.str());
    ai.session_dirty = false;
    active_chat_id = chat_list.front().id;
}

void c_ide_app::new_chat()
{
    save_session();
    ai.clear_history();
    ai.session_dirty = false;
    long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    active_chat_id = str::format("%llx", stamp);
    chat_list.insert(chat_list.begin(), { active_chat_id, "новый диалог" });
}

void c_ide_app::open_chat(const std::string& id)
{
    if (ai.busy() || id == active_chat_id)
        return;
    save_session();
    std::ifstream file(std::filesystem::path(shell::to_wide(chat_file(id))), std::ios::binary);
    if (!file.good())
        return;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ai.clear_history();
    ai.restore_session(buffer.str());
    ai.session_dirty = false;
    active_chat_id = id;
}

void c_ide_app::delete_chat(const std::string& id)
{
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(shell::to_wide(chat_file(id))), error);
    chat_list.erase(std::remove_if(chat_list.begin(), chat_list.end(), [&id](const chat_meta_t& meta) { return meta.id == id; }), chat_list.end());
    if (id != active_chat_id)
        return;
    ai.clear_history();
    ai.session_dirty = false;
    active_chat_id.clear();
    if (!chat_list.empty())
        open_chat(chat_list.front().id);
    else
        new_chat();
}

void c_ide_app::open_workspace_dialog()
{
    std::string picked;
    if (!shell::pick_folder(picked))
        return;
    set_workspace(picked);
}

void c_ide_app::attach_dropped_file(const std::string& path)
{
    chat.attach_context_file(path);
    toasts.push("прикреплён: " + path, icon_file_lines, theme.palette().accent);
}

void c_ide_app::clear_conversation()
{
    ai.clear_history();
    ai.session_dirty = true;
    save_session();
    toasts.push("диалог очищен", icon_trash_can, theme.palette().text_dim);
}

void c_ide_app::export_conversation()
{
    std::string target = workspace.valid ? (workspace.root.string() + "\\amyrIDE-chat.md") : (shell::appdata_dir() + "\\chat-export.md");
    std::ofstream file(std::filesystem::path(shell::to_wide(target)), std::ios::binary | std::ios::trunc);
    if (!file.good())
    {
        toasts.push("не удалось экспортировать", icon_circle_xmark, theme.palette().danger);
        return;
    }
    std::vector<message_t> history = ai.history_snapshot();
    for (const message_t& message : history)
    {
        if (message.role == "tool")
            continue;
        file << (message.role == "user" ? "## ты\n\n" : "## nimbus\n\n") << message.content << "\n\n";
    }
    toasts.push("экспортировано: " + target, icon_download, theme.palette().success);
}

void c_ide_app::save_session()
{
    if (!ai.session_dirty || active_chat_id.empty() || !workspace.valid)
        return;
    std::string directory = chats_dir();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(shell::to_wide(directory)), error);
    std::ofstream file(std::filesystem::path(shell::to_wide(chat_file(active_chat_id))), std::ios::binary | std::ios::trunc);
    if (file.good())
        file << ai.serialize_session();
    ai.session_dirty = false;
}

void c_ide_app::apply_code_block(const std::string& path, const std::string& code)
{
    if (!workspace.valid)
    {
        toasts.push("проект не открыт", icon_triangle_exclamation, theme.palette().warning);
        return;
    }
    std::filesystem::path full = workspace.resolve(path);
    std::error_code error;
    std::filesystem::path parent = full.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, error);
    std::ofstream file(full, std::ios::binary | std::ios::trunc);
    if (!file.good())
    {
        toasts.push("не удалось записать " + path, icon_circle_xmark, theme.palette().danger);
        return;
    }
    file.write(code.data(), static_cast<std::streamsize>(code.size()));
    file.close();
    on_files_changed_external();
    toasts.push("записано: " + path, icon_pen_to_square, theme.palette().success);
}

std::string c_ide_app::read_file_for_context(const std::string& path)
{
    std::filesystem::path full = workspace.resolve(path);
    std::error_code error;
    if (!workspace.valid || !std::filesystem::is_regular_file(full, error))
        return "";
    std::string content = read_whole_file(full.string());
    if (content.size() > 60000)
        content = str::truncate_middle(content, 60000);
    return content;
}

void c_ide_app::send_chat_message(const std::string& text, const std::string& context_block)
{
    if (text.empty())
        return;
    ai.send_user_message(text, context_block);
    save_session();
}

void c_ide_app::on_files_changed_external()
{
    files_changed_flag = true;
}

void c_ide_app::update(float)
{
    update();
}

void c_ide_app::update()
{
    ImGuiIO& io = ImGui::GetIO();

    theme.font_base = ImGui::GetStyle().FontSizeBase;
    toasts.update(io.DeltaTime);

    if (files_changed_flag.exchange(false))
        workspace.rescan();

    handle_global_shortcuts(window_handle);
    draw_background();
    draw_titlebar(window_handle);

    ImGui::SetNextWindowPos(ImVec2(0.0f, titlebar_height * theme.scale()));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - titlebar_height * theme.scale() - statusbar_height * theme.scale()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.palette().background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##host", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    if (!workspace.valid)
        draw_welcome();
    else
        draw_assistant();

    settings.draw(*this);
    toasts.draw(theme);
    ImGui::End();

    draw_statusbar();
}

void c_ide_app::draw_background()
{
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const palette_t& colors = theme.palette();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, ImGui::ColorConvertFloat4ToU32(colors.background));

    float fade = anim::ease_out((anim::time_now() - startup_time) / background_fade_seconds);
    float t = anim::time_now();
    ImVec2 top_left(io.DisplaySize.x * 0.14f, io.DisplaySize.y * -0.12f);
    float drift_x = std::sin(t * 0.11f) * 46.0f;
    float drift_y = std::cos(t * 0.09f) * 34.0f;
    for (int layer = 9; layer >= 1; --layer)
    {
        float factor = static_cast<float>(layer) / 9.0f;
        draw->AddCircleFilled(ImVec2(top_left.x + drift_x, top_left.y + drift_y), 340.0f * factor, ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.010f * factor * fade)), 48);
    }
    ImVec2 bottom_right(io.DisplaySize.x * 0.92f, io.DisplaySize.y * 1.08f);
    float drift2_x = std::sin(t * 0.07f + 2.1f) * 40.0f;
    for (int layer = 7; layer >= 1; --layer)
    {
        float factor = static_cast<float>(layer) / 7.0f;
        draw->AddCircleFilled(ImVec2(bottom_right.x + drift2_x, bottom_right.y), 300.0f * factor, ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.007f * factor * fade)), 48);
    }
}

void c_ide_app::draw_welcome()
{
    c_theme& theme_ref = theme;
    const palette_t& colors = theme_ref.palette();
    float unit = theme_ref.scale();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec2 center(io.DisplaySize.x * 0.5f, ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y * 0.40f);

    float pulse = 0.5f + 0.5f * std::sin(anim::time_now() * 1.6f);
    glow_circle(draw, center, 40.0f * unit, colors.accent);
    for (int ring = 3; ring >= 1; --ring)
    {
        float ring_radius = (40.0f + static_cast<float>(ring) * 14.0f + pulse * 6.0f) * unit;
        draw->AddCircle(center, ring_radius, theme_ref.accent_u32(0.10f / static_cast<float>(ring)), 48, 1.5f * unit);
    }
    draw->AddCircleFilled(center, 40.0f * unit, theme_ref.accent_u32(0.14f), 48);
    draw->AddCircleFilled(center, 40.0f * unit, theme_ref.accent_u32(0.10f + 0.04f * pulse), 48);
    const char* logo_icon = icon_wand_magic_sparkles;
    ImVec2 icon_size = ImGui::CalcTextSize(logo_icon);
    draw->AddText(nullptr, 30.0f * unit, ImVec2(center.x - icon_size.x * 0.5f, center.y - icon_size.y * 0.5f), theme_ref.accent_u32(0.98f), logo_icon);

    float y = center.y + 58.0f * unit;
    const char* title = "Nimbus";
    float title_size_value = ImGui::GetStyle().FontSizeBase * 2.2f;
    ImVec2 title_size = theme_ref.font_bold->CalcTextSizeA(title_size_value, 32768.0f, 0.0f, title);
    draw->AddText(theme_ref.font_bold, title_size_value, ImVec2(center.x - title_size.x * 0.5f, y), ImGui::ColorConvertFloat4ToU32(colors.text), title);
    y += title_size.y + 8.0f * unit;

    const char* subtitle = "ai-ассистент, который сам читает, правит и запускает твой проект";
    float subtitle_size_value = ImGui::GetStyle().FontSizeBase;
    ImVec2 subtitle_size = theme_ref.font_main->CalcTextSizeA(subtitle_size_value, 32768.0f, 0.0f, subtitle);
    draw->AddText(theme_ref.font_main, subtitle_size_value, ImVec2(center.x - subtitle_size.x * 0.5f, y), ImGui::ColorConvertFloat4ToU32(colors.text_faint), subtitle);
    y += subtitle_size.y + 34.0f * unit;

    std::string button_label = std::string(icon_folder_open) + "   Open Project";
    ImVec2 button_size(260.0f * unit, 54.0f * unit);
    ImVec2 button_min(center.x - button_size.x * 0.5f, y);
    ImGui::SetCursorScreenPos(button_min);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f * unit);
    ImGui::PushStyleColor(ImGuiCol_Button, theme_ref.with_alpha(colors.accent, 0.22f + 0.06f * pulse));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_ref.with_alpha(colors.accent, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme_ref.with_alpha(colors.accent, 0.55f));
    ImGui::PushFont(theme_ref.font_bold, ImGui::GetStyle().FontSizeBase * 1.08f);
    bool clicked = ImGui::Button(button_label.c_str(), button_size);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddRect(ImVec2(button_min.x - 3.0f * unit, button_min.y - 3.0f * unit), ImVec2(button_min.x + button_size.x + 3.0f * unit, button_min.y + button_size.y + 3.0f * unit), theme_ref.accent_u32(0.45f), 18.0f * unit, 0, 2.0f * unit);
    }
    if (clicked)
        open_workspace_dialog();

    y += button_size.y + 22.0f * unit;
    const char* hint = "выбери папку проекта в проводнике — ассистент получит доступ к файлам";
    float hint_size_value = ImGui::GetStyle().FontSizeBase;
    ImVec2 hint_size = theme_ref.font_main->CalcTextSizeA(hint_size_value, 32768.0f, 0.0f, hint);
    draw->AddText(theme_ref.font_main, hint_size_value, ImVec2(center.x - hint_size.x * 0.5f, y), ImGui::ColorConvertFloat4ToU32(theme_ref.with_alpha(colors.text_faint, 0.75f)), hint);

    y += hint_size.y + 14.0f * unit;
    ImGui::SetCursorScreenPos(ImVec2(center.x - 90.0f * unit, y));
    if (ghost_button(theme_ref, std::string(icon_gear).append("   настройки").c_str(), ImVec2(180.0f * unit, 36.0f * unit)))
        settings.visible = true;
}

void c_ide_app::draw_assistant()
{
    float unit = theme.scale();
    ImGui::Indent(14.0f * unit);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
    chat.render(*this);
    ImGui::Unindent(14.0f * unit);
}

void c_ide_app::draw_titlebar(void* hwnd)
{
    ImGuiIO& io = ImGui::GetIO();
    c_theme& theme_ref = theme;
    const palette_t& colors = theme_ref.palette();
    float unit = theme_ref.scale();
    float bar_height = titlebar_height * unit;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(io.DisplaySize.x, bar_height), ImGui::ColorConvertFloat4ToU32(colors.background));
    draw->AddLine(ImVec2(0, bar_height - 1.0f), ImVec2(io.DisplaySize.x, bar_height - 1.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.6f)));

    float logo_center_y = bar_height * 0.5f;
    ImGui::PushFont(theme_ref.font_bold, ImGui::GetStyle().FontSizeBase * 0.98f);
    draw->AddText(ImVec2(24.0f * unit, logo_center_y - ImGui::CalcTextSize("Nimbus").y * 0.5f), ImGui::ColorConvertFloat4ToU32(theme_ref.with_alpha(colors.text, 0.85f)), "Nimbus");
    ImGui::PopFont();
}

void c_ide_app::draw_statusbar()
{
    ImGuiIO& io = ImGui::GetIO();
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float bar_height = statusbar_height * unit;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float y = io.DisplaySize.y - bar_height;

    draw->AddRectFilled(ImVec2(0, y), ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGui::ColorConvertFloat4ToU32(colors.background));
    draw->AddLine(ImVec2(0, y), ImVec2(io.DisplaySize.x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.6f)));

    std::string left_label = workspace.valid ? workspace.root.string() : "проект не открыт";
    if (left_label.size() > 64)
        left_label = "..." + left_label.substr(left_label.size() - 61);
    draw->AddText(nullptr, 12.5f * unit, ImVec2(14.0f * unit, y + bar_height * 0.5f - ImGui::CalcTextSize(left_label.c_str()).y * 0.5f), ImGui::ColorConvertFloat4ToU32(colors.text_faint), left_label.c_str());

    ai_state state = ai.current_state();
    ImVec4 dot_color = colors.text_faint;
    bool pulse = false;
    std::string state_label = "готов";
    if (state == ai_state::streaming || state == ai_state::preparing || state == ai_state::compacting)
    {
        dot_color = colors.accent;
        pulse = true;
        state_label = localize_hint(ai.stream_hint());
    }
    else if (state == ai_state::executing)
    {
        dot_color = colors.warning;
        pulse = true;
        state_label = "выполняет инструменты";
    }
    else if (state == ai_state::error)
    {
        dot_color = colors.danger;
        state_label = "ошибка";
    }

    float right_x = io.DisplaySize.x - 14.0f * unit;
    ImVec2 state_size = ImGui::CalcTextSize(state_label.c_str());
    draw->AddText(nullptr, 12.5f * unit, ImVec2(right_x - state_size.x, y + bar_height * 0.5f - state_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(colors.text_dim), state_label.c_str());
    status_dot(theme, ImVec2(right_x - state_size.x - 14.0f * unit, y + bar_height * 0.5f), dot_color, pulse);
    right_x -= state_size.x + 34.0f * unit;

    std::string model_label = config.model;
    ImVec2 model_size = ImGui::CalcTextSize(model_label.c_str());
    draw->AddText(nullptr, 12.5f * unit, ImVec2(right_x - model_size.x, y + bar_height * 0.5f - model_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(colors.text_faint), model_label.c_str());
    right_x -= model_size.x + 20.0f * unit;

    std::string token_label = str::format("%s / %s", str::format_count(ai.total_tokens()).c_str(), str::format_count(config.context_limit).c_str());
    ImVec2 token_size = ImGui::CalcTextSize(token_label.c_str());
    draw->AddText(nullptr, 12.5f * unit, ImVec2(right_x - token_size.x, y + bar_height * 0.5f - token_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ai.total_tokens() > config.context_limit - config.response_reserve ? colors.warning : colors.text_faint), token_label.c_str());
}

void c_ide_app::handle_global_shortcuts(void*)
{
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = ImGui::IsKeyDown(ImGuiMod_Ctrl);

    if (io.WantTextInput)
        return;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false))
        settings.visible = !settings.visible;
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && settings.visible)
        settings.visible = false;
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
        clear_conversation();
}
