#include "settings.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

#include "app.hpp"
#include "core/config.hpp"
#include "core/string.hpp"
#include "platform/http.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
    void copy_to_buffer(char* buffer, size_t capacity, const std::string& text)
    {
        size_t length = text.size() < capacity - 1 ? text.size() : capacity - 1;
        std::memcpy(buffer, text.c_str(), length);
        buffer[length] = '\0';
    }
}

void c_settings_panel::open()
{
    visible = true;
}

void c_settings_panel::render_connection(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();

    section_header(theme, "подключение");
    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));

    ImGui::TextColored(app.theme.palette().text_dim, "endpoint");
    ImGui::SetNextItemWidth(-120.0f * unit);
    copy_to_buffer(endpoint_buffer, sizeof(endpoint_buffer), app.config.endpoint_url);
    if (ImGui::InputText("##endpoint", endpoint_buffer, sizeof(endpoint_buffer)))
        app.config.endpoint_url = endpoint_buffer;

    ImGui::TextColored(app.theme.palette().text_dim, "api key");
    ImGui::SetNextItemWidth(-120.0f * unit);
    if (!show_key)
    {
        std::string masked(app.config.api_key.size(), '*');
        copy_to_buffer(key_buffer, sizeof(key_buffer), masked);
        if (ImGui::InputText("##apikey", key_buffer, sizeof(key_buffer), ImGuiInputTextFlags_ReadOnly))
        {
        }
    }
    else
    {
        copy_to_buffer(key_buffer, sizeof(key_buffer), app.config.api_key);
        if (ImGui::InputText("##apikey", key_buffer, sizeof(key_buffer)))
            app.config.api_key = key_buffer;
    }
    ImGui::SameLine();
    if (icon_button(theme, show_key ? icon_eye : icon_key, "##toggle_key", show_key ? "скрыть" : "показать"))
        show_key = !show_key;

    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
    if (accent_button(theme, testing_connection ? "проверяю..." : "проверить соединение"))
    {
        testing_connection = true;
        std::thread([this, url = app.config.endpoint_url]() {
            c_http_client client;
            client.set_insecure_skip_verify(true);
            std::vector<std::pair<std::string, std::string>> headers;
            std::string response;
            http_result_t result;
            bool reached = client.get(url + "/v1/models", headers, response, result);
            connection_status = reached ? str::format("connected · http %d", result.status_code) : "no connection";
            testing_connection = false;
            connection_status_time = anim::time_now();
        }).detach();
    }
    ImGui::SameLine();
    if (!connection_status.empty())
    {
        float fresh = 1.0f - std::clamp((anim::time_now() - connection_status_time) / 8.0f, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f + 0.6f * fresh);
        ImGui::TextColored(connection_status.starts_with("connected") ? app.theme.palette().success : app.theme.palette().danger, "%s", connection_status.c_str());
        ImGui::PopStyleVar();
    }
}

void c_settings_panel::render_context(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();

    ImGui::Dummy(ImVec2(0.0f, 10.0f * unit));
    section_header(theme, "контекст");
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));

    int limit = app.config.context_limit;
    ImGui::SetNextItemWidth(-120.0f * unit);
    if (ImGui::SliderInt("##context_limit", &limit, 16000, 200000, " "))
        app.config.context_limit = limit;
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "лимит %s токенов", str::format_count(limit).c_str());

    int reserve = app.config.response_reserve;
    ImGui::SetNextItemWidth(-120.0f * unit);
    if (ImGui::SliderInt("##response_reserve", &reserve, 4096, 65536, " "))
        app.config.response_reserve = reserve;
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "резерв ответа %s", str::format_count(reserve).c_str());

    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
    if (toggle_switch(theme, "##auto_compact", &app.config.auto_compact))
    {
    }
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "автосжатие у лимита");
    ImGui::TextColored(app.theme.palette().text_faint, "вывод инструментов обрезается, старые ходы суммируются — экономия токенов");
}

