#include "chats.hpp"

#include <cctype>
#include <chrono>
#include <cmath>

#include <imgui.h>

#include "app.hpp"
#include "core/string.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
    std::string to_lower_copy(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (char symbol : text)
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(symbol))));
        return out;
    }

    long long epoch_day(long long seconds)
    {
        return seconds / 86400;
    }

    long long today_day()
    {
        return epoch_day(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }
}

void c_chats_overlay::draw(c_ide_app& app)
{
    if (!visible)
        return;
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    ImGuiIO& io = ImGui::GetIO();

    if (selected_project.empty() && app.workspace.valid)
        selected_project = app.workspace.root.string();
    if (!app.projects().empty() && selected_project.empty())
        selected_project = app.projects().front().path;

    ImVec2 card_size(std::min(940.0f * unit, io.DisplaySize.x - 80.0f), std::min(620.0f * unit, io.DisplaySize.y - 90.0f));
    ImVec2 card_min((io.DisplaySize.x - card_size.x) * 0.5f, (io.DisplaySize.y - card_size.y) * 0.5f);

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.50f)));
    soft_shadow(draw, card_min, ImVec2(card_min.x + card_size.x, card_min.y + card_size.y), 20.0f * unit, 0.6f);

    float title_height = 52.0f * unit;
    ImGui::SetNextWindowPos(card_min);
    ImGui::SetNextWindowSize(card_size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f * unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##chats_overlay", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::SetCursorPos(ImVec2(22.0f * unit, 15.0f * unit));
    ImGui::TextColored(colors.text, "диалоги");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(card_size.x - 40.0f * unit, 10.0f * unit));
    if (icon_button(theme, icon_xmark, "##chats_close", "закрыть (esc)"))
        visible = false;

    float body_top = title_height;
    float left_width = 250.0f * unit;
    float body_height = card_size.y - body_top;

    ImGui::SetCursorPos(ImVec2(0.0f, body_top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.with_alpha(colors.background, 0.65f));
    ImGui::BeginChild("##projects_col", ImVec2(left_width, body_height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * unit);
    ImGui::Indent(16.0f * unit);
    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.86f);
    ImGui::TextColored(colors.text_faint, "проекты");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 30.0f * unit);
    if (icon_button(theme, icon_plus, "##add_project", "добавить папку"))
        add_requested = true;
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));

    for (const project_info_t& project : app.projects())
    {
        bool active = project.path == selected_project;
        ImGui::PushID(project.path.c_str());
        ImVec2 row_min = ImGui::GetCursorScreenPos();
        float row_width = left_width - 24.0f * unit;
        float row_height = ImGui::GetTextLineHeight() + 14.0f * unit;
        ImGui::InvisibleButton("##project_row", ImVec2(row_width, row_height));
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
            draw->AddRectFilled(row_min, ImVec2(row_min.x + row_width, row_min.y + row_height), theme.accent_u32(active ? 0.16f : 0.09f), 9.0f * unit);
        else if (active)
            draw->AddRectFilled(row_min, ImVec2(row_min.x + row_width, row_min.y + row_height), theme.accent_u32(0.14f), 9.0f * unit);

        float badge_radius = ImGui::GetTextLineHeight() * 0.48f;
        ImVec2 badge_center(row_min.x + 10.0f * unit + badge_radius, row_min.y + row_height * 0.5f);
        draw->AddCircleFilled(badge_center, badge_radius, active ? theme.accent_u32(0.85f) : ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.text_dim, 0.30f)), 24);
        std::string letter = project.name.empty() ? "?" : project.name.substr(0, 1);
        ImVec2 letter_size = ImGui::CalcTextSize(letter.c_str());
        draw->AddText(nullptr, ImGui::GetTextLineHeight() * 0.8f, ImVec2(badge_center.x - letter_size.x * 0.5f, badge_center.y - letter_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(active ? ImVec4(0.04f, 0.07f, 0.11f, 1.0f) : colors.text), letter.c_str());

        std::string name = project.name;
        float name_limit = row_width - 34.0f * unit;
        while (!name.empty() && ImGui::CalcTextSize(name.c_str()).x > name_limit)
            name.pop_back();
        if (name != project.name)
            name += "...";
        draw->AddText(ImVec2(row_min.x + 30.0f * unit, row_min.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f), ImGui::ColorConvertFloat4ToU32(active ? colors.text : colors.text_dim), name.c_str());

        if (ImGui::IsItemClicked())
        {
            selected_project = project.path;
            search[0] = '\0';
        }
        ImGui::PopID();
    }
    ImGui::Unindent(16.0f * unit);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    float right_x = left_width + 1.0f * unit;
    float right_width = card_size.x - right_x;
    ImGui::SetCursorPos(ImVec2(right_x, body_top));
    ImGui::BeginChild("##sessions_col", ImVec2(right_width, body_height), ImGuiChildFlags_None);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * unit);
    ImGui::Indent(20.0f * unit);

    float new_button_width = 150.0f * unit;
    float search_width = right_width - 40.0f * unit - new_button_width - 12.0f * unit;
    ImGui::SetNextItemWidth(search_width);
    search_input(theme, "##chat_search", search, sizeof(search), "поиск сессий", search_width);
    ImGui::SameLine(0.0f, 12.0f * unit);
    if (accent_button(theme, "новая сессия", ImVec2(new_button_width, ImGui::GetTextLineHeight() + 12.0f * unit)))
        new_session_requested = true;
    ImGui::Dummy(ImVec2(0.0f, 10.0f * unit));

    std::string needle = to_lower_copy(str::trim(search));
    long long today = today_day();
    const char* current_group = nullptr;
    bool any = false;
    for (const chat_meta_t& meta : app.chats())
    {
        if (meta.project_path != selected_project)
            continue;
        if (!needle.empty() && to_lower_copy(meta.title).find(needle) == std::string::npos)
            continue;
        any = true;
        const char* group = epoch_day(meta.updated) >= today ? "сегодня" : epoch_day(meta.updated) == today - 1 ? "вчера" : "ранее";
        if (group != current_group)
        {
            current_group = group;
            ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
            ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.82f);
            ImGui::TextColored(colors.text_faint, "%s", group);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0.0f, 2.0f * unit));
        }

        ImGui::PushID(meta.id.c_str());
        ImVec2 row_min = ImGui::GetCursorScreenPos();
        float row_width = right_width - 40.0f * unit;
        float row_height = ImGui::GetTextLineHeight() + 16.0f * unit;
        ImGui::InvisibleButton("##session_row", ImVec2(row_width, row_height));
        bool hovered = ImGui::IsItemHovered();
        bool active = meta.active;
        if (hovered || active)
            draw->AddRectFilled(row_min, ImVec2(row_min.x + row_width, row_min.y + row_height), theme.accent_u32(active ? 0.15f : hovered ? 0.08f : 0.0f), 10.0f * unit);

        float badge_radius = ImGui::GetTextLineHeight() * 0.48f;
        ImVec2 badge_center(row_min.x + 12.0f * unit + badge_radius, row_min.y + row_height * 0.5f);
        draw->AddCircleFilled(badge_center, badge_radius, active ? theme.accent_u32(0.85f) : ImGui::ColorConvertFloat4ToU32(theme.with_alpha(colors.text_dim, 0.28f)), 24);
        std::string letter = meta.project_name.empty() ? "?" : meta.project_name.substr(0, 1);
        ImVec2 letter_size = ImGui::CalcTextSize(letter.c_str());
        draw->AddText(nullptr, ImGui::GetTextLineHeight() * 0.8f, ImVec2(badge_center.x - letter_size.x * 0.5f, badge_center.y - letter_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(active ? ImVec4(0.04f, 0.07f, 0.11f, 1.0f) : colors.text_dim), letter.c_str());

        std::string title = meta.title;
        float title_limit = row_width - 96.0f * unit;
        while (!title.empty() && ImGui::CalcTextSize(title.c_str()).x > title_limit)
            title.pop_back();
        if (title != meta.title)
            title += "...";
        draw->AddText(ImVec2(row_min.x + 34.0f * unit, row_min.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f), ImGui::ColorConvertFloat4ToU32(active ? colors.text : colors.text_dim), title.c_str());

        if (hovered)
        {
            ImVec2 del_center(row_min.x + row_width - 16.0f * unit, row_min.y + row_height * 0.5f);
            ImVec2 glyph = theme.font_main->CalcTextSizeA(ImGui::GetTextLineHeight() * 0.78f, 32768.0f, 0.0f, icon_xmark);
            draw->AddText(nullptr, ImGui::GetTextLineHeight() * 0.78f, ImVec2(del_center.x - glyph.x * 0.5f, del_center.y - glyph.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.danger.x, colors.danger.y, colors.danger.z, ImGui::IsMouseHoveringRect(ImVec2(del_center.x - 12.0f * unit, del_center.y - 12.0f * unit), ImVec2(del_center.x + 12.0f * unit, del_center.y + 12.0f * unit)) ? 1.0f : 0.6f)), icon_xmark);
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ImVec2(del_center.x - 13.0f * unit, del_center.y - 13.0f * unit), ImVec2(del_center.x + 13.0f * unit, del_center.y + 13.0f * unit)))
            {
                app.delete_chat(meta.id);
                ImGui::PopID();
                break;
            }
        }

        if (ImGui::IsItemClicked() && !app.ai.busy())
        {
            open_requested_id = meta.id;
            open_requested_project = meta.project_path;
        }
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextColored(colors.text_faint, "пока нет сессий — начни новую");

    ImGui::Unindent(20.0f * unit);
    ImGui::EndChild();
    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        visible = false;

    if (add_requested)
    {
        add_requested = false;
        visible = false;
        app.add_current_project();
    }
    if (new_session_requested)
    {
        new_session_requested = false;
        visible = false;
        if (app.workspace.valid && selected_project == app.workspace.root.string())
            app.new_chat();
        else
        {
            app.set_workspace(selected_project);
            app.new_chat();
        }
    }
    if (!open_requested_id.empty())
    {
        std::string id = open_requested_id;
        std::string project = open_requested_project;
        open_requested_id.clear();
        open_requested_project.clear();
        visible = false;
        for (const chat_meta_t& meta : app.chats())
        {
            if (meta.id == id && meta.project_path == project)
            {
                app.open_chat(meta);
                break;
            }
        }
    }
}
