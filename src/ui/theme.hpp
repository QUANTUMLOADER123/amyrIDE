#pragma once

#include <imgui.h>

struct palette_t
{
    const char* name;
    ImVec4 accent;
    ImVec4 accent_dim;
    ImVec4 background;
    ImVec4 panel;
    ImVec4 elevated;
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_dim;
    ImVec4 text_faint;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
};

struct syntax_palette_t
{
    ImVec4 plain;
    ImVec4 keyword;
    ImVec4 control;
    ImVec4 type;
    ImVec4 string;
    ImVec4 number;
    ImVec4 comment;
    ImVec4 preprocessor;
    ImVec4 function;
    ImVec4 punctuation;
};

class c_theme
{
public:
    int accent_index = 0;
    float font_base = 17.0f;

    ImFont* font_main = nullptr;
    ImFont* font_bold = nullptr;
    ImFont* font_mono = nullptr;

    void load_fonts(float dpi_scale);
    void apply_style();
    void shutdown();

    const palette_t& palette() const;
    ImVec4 accent() const { return palette().accent; }
    ImU32 accent_u32(float alpha = 1.0f) const;
    ImVec4 with_alpha(const ImVec4& color, float alpha) const;

    float scale() const { return font_base / 17.0f; }
};

const syntax_palette_t& syntax_palette();

namespace anim
{
    float time_now();
    float approach(float current, float target, float rate, float dt);
    ImVec4 approach_color(const ImVec4& current, const ImVec4& target, float rate, float dt);
    float ease_out(float t);
    float ease_in_out(float t);
}
