#pragma once

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace str
{
    inline std::string trim(std::string_view text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && static_cast<unsigned char>(text[begin]) <= ' ')
            ++begin;
        while (end > begin && static_cast<unsigned char>(text[end - 1]) <= ' ')
            --end;
        return std::string(text.substr(begin, end - begin));
    }

    inline std::vector<std::string> split(std::string_view text, char delimiter)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (true)
        {
            size_t hit = text.find(delimiter, start);
            if (hit == std::string_view::npos)
            {
                parts.emplace_back(text.substr(start));
                return parts;
            }
            parts.emplace_back(text.substr(start, hit - start));
            start = hit + 1;
        }
    }

    inline std::string join(const std::vector<std::string>& parts, std::string_view glue)
    {
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
                out += glue;
            out += parts[i];
        }
        return out;
    }

    inline void replace_all(std::string& text, std::string_view from, std::string_view to)
    {
        if (from.empty())
            return;
        size_t position = 0;
        while ((position = text.find(from, position)) != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
    }

    inline std::string to_lower(std::string_view text)
    {
        std::string out(text);
        for (char& symbol : out)
        {
            if (symbol >= 'A' && symbol <= 'Z')
                symbol += 'a' - 'A';
        }
        return out;
    }

    inline bool iequals(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size())
            return false;
        for (size_t i = 0; i < left.size(); ++i)
        {
            char a = left[i] >= 'A' && left[i] <= 'Z' ? left[i] + 'a' - 'A' : left[i];
            char b = right[i] >= 'A' && right[i] <= 'Z' ? right[i] + 'a' - 'A' : right[i];
            if (a != b)
                return false;
        }
        return true;
    }

    inline std::string format(const char* pattern, ...)
    {
        va_list args;
        va_start(args, pattern);
        va_list args_copy;
        va_copy(args_copy, args);
        int needed = std::vsnprintf(nullptr, 0, pattern, args);
        va_end(args);
        std::string out;
        if (needed > 0)
        {
            out.resize(static_cast<size_t>(needed));
            std::vsnprintf(out.data(), out.size() + 1, pattern, args_copy);
        }
        va_end(args_copy);
        return out;
    }

    inline std::string format_count(long long value)
    {
        double magnitude = static_cast<double>(value < 0 ? -value : value);
        if (magnitude >= 1000000)
            return format("%.1fm", magnitude / 1000000.0);
        if (magnitude >= 1000)
            return format("%.1fk", magnitude / 1000.0);
        return std::to_string(value);
    }

    inline std::string truncate_middle(std::string_view text, size_t limit)
    {
        if (text.size() <= limit)
            return std::string(text);
        size_t head = limit / 2;
        size_t tail = limit - head;
        return std::string(text.substr(0, head)) + " ... " + std::string(text.substr(text.size() - tail));
    }

    inline std::string strip_ansi(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\x1b' && i + 1 < text.size())
            {
                char next = text[i + 1];
                if (next == '[')
                {
                    i += 2;
                    while (i < text.size() && !((text[i] >= '@' && text[i] <= '~')))
                        ++i;
                    continue;
                }
                if (next == ']' || next == '(' || next == ')')
                {
                    ++i;
                    while (i < text.size() && text[i] != '\x07' && !(text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '\\'))
                        ++i;
                    if (i < text.size() && text[i] == '\x07')
                        ++i;
                    continue;
                }
            }
            if (text[i] != '\r')
                out.push_back(text[i]);
        }
        return out;
    }

    inline bool fuzzy_match(std::string_view text, std::string_view query)
    {
        if (query.empty())
            return true;
        size_t cursor = 0;
        for (char symbol : text)
        {
            if (cursor < query.size() && (symbol | 0x20) == (query[cursor] | 0x20))
                ++cursor;
        }
        return cursor == query.size();
    }

    inline int fuzzy_score(std::string_view text, std::string_view query)
    {
        if (query.empty())
            return 0;
        int score = 0;
        size_t cursor = 0;
        bool previous_hit = true;
        for (size_t i = 0; i < text.size() && cursor < query.size(); ++i)
        {
            if ((text[i] | 0x20) == (query[cursor] | 0x20))
            {
                score += previous_hit ? 3 : 1;
                if (i == 0 || text[i - 1] == '_' || text[i - 1] == '/' || text[i - 1] == '\\' || text[i - 1] == '.' || text[i - 1] == ' ')
                    score += 2;
                previous_hit = true;
                ++cursor;
            }
            else
            {
                previous_hit = false;
            }
        }
        if (cursor != query.size())
            return -1;
        score -= static_cast<int>(text.size()) / 8;
        return score;
    }
}
