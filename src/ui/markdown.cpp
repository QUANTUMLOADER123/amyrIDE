#include "markdown.hpp"

#include <cctype>
#include <cstdio>

#include "app.hpp"
#include "core/string.hpp"
#include "core/utf8.hpp"
#include "highlight.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
    struct inline_run_t
    {
        std::string text;
        bool bold = false;
        bool code = false;
        bool heading_run = false;
        int heading_level = 0;
    };

    bool is_path_like(std::string_view token)
    {
        if (token.size() < 3 || token.size() > 120)
            return false;
        bool has_separator = token.find('/') != std::string_view::npos || token.find('\\') != std::string_view::npos;
        bool has_dot = token.find('.') != std::string_view::npos;
        return has_separator && has_dot;
    }

    std::string extract_path(const std::string& info)
    {
        for (const std::string& token : str::split(str::trim(info), ' '))
        {
            if (is_path_like(token))
                return token;
        }
        return "";
    }

    std::vector<inline_run_t> parse_inline(const std::string& text, bool heading, int level)
    {
        std::vector<inline_run_t> runs;
        std::string plain;
        bool bold = false;
        bool code = false;

        auto emit = [&](const std::string& piece, bool is_bold, bool is_code)
        {
            if (piece.empty())
                return;
            inline_run_t run;
            run.text = piece;
            run.bold = is_bold;
            run.code = is_code;
            run.heading_run = heading;
            run.heading_level = level;
            runs.push_back(std::move(run));
        };

        size_t position = 0;
        while (position < text.size())
        {
            if (text.compare(position, 2, "**") == 0)
            {
                emit(std::move(plain), bold, code);
                plain.clear();
                bold = !bold;
                position += 2;
                continue;
            }
            if (text[position] == '`')
            {
                emit(std::move(plain), bold, code);
                plain.clear();
                code = !code;
                position += 1;
                continue;
            }
            plain.push_back(text[position]);
            ++position;
        }
        emit(std::move(plain), bold, code);
        return runs;
    }

    void render_runs(c_theme& theme, const std::vector<inline_run_t>& runs, float wrap_width, const ImVec4& base_color)
    {
        const palette_t& colors = theme.palette();
        float unit = theme.scale();
        if (runs.empty())
            return;

        float wrap_right = ImGui::GetCursorScreenPos().x + wrap_width;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushTextWrapPos(wrap_right);

        for (size_t i = 0; i < runs.size(); ++i)
        {
            const inline_run_t& run = runs[i];
            float font_size = ImGui::GetStyle().FontSizeBase * (run.heading_run ? 1.0f + 0.16f * static_cast<float>(run.heading_level) : 1.0f);
            ImFont* font = run.code ? theme.font_mono : run.bold || run.heading_run ? theme.font_bold : theme.font_main;
            ImVec4 color = base_color;
            if (run.code)
                color = ImVec4(colors.accent.x * 0.40f + colors.text.x * 0.60f, colors.accent.y * 0.40f + colors.text.y * 0.60f, colors.accent.z * 0.40f + colors.text.z * 0.60f, 1.0f);

            size_t scan = 0;
            while (scan <= run.text.size())
            {
                size_t newline = run.text.find('\n', scan);
                size_t end = newline == std::string::npos ? run.text.size() : newline;
                std::string fragment(run.text.data() + scan, end - scan);
                bool ends_line = newline != std::string::npos;
                scan = ends_line ? newline + 1 : run.text.size() + 1;

                if (fragment.empty())
                    continue;

                ImGui::PushFont(font, font_size);
                std::string flow = fragment;
                if (ImGui::CalcTextSize(flow.c_str()).x > wrap_width)
                {
                    std::string wrapped;
                    float used = 0.0f;
                    size_t walk = 0;
                    while (walk < flow.size())
                    {
                        size_t next = utf8::next_position(flow, walk);
                        std::string symbol = flow.substr(walk, next - walk);
                        float width = ImGui::CalcTextSize(symbol.c_str()).x;
                        if (used + width > wrap_width)
                        {
                            wrapped += '\n';
                            used = 0.0f;
                        }
                        wrapped += symbol;
                        used += width;
                        walk = next;
                    }
                    flow = wrapped;
                }
                ImVec2 piece_size = ImGui::CalcTextSize(flow.c_str());
                bool draw_pill = run.code && flow.find('\n') == std::string::npos && piece_size.x + 10.0f * unit <= wrap_width;
                if (draw_pill)
                {
                    ImVec2 pill_min = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pill_min.x - 5.0f * unit, pill_min.y - 1.0f), ImVec2(pill_min.x + piece_size.x + 5.0f * unit, pill_min.y + piece_size.y + 1.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.14f)), 6.0f * unit);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(flow.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();

                bool last = i + 1 >= runs.size();
                if (!last && !ends_line)
                    ImGui::SameLine(0.0f, 0.0f);
            }
        }

        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 3.0f * unit));
    }

    void render_code_block(c_ide_app& app, const md_block_t& block, int block_index, float wrap_width)
    {
        c_theme& theme = app.theme;
        const palette_t& colors = theme.palette();
        float unit = theme.scale();

        highlight_lang lang = highlight_lang_from_name(block.lang);
        std::vector<std::string> code_lines = block.text.empty() ? std::vector<std::string>{} : str::split(block.text, '\n');
        float line_height = ImGui::GetTextLineHeight();
        float body_height = line_height * static_cast<float>(code_lines.size()) + 10.0f * unit;
        float max_height = 360.0f * unit;
        float view_height = body_height > max_height ? max_height : body_height;
        float header_height = 30.0f * unit;
        float block_width = wrap_width;

        char id_block[32];
        std::snprintf(id_block, sizeof(id_block), "##codeblock%d", block_index);
        char id_body[32];
        std::snprintf(id_body, sizeof(id_body), "##codebody%d", block_index);
        char id_copy[32];
        std::snprintf(id_copy, sizeof(id_copy), "##copy%d", block_index);
        char id_apply[32];
        std::snprintf(id_apply, sizeof(id_apply), "##apply%d", block_index);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.background);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * unit);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild(id_block, ImVec2(block_width, view_height + header_height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 header_max(origin.x + block_width, origin.y + header_height);
        float block_round = 10.0f * unit;
        ImVec4 header_fill(colors.elevated.x, colors.elevated.y, colors.elevated.z, 1.0f);
        draw->AddRectFilled(origin, header_max, ImGui::ColorConvertFloat4ToU32(header_fill), block_round);
        draw->AddRectFilled(ImVec2(origin.x, origin.y + header_height * 0.5f), header_max, ImGui::ColorConvertFloat4ToU32(header_fill), 0.0f);
        draw->AddLine(ImVec2(origin.x, header_max.y), ImVec2(header_max.x, header_max.y), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 1.0f)));

        std::string target_path = extract_path(block.lang);
        ImGui::SetCursorPos(ImVec2(12.0f * unit, 6.0f * unit));
        ImGui::PushFont(theme.font_bold, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(colors.text_faint, "%s", highlight_lang_label(lang));
        ImGui::PopFont();
        if (!target_path.empty())
        {
            ImGui::SameLine(0.0f, 8.0f * unit);
            ImGui::TextColored(colors.text_faint, "\xe2\x80\xa2 %s", target_path.c_str());
        }

        ImGui::SetCursorPos(ImVec2(block_width - 64.0f * unit, 4.0f * unit));
        if (icon_button(theme, icon_copy, id_copy, "copy"))
            ImGui::SetClipboardText(block.text.c_str());
        ImGui::SameLine(0.0f, 2.0f * unit);
        if (icon_button(theme, icon_pen_to_square, id_apply, target_path.empty() ? "insert at cursor" : "write to file"))
            app.apply_code_block(target_path, block.text);

        ImGui::SetCursorPos(ImVec2(0.0f, header_height));
        ImGui::BeginChild(id_body, ImVec2(block_width, view_height), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::PushFont(theme.font_mono, ImGui::GetStyle().FontSizeBase * 0.92f);
        float char_width = ImGui::CalcTextSize(" ").x;
        float scroll_x = ImGui::GetScrollX();
        float body_left = 14.0f * unit;
        int state = 0;
        for (const std::string& code_line : code_lines)
        {
            std::vector<text_segment_t> segments = highlight_line(code_line, lang, state, &state);
            ImVec2 line_pos = ImGui::GetCursorScreenPos();
            float base_x = line_pos.x - scroll_x + body_left;
            for (const text_segment_t& segment : segments)
            {
                std::string piece = code_line.substr(static_cast<size_t>(segment.begin), static_cast<size_t>(segment.end - segment.begin));
                float piece_offset = static_cast<float>(utf8::column_count(std::string_view(code_line).substr(0, static_cast<size_t>(segment.begin)))) * char_width;
                ImGui::GetWindowDrawList()->AddText(ImVec2(base_x + piece_offset, line_pos.y), ImGui::ColorConvertFloat4ToU32(segment.color), piece.c_str());
            }
            ImGui::Dummy(ImVec2(body_left + static_cast<float>(utf8::column_count(code_line)) * char_width + 24.0f * unit, line_height));
        }
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0.0f, 6.0f * unit));
    }
}

