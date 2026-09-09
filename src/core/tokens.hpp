#pragma once

#include <string_view>

namespace tokens
{
    inline int estimate(std::string_view text)
    {
        size_t ascii = 0;
        size_t wide = 0;
        for (char symbol : text)
        {
            if (static_cast<unsigned char>(symbol) < 0x80)
                ++ascii;
            else
                ++wide;
        }
        double estimate = static_cast<double>(ascii) / 4.0 + static_cast<double>(wide) / 1.2;
        return static_cast<int>(estimate) + 8;
    }
}
