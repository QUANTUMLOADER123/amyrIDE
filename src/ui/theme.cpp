#include "theme.hpp"

#include <cmath>
#include <string>

#include "core/config.hpp"
#include "icons.hpp"
#include "platform/shell.hpp"

namespace
{
    palette_t palettes[6] = {
        { "Sky", { 0.612f, 0.780f, 0.961f, 1.0f }, { 0.369f, 0.533f, 0.769f, 1.0f }, { 0.055f, 0.075f, 0.110f, 1.0f }, { 0.075f, 0.102f, 0.149f, 1.0f }, { 0.102f, 0.137f, 0.196f, 1.0f }, { 0.153f, 0.200f, 0.286f, 1.0f }, { 0.910f, 0.933f, 0.973f, 1.0f }, { 0.604f, 0.659f, 0.753f, 1.0f }, { 0.420f, 0.471f, 0.565f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } },
        { "Periwinkle", { 0.647f, 0.690f, 0.961f, 1.0f }, { 0.408f, 0.451f, 0.769f, 1.0f }, { 0.060f, 0.065f, 0.115f, 1.0f }, { 0.082f, 0.088f, 0.155f, 1.0f }, { 0.110f, 0.118f, 0.204f, 1.0f }, { 0.161f, 0.173f, 0.294f, 1.0f }, { 0.918f, 0.922f, 0.973f, 1.0f }, { 0.616f, 0.635f, 0.761f, 1.0f }, { 0.431f, 0.447f, 0.573f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } },
        { "Aqua", { 0.561f, 0.847f, 0.910f, 1.0f }, { 0.329f, 0.600f, 0.702f, 1.0f }, { 0.047f, 0.078f, 0.094f, 1.0f }, { 0.067f, 0.106f, 0.129f, 1.0f }, { 0.094f, 0.141f, 0.169f, 1.0f }, { 0.141f, 0.204f, 0.247f, 1.0f }, { 0.898f, 0.941f, 0.953f, 1.0f }, { 0.588f, 0.678f, 0.722f, 1.0f }, { 0.408f, 0.494f, 0.541f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } },
        { "Lavender", { 0.753f, 0.659f, 0.941f, 1.0f }, { 0.510f, 0.420f, 0.753f, 1.0f }, { 0.071f, 0.059f, 0.110f, 1.0f }, { 0.094f, 0.082f, 0.149f, 1.0f }, { 0.125f, 0.110f, 0.196f, 1.0f }, { 0.180f, 0.161f, 0.278f, 1.0f }, { 0.933f, 0.925f, 0.973f, 1.0f }, { 0.647f, 0.624f, 0.761f, 1.0f }, { 0.455f, 0.439f, 0.573f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } },
        { "Mist", { 0.722f, 0.804f, 0.898f, 1.0f }, { 0.459f, 0.557f, 0.667f, 1.0f }, { 0.078f, 0.086f, 0.102f, 1.0f }, { 0.102f, 0.114f, 0.137f, 1.0f }, { 0.137f, 0.153f, 0.180f, 1.0f }, { 0.196f, 0.216f, 0.255f, 1.0f }, { 0.925f, 0.937f, 0.953f, 1.0f }, { 0.651f, 0.694f, 0.745f, 1.0f }, { 0.478f, 0.518f, 0.573f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } },
        { "Powder", { 0.659f, 0.804f, 0.925f, 1.0f }, { 0.420f, 0.557f, 0.702f, 1.0f }, { 0.067f, 0.086f, 0.106f, 1.0f }, { 0.090f, 0.114f, 0.141f, 1.0f }, { 0.122f, 0.153f, 0.188f, 1.0f }, { 0.180f, 0.224f, 0.275f, 1.0f }, { 0.914f, 0.937f, 0.961f, 1.0f }, { 0.616f, 0.678f, 0.745f, 1.0f }, { 0.435f, 0.490f, 0.557f, 1.0f }, { 0.576f, 0.839f, 0.686f, 1.0f }, { 0.910f, 0.804f, 0.612f, 1.0f }, { 0.910f, 0.604f, 0.624f, 1.0f } }
    };