void c_settings_panel::render_appearance(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();

    ImGui::Dummy(ImVec2(0.0f, 10.0f * unit));
    section_header(theme, "внешний вид");
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));

    for (int i = 0; i < palette_count; ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, 8.0f * unit);
        accent_t preset = accent_presets[i];
        ImVec4 color(preset.r, preset.g, preset.b, 1.0f);
        char id[32];
        std::snprintf(id, sizeof(id), "##accent_%d", i);
        ImVec2 swatch_min = ImGui::GetCursorScreenPos();
        ImVec2 swatch_size(44.0f * unit, 30.0f * unit);
        bool selected = app.config.accent_index == i;
        bool hovered = ImGui::IsMouseHoveringRect(swatch_min, ImVec2(swatch_min.x + swatch_size.x, swatch_min.y + swatch_size.y));
        if (ImGui::InvisibleButton(id, swatch_size))
        {
            app.config.accent_index = i;
            theme.accent_index = i;
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(swatch_min, ImVec2(swatch_min.x + swatch_size.x, swatch_min.y + swatch_size.y), ImGui::ColorConvertFloat4ToU32(color), 9.0f * unit);
        if (selected)
            draw->AddRect(ImVec2(swatch_min.x - 2.0f * unit, swatch_min.y - 2.0f * unit), ImVec2(swatch_min.x + swatch_size.x + 2.0f * unit, swatch_min.y + swatch_size.y + 2.0f * unit), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.8f)), 11.0f * unit, 0, 2.0f * unit);
        else if (hovered)
            draw->AddRect(swatch_min, ImVec2(swatch_min.x + swatch_size.x, swatch_min.y + swatch_size.y), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.3f)), 9.0f * unit);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * unit));
    if (toggle_switch(theme, "##animations", &app.config.animations))
    {
    }
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "анимации");

    float font_size = app.config.font_size;
    ImGui::SetNextItemWidth(-120.0f * unit);
    if (ImGui::SliderFloat("##font_size", &font_size, 13.0f, 24.0f, " "))
        app.config.font_size = font_size;
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "шрифт %.0fpx (применится после перезапуска)", font_size);
}

void c_settings_panel::render_tools(c_ide_app& app)
{
    c_theme& theme = app.theme;
    float unit = theme.scale();

    ImGui::Dummy(ImVec2(0.0f, 10.0f * unit));
    section_header(theme, "инструменты");
    ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));

    if (toggle_switch(theme, "##allow_commands", &app.config.allow_commands))
    {
    }
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "разрешить команды (cmd / powershell)");

    if (toggle_switch(theme, "##confirm_edits", &app.config.confirm_edits))
    {
    }
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "спрашивать перед правками файлов");

    if (toggle_switch(theme, "##skip_tls", &app.config.skip_tls_verify))
    {
    }
    ImGui::SameLine();
    ImGui::TextColored(app.theme.palette().text_dim, "не проверять tls сертификаты");
}

void c_settings_panel::draw(c_ide_app& app)
{
    if (!visible)
        return;
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 card_size(std::min(640.0f * unit, io.DisplaySize.x - 60.0f), std::min(680.0f * unit, io.DisplaySize.y - 90.0f));
    ImVec2 card_min((io.DisplaySize.x - card_size.x) * 0.5f, (io.DisplaySize.y - card_size.y) * 0.5f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float now = anim::time_now();
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.45f)));
    soft_shadow(draw, card_min, ImVec2(card_min.x + card_size.x, card_min.y + card_size.y), 18.0f * unit, 0.55f);

    ImGui::SetCursorScreenPos(card_min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f * unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * unit, 16.0f * unit));
    ImGui::BeginChild("##settings_card", card_size, ImGuiChildFlags_Borders);

    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextColored(colors.text, "настройки");
    ImGui::PopFont();
    ImGui::SameLine(card_size.x - 46.0f * unit);
    if (icon_button(theme, icon_xmark, "##settings_close", "закрыть (esc)"))
    {
        visible = false;
        app.config.save();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * unit));
    ImGui::BeginChild("##settings_scroll", ImVec2(0.0f, -30.0f * unit), ImGuiChildFlags_None);

    render_connection(app);
    render_context(app);
    render_appearance(app);
    render_tools(app);

    ImGui::Dummy(ImVec2(0.0f, 12.0f * unit));
    ImGui::TextColored(colors.text_faint, "amyrIDE — собран с любовью, работает через твой endpoint");
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        visible = false;
        app.config.save();
    }
}
