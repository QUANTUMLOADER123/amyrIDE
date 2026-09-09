#include "widgets.hpp"

#include <cmath>

#include <imgui_internal.h>

#include "core/string.hpp"
#include "theme.hpp"

namespace
{
    constexpr float toast_width = 340.0f;
    constexpr float toast_margin = 18.0f;
    constexpr float toast_height = 46.0f;
    constexpr float toast_gap = 10.0f;
    constexpr float toast_slide_seconds = 0.28f;
}

void c_toasts::push(const std::string& text, const char* icon, const ImVec4& color, float life_seconds)
{
    toast_t toast;
    toast.text = text;
    toast.icon = icon;
    toast.color = color;
    toast.born_at = anim::time_now();
    toast.life_seconds = life_seconds;
    entries.push_back(std::move(toast));
}

void c_toasts::push_action(const std::string& text, const char* icon, const ImVec4& color, const std::string& action_label, std::function<void()> action)
{
    toast_t toast;
    toast.text = text;
    toast.icon = icon;
    toast.color = color;
    toast.born_at = anim::time_now();
    toast.life_seconds = 7.0f;
    toast.action_label = action_label;
    toast.action = std::move(action);
    entries.push_back(std::move(toast));
}

void c_toasts::update(float dt)
{
    float now = anim::time_now();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [now](const toast_t& toast) {
        return now - toast.born_at > toast.life_seconds;
    }), entries.end());
}

void c_toasts::draw(c_theme& theme)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float now = anim::time_now();

    for (size_t i = 0; i < entries.size(); ++i)
    {
        toast_t& toast = entries[entries.size() - 1 - i];
        float age = now - toast.born_at;
        float appear = anim::ease_out(age / toast_slide_seconds);
        float fade = age > toast.life_seconds - 0.4f ? (toast.life_seconds - age) / 0.4f : 1.0f;
        float alpha = appear * (fade < 0.0f ? 0.0f : fade);

        float width = toast_width * unit;
        float height = toast_height * unit;
        float x = io.DisplaySize.x - width - toast_margin * unit;
        float y = io.DisplaySize.y - (static_cast<float>(i) + 1.0f) * (height + toast_gap * unit) - 34.0f * unit;
        y += (1.0f - appear) * 26.0f * unit;
        ImVec2 min_corner(x, y);
        ImVec2 max_corner(x + width, y + height);

        soft_shadow(draw, min_corner, max_corner, 10.0f * unit, 0.45f * alpha);
        draw->AddRectFilled(min_corner, max_corner, ImGui::ColorConvertFloat4ToU32(ImVec4(colors.elevated.x, colors.elevated.y, colors.elevated.z, 0.98f * alpha)), 10.0f * unit);
        draw->AddRectFilled(min_corner, ImVec2(min_corner.x + 3.0f * unit, max_corner.y), ImGui::ColorConvertFloat4ToU32(ImVec4(toast.color.x, toast.color.y, toast.color.z, alpha)), 2.0f * unit);
        draw->AddRect(min_corner, max_corner, ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, alpha)), 10.0f * unit);

        ImVec2 icon_pos(min_corner.x + 16.0f * unit, (min_corner.y + max_corner.y) * 0.5f);
        draw->AddText(nullptr, 15.0f * unit, ImVec2(icon_pos.x, icon_pos.y - ImGui::CalcTextSize(toast.icon).y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(toast.color.x, toast.color.y, toast.color.z, alpha)), toast.icon);

        std::string clipped = toast.text;
        ImVec2 text_size = ImGui::CalcTextSize(clipped.c_str());
        float available = width - 92.0f * unit;
        while (text_size.x > available && clipped.size() > 4)
        {
            clipped.resize(clipped.size() - 2);
            text_size = ImGui::CalcTextSize((clipped + "...").c_str());
        }
        if (clipped.size() != toast.text.size())
            clipped += "...";
        draw->AddText(nullptr, 14.5f * unit, ImVec2(icon_pos.x + 28.0f * unit, (min_corner.y + max_corner.y) * 0.5f - text_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text.x, colors.text.y, colors.text.z, alpha)), clipped.c_str());

        if (!toast.action)
            continue;

        float button_width = ImGui::CalcTextSize(toast.action_label.c_str()).x + 22.0f * unit;
        ImVec2 action_min(max_corner.x - button_width - 10.0f * unit, (min_corner.y + max_corner.y) * 0.5f - 12.0f * unit);
        ImVec2 action_max(max_corner.x - 10.0f * unit, (min_corner.y + max_corner.y) * 0.5f + 12.0f * unit);
        bool hovered = ImGui::IsMouseHoveringRect(action_min, action_max) && alpha > 0.9f;
        draw->AddRectFilled(action_min, action_max, ImGui::ColorConvertFloat4ToU32(ImVec4(toast.color.x, toast.color.y, toast.color.z, (hovered ? 0.85f : 0.30f) * alpha)), 6.0f * unit);
        ImVec2 label_size = ImGui::CalcTextSize(toast.action_label.c_str());
        draw->AddText(nullptr, 13.5f * unit, ImVec2((action_min.x + action_max.x) * 0.5f - label_size.x * 0.5f, (action_min.y + action_max.y) * 0.5f - label_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text.x, colors.text.y, colors.text.z, alpha)), toast.action_label.c_str());
        if (hovered && ImGui::IsMouseClicked(0))
        {
            if (toast.action)
                toast.action();
            toast.life_seconds = -1.0f;
        }
    }
}

