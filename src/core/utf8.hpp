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

    inline bool is_valid(std::string_view text)
    {
        size_t position = 0;
        while (position < text.size())
        {
            unsigned char lead = static_cast<unsigned char>(text[position]);
            int size = char_size(lead);
            if (lead >= 0x80 && size == 1)
                return false;
            if (position + static_cast<size_t>(size) > text.size())
                return false;
            for (int i = 1; i < size; ++i)
            {
                if ((static_cast<unsigned char>(text[position + static_cast<size_t>(i)]) & 0xc0) != 0x80)
                    return false;
            }
            position += static_cast<size_t>(size);
        }
        return true;
    }

    inline std::string from_cp1251(std::string_view text)
    {
        const unsigned int upper[64] = {
            0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
            0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
            0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x0491, 0x0407, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD,
            0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405
        };
        std::string out;
        out.reserve(text.size() * 2);
        for (unsigned char symbol : text)
        {
            if (symbol < 0x80)
                out.push_back(static_cast<char>(symbol));
            else if (symbol >= 0xc0)
                append_codepoint(out, 0x0410 + (symbol - 0xc0));
            else
                append_codepoint(out, upper[symbol - 0x80]);
        }
        return out;
    }

    inline std::string ensure_utf8(std::string_view text)
    {
        return is_valid(text) ? std::string(text) : from_cp1251(text);
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
