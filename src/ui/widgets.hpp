#pragma once

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

#include "icons.hpp"

class c_theme;

struct toast_t
{
    std::string text;
    const char* icon;
    ImVec4 color;
    float born_at = 0.0f;
    float life_seconds = 4.0f;
    std::string action_label;
    std::function<void()> action;
};

class c_toasts
{
public:
    void push(const std::string& text, const char* icon, const ImVec4& color, float life_seconds = 4.0f);
    void push_action(const std::string& text, const char* icon, const ImVec4& color, const std::string& action_label, std::function<void()> action);
    void draw(c_theme& theme);
    void update(float dt);

private:
    std::vector<toast_t> entries;
};

void soft_shadow(ImDrawList* draw, ImVec2 min_corner, ImVec2 max_corner, float rounding, float alpha);
void glow_circle(ImDrawList* draw, ImVec2 center, float radius, const ImVec4& color);
void spinner(c_theme& theme, float radius, float thickness, float speed = 2.4f);
bool accent_button(c_theme& theme, const char* label, ImVec2 size = ImVec2(0, 0));
bool ghost_button(c_theme& theme, const char* label, ImVec2 size = ImVec2(0, 0));
bool icon_button(c_theme& theme, const char* icon, const char* id, const char* tooltip, float size = 0.0f, const ImVec4& tint = ImVec4(0, 0, 0, 0));
bool toggle_switch(c_theme& theme, const char* id, bool* value);
bool chip(c_theme& theme, const char* label, bool selected);
bool search_input(c_theme& theme, const char* id, char* buffer, size_t buffer_size, const char* hint, float width = 0.0f);
void token_meter(c_theme& theme, long long used, long long limit, float width);
void status_dot(c_theme& theme, ImVec2 center, const ImVec4& color, bool pulse);
void section_header(c_theme& theme, const char* text);
void animated_underline(c_theme& theme, ImVec2 begin, ImVec2 end, float height);
bool begin_card(c_theme& theme, const char* id, ImVec2 size = ImVec2(0, 0));
void end_card();
void styled_tooltip(c_theme& theme, const char* text);
