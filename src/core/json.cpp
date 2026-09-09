#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
    constexpr int parse_depth_limit = 100;

    struct parser_t
    {
        std::string_view text;
        size_t position = 0;
        int depth = 0;
        bool failed = false;

        char peek() const { return position < text.size() ? text[position] : '\0'; }
        char take() { return position < text.size() ? text[position++] : '\0'; }
        void skip_space()
        {
            while (position < text.size())
            {
                char current = text[position];
                if (current != ' ' && current != '\t' && current != '\n' && current != '\r')
                    return;
                ++position;
            }
        }
        bool expect(char symbol)
        {
            skip_space();
            if (peek() != symbol)
            {
                failed = true;
                return false;
            }
            ++position;
            return true;
        }
        bool consume_literal(std::string_view literal)
        {
            if (text.compare(position, literal.size(), literal) != 0)
            {
                failed = true;
                return false;
            }
            position += literal.size();
            return true;
        }
    };

    void append_utf8(std::string& out, unsigned int codepoint)
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

    unsigned int decode_hex4(std::string_view text, size_t position, bool* ok)
    {
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i)
        {
            char digit = position + static_cast<size_t>(i) < text.size() ? text[position + static_cast<size_t>(i)] : '\0';
            value <<= 4;
            if (digit >= '0' && digit <= '9')
                value |= static_cast<unsigned int>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<unsigned int>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<unsigned int>(digit - 'A' + 10);
            else
            {
                *ok = false;
                return 0;
            }
        }
        return value;
    }

    bool parse_string_body(parser_t& state, std::string& out)
    {
        if (!state.expect('"'))
            return false;
        while (state.position < state.text.size())
        {
            char current = state.text[state.position++];
            if (current == '"')
                return true;
            if (current != '\\')
            {
                out.push_back(current);
                continue;
            }
            char escape = state.take();
            switch (escape)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                bool ok = true;
                unsigned int codepoint = decode_hex4(state.text, state.position, &ok);
                state.position += 4;
                if (!ok)
                {
                    state.failed = true;
                    return false;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff && state.text.compare(state.position, 2, "\\u") == 0)
                {
                    bool low_ok = true;
                    unsigned int low = decode_hex4(state.text, state.position + 2, &low_ok);
                    if (low_ok && low >= 0xdc00 && low <= 0xdfff)
                    {
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                        state.position += 6;
                    }
                }
                append_utf8(out, codepoint);
                break;
            }
            default:
                state.failed = true;
                return false;
            }
        }
        state.failed = true;
        return false;
    }

    bool parse_number(parser_t& state, double& out)
    {
        size_t begin = state.position;
        if (state.peek() == '-')
            ++state.position;
        bool digits = false;
        while (state.peek() >= '0' && state.peek() <= '9')
        {
            ++state.position;
            digits = true;
        }
        if (state.peek() == '.')
        {
            ++state.position;
            while (state.peek() >= '0' && state.peek() <= '9')
            {
                ++state.position;
                digits = true;
            }
        }
        if (state.peek() == 'e' || state.peek() == 'E')
        {
            ++state.position;
            if (state.peek() == '+' || state.peek() == '-')
                ++state.position;
            while (state.peek() >= '0' && state.peek() <= '9')
                ++state.position;
        }
        if (!digits)
        {
            state.failed = true;
            return false;
        }
        std::string number_text(state.text.substr(begin, state.position - begin));
        out = std::strtod(number_text.c_str(), nullptr);
        return true;
    }

    bool parse_value(parser_t& state, json_t& out);

    bool parse_container(parser_t& state, json_t& out, bool want_object)
    {
        if (state.depth >= parse_depth_limit)
        {
            state.failed = true;
            return false;
        }
        ++state.depth;
        json_t container = want_object ? json_t::object() : json_t::array();
        state.skip_space();
        if (state.peek() == (want_object ? '}' : ']'))
        {
            ++state.position;
            --state.depth;
            out = std::move(container);
            return true;
        }
        while (true)
        {
            state.skip_space();
            json_t element;
            if (want_object)
            {
                std::string key;
                if (!parse_string_body(state, key))
                    break;
                if (!state.expect(':'))
                    break;
                if (!parse_value(state, element))
                    break;
                container[std::string_view(key)] = std::move(element);
            }
            else
            {
                if (!parse_value(state, element))
                    break;
                container.push(std::move(element));
            }
            state.skip_space();
            char current = state.peek();
            if (current == ',')
            {
                ++state.position;
                continue;
            }
            if (current == (want_object ? '}' : ']'))
            {
                ++state.position;
                --state.depth;
                out = std::move(container);
                return true;
            }
            break;
        }
        state.failed = true;
        --state.depth;
        return false;
    }

    bool parse_value(parser_t& state, json_t& out)
    {
        state.skip_space();
        char current = state.peek();
        if (current == '{')
        {
            ++state.position;
            return parse_container(state, out, true);
        }
        if (current == '[')
        {
            ++state.position;
            return parse_container(state, out, false);
        }
        if (current == '"')
        {
            std::string value;
            if (!parse_string_body(state, value))
                return false;
            out = json_t(std::move(value));
            return true;
        }
        if (current == 't')
            return state.consume_literal("true") ? (out = json_t(true), true) : false;
        if (current == 'f')
            return state.consume_literal("false") ? (out = json_t(false), true) : false;
        if (current == 'n')
            return state.consume_literal("null") ? (out = json_t(nullptr), true) : false;
        double number = 0.0;
        if (parse_number(state, number))
        {
            out = json_t(number);
            return true;
        }
        return false;
    }

    void dump_string(std::string& out, const std::string& value)
    {
        out.push_back('"');
        for (unsigned char symbol : value)
        {
            switch (symbol)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (symbol < 0x20)
                {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", symbol);
                    out += escape;
                }
                else
                {
                    out.push_back(static_cast<char>(symbol));
                }
                break;
            }
        }
        out.push_back('"');
    }

    void dump_number(std::string& out, double value)
    {
        if (std::isfinite(value) && std::fabs(value) < 1.0e15 && value == std::floor(value))
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
            out += buffer;
            return;
        }
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        out += buffer;
    }

    void dump_value(std::string& out, const json_t& value, bool pretty, int depth)
    {
        const std::string line_feed = pretty ? "\n" : "";
        const std::string indent(static_cast<size_t>(depth) * 2, ' ');
        const std::string child_indent(static_cast<size_t>(depth + 1) * 2, ' ');

        switch (value.type())
        {
        case json_t::kind::null: out += "null"; break;
        case json_t::kind::boolean: out += value.as_bool() ? "true" : "false"; break;
        case json_t::kind::number: dump_number(out, value.as_float()); break;
        case json_t::kind::string: dump_string(out, value.as_string()); break;
        case json_t::kind::array:
        {
            if (value.size() == 0)
            {
                out += "[]";
                return;
            }
            out.push_back('[');
            out += line_feed;
            for (size_t i = 0; i < value.size(); ++i)
            {
                out += child_indent;
                dump_value(out, value.at(i), pretty, depth + 1);
                if (i + 1 < value.size())
                    out.push_back(',');
                out += line_feed;
            }
            out += indent;
            out.push_back(']');
            break;
        }
        case json_t::kind::object:
        {
            if (value.size() == 0)
            {
                out += "{}";
                return;
            }
            out.push_back('{');
            out += line_feed;
            size_t index = 0;
            for (const auto& [key, element] : value)
            {
                out += child_indent;
                dump_string(out, key);
                out += pretty ? ": " : ":";
                dump_value(out, element, pretty, depth + 1);
                if (++index < value.size())
                    out.push_back(',');
                out += line_feed;
            }
            out += indent;
            out.push_back('}');
            break;
        }
        }
    }
}