void soft_shadow(ImDrawList* draw, ImVec2 min_corner, ImVec2 max_corner, float rounding, float alpha)
{
    for (int layer = 4; layer >= 1; --layer)
    {
        float expand = static_cast<float>(layer) * 2.4f;
        ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, alpha / static_cast<float>(layer * 3)));
        draw->AddRectFilled(ImVec2(min_corner.x - expand, min_corner.y - expand * 0.5f), ImVec2(max_corner.x + expand, max_corner.y + expand), color, rounding + expand);
    }
}

void glow_circle(ImDrawList* draw, ImVec2 center, float radius, const ImVec4& color)
{
    for (int layer = 12; layer >= 1; --layer)
    {
        float factor = static_cast<float>(layer) / 12.0f;
        draw->AddCircleFilled(center, radius * (2.0f - factor * 1.1f), ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.035f * factor)));
    }
}

void spinner(c_theme& theme, float radius, float thickness, float speed)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 center = ImGui::GetCursorScreenPos();
    center.x += radius + thickness;
    center.y += ImGui::GetTextLineHeight() * 0.5f;
    float phase = std::fmod(anim::time_now() * speed, 1.0f);
    int segments = 40;
    float start_angle = phase * 6.2831f;
    float arc = 1.4f + 0.9f * std::sin(anim::time_now() * 3.1f);
    draw->PathClear();
    for (int i = 0; i <= segments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        if (t > arc)
            break;
        float angle = start_angle + t * 6.2831f;
        draw->PathLineTo(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius));
    }
    draw->PathStroke(theme.accent_u32(), 0, thickness);
}

bool accent_button(c_theme& theme, const char* label, ImVec2 size)
{
    const palette_t& colors = theme.palette();
    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    if (size.x <= 0.0f)
        size.x = ImGui::CalcTextSize(label).x + 34.0f * theme.scale();
    if (size.y <= 0.0f)
        size.y = ImGui::GetTextLineHeight() + 13.0f * theme.scale();
    ImVec2 max_corner(min_corner.x + size.x, min_corner.y + size.y);

    bool hovered = ImGui::IsMouseHoveringRect(min_corner, max_corner);
    float t = anim::time_now();

    float brightness = ImGui::IsMouseDown(0) && hovered ? 0.85f : hovered ? 1.14f : 1.0f;
    ImU32 top = ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::fmin(1.0f, colors.accent.x * 1.16f * brightness),
        std::fmin(1.0f, colors.accent.y * 1.12f * brightness),
        std::fmin(1.0f, colors.accent.z * 1.16f * brightness), 1.0f));
    ImU32 bottom = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.accent_dim.x * brightness, colors.accent_dim.y * brightness, colors.accent_dim.z * brightness, 1.0f));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered)
    {
        float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
        soft_shadow(draw, min_corner, max_corner, 8.0f * theme.scale(), 0.30f + 0.12f * pulse);
    }
    float rounding = 8.0f * theme.scale();
    draw->PushClipRect(min_corner, max_corner, true);
    draw->AddRectFilled(min_corner, max_corner, bottom, rounding);
    draw->AddRectFilled(min_corner, ImVec2(max_corner.x, (min_corner.y + max_corner.y) * 0.5f + 1.0f), top, rounding);
    draw->PopClipRect();
    draw->AddRect(min_corner, max_corner, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, hovered ? 0.22f : 0.09f)), rounding);

    ImVec2 text_size = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2((min_corner.x + max_corner.x) * 0.5f - text_size.x * 0.5f, (min_corner.y + max_corner.y) * 0.5f - text_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.96f)), label);

    ImGui::InvisibleButton(label, size);
    ImGui::SetItemTooltip("%s", label);
    return ImGui::IsItemClicked();
}