std::vector<md_block_t> parse_markdown(const std::string& text)
{
    std::vector<md_block_t> blocks;
    std::vector<std::string> lines = str::split(text, '\n');

    size_t index = 0;
    while (index < lines.size())
    {
        std::string line = lines[index];
        std::string trimmed = str::trim(line);

        if (trimmed.starts_with("```"))
        {
            md_block_t block;
            block.kind = md_kind::code;
            block.lang = str::trim(trimmed.substr(3));
            ++index;
            while (index < lines.size() && !str::trim(lines[index]).starts_with("```"))
            {
                block.text += lines[index];
                if (index + 1 < lines.size())
                    block.text += "\n";
                ++index;
            }
            ++index;
            blocks.push_back(std::move(block));
            continue;
        }

        if (trimmed.empty())
        {
            ++index;
            continue;
        }

        if (trimmed.starts_with("#"))
        {
            int level = 0;
            while (level < static_cast<int>(trimmed.size()) && trimmed[static_cast<size_t>(level)] == '#')
                ++level;
            md_block_t block;
            block.kind = md_kind::heading;
            block.level = level;
            block.text = str::trim(trimmed.substr(static_cast<size_t>(level)));
            blocks.push_back(std::move(block));
            ++index;
            continue;
        }

        if (trimmed == "---" || trimmed == "***" || trimmed == "___")
        {
            md_block_t block;
            block.kind = md_kind::rule;
            blocks.push_back(std::move(block));
            ++index;
            continue;
        }

        bool bulleted = trimmed.starts_with("- ") || trimmed.starts_with("* ") || trimmed.starts_with("+ ");
        bool numbered = trimmed.size() > 2 && isdigit(static_cast<unsigned char>(trimmed[0])) && trimmed[1] == '.' && trimmed[2] == ' ';
        if (bulleted || numbered)
        {
            md_block_t block;
            block.kind = md_kind::list;
            while (index < lines.size())
            {
                std::string entry = str::trim(lines[index]);
                if (entry.starts_with("- ") || entry.starts_with("* ") || entry.starts_with("+ "))
                    block.items.push_back(entry.substr(2));
                else if (entry.size() > 3 && isdigit(static_cast<unsigned char>(entry[0])) && entry[1] == '.' && entry[2] == ' ')
                    block.items.push_back(entry.substr(3));
                else if (!entry.empty() && (lines[index].starts_with("  ") || lines[index].starts_with("\t")) && !block.items.empty())
                    block.items.back() += " " + entry;
                else
                    break;
                ++index;
            }
            blocks.push_back(std::move(block));
            continue;
        }

        if (trimmed.starts_with(">"))
        {
            md_block_t block;
            block.kind = md_kind::quote;
            std::string body;
            while (index < lines.size() && str::trim(lines[index]).starts_with(">"))
            {
                std::string quoted = str::trim(lines[index]);
                body += quoted.starts_with("> ") ? quoted.substr(2) : quoted.substr(1);
                body += "\n";
                ++index;
            }
            block.text = std::move(body);
            blocks.push_back(std::move(block));
            continue;
        }

        std::string paragraph;
        while (index < lines.size())
        {
            std::string candidate = str::trim(lines[index]);
            if (candidate.empty() || candidate.starts_with("```") || candidate.starts_with("#") || candidate.starts_with("- ") || candidate.starts_with("> ") || candidate == "---")
                break;
            paragraph += candidate;
            if (index + 1 < lines.size())
                paragraph += "\n";
            ++index;
        }
        md_block_t block;
        block.kind = md_kind::paragraph;
        block.text = std::move(paragraph);
        blocks.push_back(std::move(block));
    }
    return blocks;
}