    syntax_palette_t syntax_colors = {
        { 0.855f, 0.890f, 0.949f, 1.0f },
        { 0.741f, 0.635f, 1.000f, 1.0f },
        { 0.973f, 0.612f, 0.702f, 1.0f },
        { 0.537f, 0.816f, 1.000f, 1.0f },
        { 0.678f, 0.898f, 0.573f, 1.0f },
        { 0.961f, 0.745f, 0.510f, 1.0f },
        { 0.435f, 0.467f, 0.545f, 1.0f },
        { 0.973f, 0.651f, 0.847f, 1.0f },
        { 0.573f, 0.784f, 1.000f, 1.0f },
        { 0.588f, 0.639f, 0.722f, 1.0f }
    };

    constexpr float default_font_size = 17.0f;

    const ImWchar text_glyph_ranges[] = {
        0x0020, 0x00FF,
        0x0400, 0x04FF,
        0x2010, 0x205E,
        0x20AC, 0x20AC,
        0x2116, 0x2116,
        0x20BD, 0x20BD,
        0x2190, 0x21BB,
        0
    };
}

void c_theme::load_fonts(float dpi_scale, const char* font_directory)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;

    font_base = default_font_size * dpi_scale;

    std::string regular_path = std::string(font_directory) + "Inter-Regular.ttf";
    std::string semibold_path = std::string(font_directory) + "Inter-SemiBold.ttf";
    std::string mono_path = std::string(font_directory) + "JetBrainsMono-Regular.ttf";
    std::string mono_bold_path = std::string(font_directory) + "JetBrainsMono-SemiBold.ttf";
    std::string icons_path = std::string(font_directory) + "fa-solid-900.ttf";

    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = false;
    icons_config.GlyphMinAdvanceX = font_base * 0.92f;

    font_main = atlas->AddFontFromFileTTF(regular_path.c_str(), font_base, nullptr, text_glyph_ranges);
    if (font_main)
        atlas->AddFontFromFileTTF(icons_path.c_str(), font_base * 0.80f, &icons_config, icon_glyphs);
    else
        font_main = atlas->AddFontFromFileTTF((std::string(font_directory) + "JetBrainsMono-Regular.ttf").c_str(), font_base, nullptr, text_glyph_ranges);
    if (!font_main)
        font_main = atlas->AddFontDefault();
    if (font_main && shell::path_exists(icons_path))
        atlas->AddFontFromFileTTF(icons_path.c_str(), font_base * 0.80f, &icons_config, icon_glyphs);

    font_bold = atlas->AddFontFromFileTTF(semibold_path.c_str(), font_base, nullptr, text_glyph_ranges);
    if (!font_bold)
        font_bold = font_main;

    font_mono = atlas->AddFontFromFileTTF(mono_path.c_str(), font_base * 0.92f, nullptr, text_glyph_ranges);
    if (!font_mono)
        font_mono = atlas->AddFontFromFileTTF(mono_bold_path.c_str(), font_base * 0.92f, nullptr, text_glyph_ranges);
    if (!font_mono)
        font_mono = font_main;
}

