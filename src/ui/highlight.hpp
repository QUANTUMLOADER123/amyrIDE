#pragma once

#include <imgui.h>

#include <string>
#include <vector>

enum class highlight_lang
{
    none,
    cpp,
    python,
    javascript,
    rust,
    csharp,
    json,
    generic
};

struct text_segment_t
{
    int begin = 0;
    int end = 0;
    ImVec4 color;
};

highlight_lang highlight_lang_from_extension(const std::string& extension);
highlight_lang highlight_lang_from_name(const std::string& name);
const char* highlight_lang_label(highlight_lang lang);
std::vector<text_segment_t> highlight_line(const std::string& line, highlight_lang lang, int state_in, int* state_out);