void render_markdown(c_ide_app& app, const std::vector<md_block_t>& blocks, float wrap_width)
{
    c_theme& theme = app.theme;
    const palette_t& colors = theme.palette();

    int block_index = 0;
    for (const md_block_t& block : blocks)
    {
        switch (block.kind)
        {
        case md_kind::heading:
        {
            std::vector<inline_run_t> runs = parse_inline(block.text, true, block.level);
            render_runs(theme, runs, wrap_width, colors.text);
            break;
        }
        case md_kind::paragraph:
        {
            std::vector<inline_run_t> runs = parse_inline(block.text, false, 0);
            render_runs(theme, runs, wrap_width, colors.text);
            break;
        }
        case md_kind::quote:
        {
            float unit = theme.scale();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::Indent(14.0f * unit);
            std::vector<inline_run_t> runs = parse_inline(block.text, false, 0);
            render_runs(theme, runs, wrap_width - 14.0f * unit, colors.text_dim);
            ImGui::Unindent(14.0f * unit);
            float used_height = ImGui::GetCursorScreenPos().y - pos.y;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f * unit), ImVec2(pos.x + 3.0f * unit, pos.y + used_height - 10.0f * unit), theme.accent_u32(0.65f), 2.0f * unit);
            break;
        }
        case md_kind::list:
        {
            float unit = theme.scale();
            for (const std::string& item : block.items)
            {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + 4.0f * unit, pos.y + ImGui::GetTextLineHeight() * 0.55f), 2.6f * unit, theme.accent_u32(0.9f));
                ImGui::Indent(14.0f * unit);
                std::vector<inline_run_t> runs = parse_inline(item, false, 0);
                render_runs(theme, runs, wrap_width - 14.0f * unit, colors.text);
                ImGui::Unindent(14.0f * unit);
            }
            break;
        }
        case md_kind::rule:
        {
            float unit = theme.scale();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + wrap_width, pos.y + 1.5f * unit), ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border.x, colors.border.y, colors.border.z, 0.9f)), 1.0f * unit);
            ImGui::Dummy(ImVec2(wrap_width, 8.0f * unit));
            break;
        }
        case md_kind::code:
        {
            render_code_block(app, block, block_index, wrap_width);
            break;
        }
        }
        ++block_index;
    }
}
