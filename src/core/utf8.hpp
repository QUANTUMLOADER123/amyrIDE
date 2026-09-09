#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace utf8
{
    inline int char_size(unsigned char lead)
    {
        if (lead < 0x80)
            return 1;
        if ((lead & 0xe0) == 0xc0)
            return 2;
        if ((lead & 0xf0) == 0xe0)
            return 3;
        if ((lead & 0xf8) == 0xf0)
            return 4;
        return 1;
    }

    inline int char_size_at(std::string_view text, size_t position)
    {
        if (position >= text.size())
            return 0;
        return char_size(static_cast<unsigned char>(text[position]));
    }

    inline size_t next_position(std::string_view text, size_t position)
    {
        if (position >= text.size())
            return text.size();
        size_t next = position + static_cast<size_t>(char_size_at(text, position));
        return next > text.size() ? text.size() : next;
    }

    inline size_t prev_position(std::string_view text, size_t position)
    {
        if (position == 0 || position > text.size())
            return 0;
        size_t probe = position - 1;
        while (probe > 0 && (static_cast<unsigned char>(text[probe]) & 0xc0) == 0x80)
            --probe;
        return probe;
    }

    inline int column_count(std::string_view text)
    {
        int columns = 0;
        size_t position = 0;
        while (position < text.size())
        {
            position = next_position(text, position);
            ++columns;
        }
        return columns;
    }

    inline size_t column_to_byte(std::string_view text, int column)
    {
        size_t position = 0;
        for (int i = 0; i < column && position < text.size(); ++i)
            position = next_position(text, position);
        return position;
    }

    inline int byte_to_column(std::string_view text, size_t byte)
    {
        int column = 0;
        size_t position = 0;
        while (position < text.size() && position < byte)
        {
            position = next_position(text, position);
            ++column;
        }
        return column;
    }

    inline void append_codepoint(std::string& out, unsigned int codepoint)
    {
        if (codepoint < 0x80)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint < 0x800)
        {
            out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        else if (codepoint < 0x10000)
        {
            out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        else
        {
            out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    inline bool is_word_byte(unsigned char byte)
    {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte >= 0x80;
    }

    inline bool is_space_byte(unsigned char byte)
    {
        return byte == ' ' || byte == '\t';
    }
}