bool json_t::parse(std::string_view text, json_t& out)
{
    parser_t state;
    state.text = text;
    json_t result;
    if (!parse_value(state, result))
        return false;
    state.skip_space();
    if (state.position != state.text.size())
        return false;
    out = std::move(result);
    return true;
}

json_t json_t::object()
{
    return json_t(object_t{});
}

json_t json_t::array()
{
    return json_t(array_t{});
}

int json_t::as_int(int fallback) const
{
    return is_number() ? static_cast<int>(std::llround(number_value)) : fallback;
}

long long json_t::as_int64(long long fallback) const
{
    return is_number() ? static_cast<long long>(std::llround(number_value)) : fallback;
}

const json_t* json_t::find(std::string_view key) const
{
    if (!is_object())
        return nullptr;
    for (const auto& [entry_key, entry] : object_value)
    {
        if (entry_key == key)
            return &entry;
    }
    return nullptr;
}

json_t* json_t::find(std::string_view key)
{
    if (!is_object())
        return nullptr;
    for (auto& [entry_key, entry] : object_value)
    {
        if (entry_key == key)
            return &entry;
    }
    return nullptr;
}

json_t& json_t::operator[](std::string_view key)
{
    if (json_t* existing = find(key))
        return *existing;
    ensure_object().object_value.emplace_back(std::string(key), json_t());
    return object_value.back().second;
}

const json_t& json_t::operator[](std::string_view key) const
{
    static const json_t null_value;
    if (const json_t* existing = find(key))
        return *existing;
    return null_value;
}

json_t& json_t::ensure_object()
{
    if (!is_object())
    {
        *this = json_t(object_t{});
    }
    return *this;
}

json_t& json_t::ensure_array()
{
    if (!is_array())
    {
        *this = json_t(array_t{});
    }
    return *this;
}

std::string json_t::dump(bool pretty) const
{
    std::string out;
    dump_value(out, *this, pretty, 0);
    return out;
}
