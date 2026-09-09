#pragma once

#include <string>
#include <vector>

enum class md_kind
{
    paragraph,
    heading,
    code,
    list,
    quote,
    rule
};

struct md_block_t
{
    md_kind kind = md_kind::paragraph;
    std::string text;
    std::string lang;
    std::vector<std::string> items;
    int level = 0;
};

std::vector<md_block_t> parse_markdown(const std::string& text);

class c_theme;
class c_ide_app;

void render_markdown(c_ide_app& app, const std::vector<md_block_t>& blocks, float wrap_width);