bool ghost_button(c_theme& theme, const char* label, ImVec2 size)
{
    const palette_t& colors = theme.palette();
    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    if (size.x <= 0.0f)
        size.x = ImGui::CalcTextSize(label).x + 26.0f * theme.scale();
    if (size.y <= 0.0f)
        size.y = ImGui::GetTextLineHeight() + 11.0f * theme.scale();
    ImVec2 max_corner(min_corner.x + size.x, min_corner.y + size.y);

    bool hovered = ImGui::IsMouseHoveringRect(min_corner, max_corner);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min_corner, max_corner, theme.accent_u32(hovered ? 0.22f : 0.10f), 8.0f * theme.scale());
    draw->AddRect(min_corner, max_corner, theme.accent_u32(hovered ? 0.55f : 0.30f), 8.0f * theme.scale());

    ImVec2 text_size = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2((min_corner.x + max_corner.x) * 0.5f - text_size.x * 0.5f, (min_corner.y + max_corner.y) * 0.5f - text_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(hovered ? colors.text : colors.text_dim), label);

    ImGui::InvisibleButton(label, size);
    ImGui::SetItemTooltip("%s", label);
    return ImGui::IsItemClicked();
}

bool icon_button(c_theme& theme, const char* icon, const char* id, const char* tooltip, float size, const ImVec4& tint)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    if (size <= 0.0f)
        size = ImGui::GetTextLineHeight() + 10.0f * unit;

    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    ImVec2 max_corner(min_corner.x + size, min_corner.y + size);
    bool hovered = ImGui::IsMouseHoveringRect(min_corner, max_corner);
    bool pressed = hovered && ImGui::IsMouseDown(0);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float fill = pressed ? 0.30f : hovered ? 0.18f : 0.0f;
    draw->AddRectFilled(min_corner, max_corner, theme.accent_u32(fill), 7.0f * unit);

    ImVec4 icon_color = tint.w > 0.0f ? tint : hovered ? colors.text : colors.text_dim;
    ImVec2 icon_size = ImGui::CalcTextSize(icon);
    draw->AddText(nullptr, 14.5f * unit, ImVec2((min_corner.x + max_corner.x) * 0.5f - icon_size.x * 0.5f, (min_corner.y + max_corner.y) * 0.5f - icon_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(icon_color), icon);

    ImGui::InvisibleButton(id, ImVec2(size, size));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && tooltip && tooltip[0] != '#')
        ImGui::SetTooltip("%s", tooltip);
    return hovered && ImGui::IsMouseClicked(0);
}

bool toggle_switch(c_theme& theme, const char* id, bool* value)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float width = 40.0f * unit;
    float height = 22.0f * unit;

    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    ImVec2 max_corner(min_corner.x + width, min_corner.y + height);
    bool hovered = ImGui::IsMouseHoveringRect(min_corner, max_corner);
    if (hovered && ImGui::IsMouseClicked(0))
        *value = !*value;

    float target = *value ? 1.0f : 0.0f;
    float* anim_state = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(id), 0.0f);
    *anim_state = anim::approach(*anim_state, target, 16.0f, ImGui::GetIO().DeltaTime);
    float t = *anim_state;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 track = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.border.x + (colors.accent.x - colors.border.x) * t,
        colors.border.y + (colors.accent.y - colors.border.y) * t,
        colors.border.z + (colors.accent.z - colors.border.z) * t, 1.0f));
    draw->AddRectFilled(min_corner, max_corner, track, height * 0.5f);
    float knob_x = min_corner.x + height * 0.5f + t * (width - height);
    draw->AddCircleFilled(ImVec2(knob_x, (min_corner.y + max_corner.y) * 0.5f), height * 0.5f - 3.0f * unit, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.95f)));
    if (hovered)
        draw->AddRect(min_corner, max_corner, theme.accent_u32(0.6f), height * 0.5f);

    ImGui::ItemSize(ImVec2(width, height));
    ImGui::ItemAdd(ImRect(min_corner, max_corner), ImGui::GetID(id));
    return hovered;
}