void c_theme::apply_style()
{
    ImGuiStyle& style = ImGui::GetStyle();
    const palette_t& colors = palette();
    float unit = scale();

    style.FontSizeBase = font_base;
    style.WindowRounding = 12.0f * unit;
    style.ChildRounding = 12.0f * unit;
    style.PopupRounding = 12.0f * unit;
    style.FrameRounding = 9.0f * unit;
    style.GrabRounding = 8.0f * unit;
    style.TabRounding = 8.0f * unit;
    style.ScrollbarRounding = 8.0f * unit;
    style.PopupBorderSize = 1.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12.0f * unit, 10.0f * unit);
    style.FramePadding = ImVec2(10.0f * unit, 7.0f * unit);
    style.ItemSpacing = ImVec2(9.0f * unit, 8.0f * unit);
    style.ItemInnerSpacing = ImVec2(7.0f * unit, 5.0f * unit);
    style.CellPadding = ImVec2(8.0f * unit, 5.0f * unit);
    style.IndentSpacing = 20.0f * unit;
    style.ScrollbarSize = 12.0f * unit;
    style.GrabMinSize = 10.0f * unit;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 2.0f;
    style.DockingSeparatorSize = 2.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = colors.text;
    c[ImGuiCol_TextDisabled] = colors.text_dim;
    c[ImGuiCol_WindowBg] = colors.panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = colors.elevated;
    c[ImGuiCol_Border] = with_alpha(colors.border, 0.8f);
    c[ImGuiCol_FrameBg] = colors.background;
    c[ImGuiCol_FrameBgHovered] = with_alpha(colors.accent, 0.10f);
    c[ImGuiCol_FrameBgActive] = with_alpha(colors.accent, 0.18f);
    c[ImGuiCol_TitleBg] = colors.background;
    c[ImGuiCol_TitleBgActive] = colors.background;
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_MenuBarBg] = colors.background;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = with_alpha(colors.border, 0.9f);
    c[ImGuiCol_ScrollbarGrabHovered] = with_alpha(colors.accent, 0.45f);
    c[ImGuiCol_ScrollbarGrabActive] = colors.accent;
    c[ImGuiCol_CheckMark] = colors.accent;
    c[ImGuiCol_SliderGrab] = colors.accent;
    c[ImGuiCol_SliderGrabActive] = with_alpha(colors.accent, 0.85f);
    c[ImGuiCol_Button] = colors.elevated;
    c[ImGuiCol_ButtonHovered] = with_alpha(colors.accent, 0.28f);
    c[ImGuiCol_ButtonActive] = with_alpha(colors.accent, 0.45f);
    c[ImGuiCol_Header] = with_alpha(colors.accent, 0.18f);
    c[ImGuiCol_HeaderHovered] = with_alpha(colors.accent, 0.32f);
    c[ImGuiCol_HeaderActive] = with_alpha(colors.accent, 0.45f);
    c[ImGuiCol_Separator] = with_alpha(colors.border, 0.6f);
    c[ImGuiCol_SeparatorHovered] = with_alpha(colors.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = colors.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = with_alpha(colors.accent, 0.25f);
    c[ImGuiCol_ResizeGripActive] = with_alpha(colors.accent, 0.5f);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = with_alpha(colors.accent, 0.20f);
    c[ImGuiCol_TabSelected] = colors.elevated;
    c[ImGuiCol_TabSelectedOverline] = colors.accent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = colors.panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = colors.border;
    c[ImGuiCol_DockingPreview] = with_alpha(colors.accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = colors.background;
    c[ImGuiCol_PlotLines] = colors.accent;
    c[ImGuiCol_PlotHistogram] = colors.accent;
    c[ImGuiCol_TableHeaderBg] = colors.elevated;
    c[ImGuiCol_TableBorderStrong] = colors.border;
    c[ImGuiCol_TableBorderLight] = with_alpha(colors.border, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = with_alpha(colors.text, 0.02f);
    c[ImGuiCol_TextSelectedBg] = with_alpha(colors.accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = colors.accent;
    c[ImGuiCol_NavCursor] = with_alpha(colors.accent, 0.7f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

void c_theme::shutdown()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->ClearFonts();
}

const palette_t& c_theme::palette() const
{
    int index = accent_index >= 0 && accent_index < palette_count ? accent_index : 0;
    return palettes[index];
}

const syntax_palette_t& syntax_palette()
{
    return syntax_colors;
}

ImU32 c_theme::accent_u32(float alpha) const
{
    ImVec4 color = with_alpha(accent(), alpha);
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 c_theme::with_alpha(const ImVec4& color, float alpha) const
{
    return ImVec4(color.x, color.y, color.z, color.w * alpha);
}

float anim::time_now()
{
    return static_cast<float>(ImGui::GetTime());
}

float anim::approach(float current, float target, float rate, float dt)
{
    float blend = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * blend;
}

ImVec4 anim::approach_color(const ImVec4& current, const ImVec4& target, float rate, float dt)
{
    float blend = 1.0f - std::exp(-rate * dt);
    return ImVec4(
        current.x + (target.x - current.x) * blend,
        current.y + (target.y - current.y) * blend,
        current.z + (target.z - current.z) * blend,
        current.w + (target.w - current.w) * blend
    );
}

float anim::ease_out(float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

float anim::ease_in_out(float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