bool chip(c_theme& theme, const char* label, bool selected)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    ImVec2 size(text_size.x + 22.0f * unit, text_size.y + 10.0f * unit);
    ImVec2 max_corner(min_corner.x + size.x, min_corner.y + size.y);

    bool hovered = ImGui::IsMouseHoveringRect(min_corner, max_corner);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 fill = selected ? theme.accent_u32(0.26f) : theme.accent_u32(hovered ? 0.14f : 0.06f);
    draw->AddRectFilled(min_corner, max_corner, fill, size.y * 0.5f);
    draw->AddRect(min_corner, max_corner, theme.accent_u32(selected ? 0.75f : hovered ? 0.40f : 0.22f), size.y * 0.5f);
    draw->AddText(ImVec2(min_corner.x + 11.0f * unit, min_corner.y + 5.0f * unit), ImGui::ColorConvertFloat4ToU32(selected ? colors.text : colors.text_dim), label);

    ImGui::InvisibleButton(label, size);
    return hovered && ImGui::IsMouseClicked(0);
}

bool search_input(c_theme& theme, const char* id, char* buffer, size_t buffer_size, const char* hint, float width)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    if (width <= 0.0f)
        width = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, colors.background);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30.0f * unit, 7.0f * unit));
    bool changed = ImGui::InputTextWithHint(id, hint, buffer, buffer_size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImVec2 frame_min = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(frame_min.x + 11.0f * unit, (frame_min.y + ImGui::GetItemRectMax().y) * 0.5f - ImGui::CalcTextSize(icon_magnifying_glass).y * 0.5f), ImGui::ColorConvertFloat4ToU32(colors.text_faint), icon_magnifying_glass);
    return changed;
}

void token_meter(c_theme& theme, long long used, long long limit, float width)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    float fraction = limit > 0 ? static_cast<float>(static_cast<double>(used) / static_cast<double>(limit)) : 0.0f;
    fraction = fraction < 0.0f ? 0.0f : fraction > 1.0f ? 1.0f : fraction;

    ImVec4 fill_color = fraction > 0.92f ? colors.danger : fraction > 0.75f ? colors.warning : colors.accent;
    float height = 5.0f * unit;
    ImVec2 min_corner = ImGui::GetCursorScreenPos();
    ImVec2 max_corner(min_corner.x + width, min_corner.y + height);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min_corner, max_corner, ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.55f)), height * 0.5f);
    draw->AddRectFilled(min_corner, ImVec2(min_corner.x + width * fraction, max_corner.y), ImGui::ColorConvertFloat4ToU32(fill_color), height * 0.5f);

    ImGui::ItemSize(ImVec2(width, height));
    ImGui::ItemAdd(ImRect(min_corner, max_corner), ImGui::GetID("##token_meter"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("context: %s of %s tokens (%.0f%%)", str::format_count(used).c_str(), str::format_count(limit).c_str(), fraction * 100.0f);
}

void status_dot(c_theme& theme, ImVec2 center, const ImVec4& color, bool pulse)
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float radius = 4.0f * theme.scale();
    float glow = 1.0f;
    if (pulse)
        glow = 0.7f + 0.3f * std::sin(anim::time_now() * 5.0f);
    draw->AddCircleFilled(center, radius * 2.2f * glow, ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.16f * glow)));
    draw->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(color));
}

void section_header(c_theme& theme, const char* text)
{
    const palette_t& colors = theme.palette();
    float unit = theme.scale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(pos.x, pos.y + 3.0f * unit), ImVec2(pos.x + 3.0f * unit, pos.y + ImGui::GetTextLineHeight() - 3.0f * unit), theme.accent_u32(0.9f), 2.0f * unit);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 11.0f * unit, pos.y));
    ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.92f);
    ImGui::TextColored(colors.text_dim, "%s", text);
    ImGui::PopFont();
}

void animated_underline(c_theme& theme, ImVec2 begin, ImVec2 end, float height)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(begin, ImVec2(end.x, end.y + height), theme.accent_u32(), height * 0.5f);
}

bool begin_card(c_theme& theme, const char* id, ImVec2 size)
{
    const palette_t& colors = theme.palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.elevated);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * theme.scale());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * theme.scale(), 10.0f * theme.scale()));
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return open;
}

void end_card()
{
    ImGui::EndChild();
}

void styled_tooltip(c_theme& theme, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.palette().elevated);
    ImGui::SetTooltip("%s", text);
    ImGui::PopStyleColor();
}
