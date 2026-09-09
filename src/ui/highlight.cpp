#include "highlight.hpp"

#include <unordered_map>
#include <unordered_set>

#include "theme.hpp"

namespace
{
    const char* cpp_keywords[] = {
        "alignas", "alignof", "and", "asm", "class", "concept", "consteval", "constexpr", "constinit",
        "const_cast", "co_await", "co_return", "co_yield", "decltype", "delete", "dynamic_cast", "explicit",
        "export", "extern", "friend", "inline", "mutable", "namespace", "new", "noexcept", "not", "operator",
        "or", "private", "protected", "public", "register", "reinterpret_cast", "requires", "sizeof",
        "static_assert", "static_cast", "struct", "template", "thread_local", "typedef", "typeid", "typename",
        "union", "using", "virtual", "volatile", "xor", nullptr
    };

    const char* cpp_control[] = {
        "break", "case", "catch", "continue", "default", "do", "else", "finally", "for", "goto", "if",
        "return", "switch", "throw", "try", "while", nullptr
    };

    const char* cpp_types[] = {
        "auto", "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long",
        "short", "signed", "unsigned", "void", "wchar_t", "size_t", "uint8_t", "uint16_t", "uint32_t",
        "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "intptr_t", "uintptr_t", nullptr
    };

    const char* python_keywords[] = {
        "and", "as", "assert", "async", "await", "class", "def", "del", "elif", "except", "from",
        "global", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "with", "yield", nullptr
    };

    const char* javascript_keywords[] = {
        "abstract", "any", "as", "async", "await", "class", "constructor", "declare", "enum", "export",
        "extends", "from", "get", "implements", "import", "in", "instanceof", "interface", "is", "keyof",
        "let", "namespace", "new", "of", "private", "protected", "public", "readonly", "set", "static",
        "super", "this", "throw", "try", "type", "typeof", "var", "void", "with", "yield", nullptr
    };

    const char* rust_keywords[] = {
        "as", "async", "await", "dyn", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub",
        "ref", "unsafe", "use", "where", "crate", "enum", "fn", "impl", "struct", "trait", "type", "union", nullptr
    };

    bool contains_word(const char** list, const std::string& word)
    {
        for (int i = 0; list[i]; ++i)
        {
            if (word == list[i])
                return true;
        }
        return false;
    }

    bool is_identifier_start(char symbol)
    {
        return (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z') || symbol == '_' || static_cast<unsigned char>(symbol) >= 0x80;
    }

    bool is_identifier_char(char symbol)
    {
        return is_identifier_start(symbol) || (symbol >= '0' && symbol <= '9');
    }

    bool is_digit(char symbol)
    {
        return symbol >= '0' && symbol <= '9';
    }

    struct keyword_sets_t
    {
        const char** keywords = nullptr;
        const char** control = cpp_control;
        const char** types = cpp_types;
        bool hash_preprocessor = true;
    };

    keyword_sets_t sets_for(highlight_lang lang)
    {
        switch (lang)
        {
        case highlight_lang::python:
            return { python_keywords, cpp_control, cpp_types, false };
        case highlight_lang::javascript:
            return { javascript_keywords, cpp_control, cpp_types, false };
        case highlight_lang::rust:
            return { rust_keywords, cpp_control, cpp_types, false };
        case highlight_lang::csharp:
        case highlight_lang::cpp:
        case highlight_lang::generic:
        default:
            return { cpp_keywords, cpp_control, cpp_types, true };
        }
    }
}

highlight_lang highlight_lang_from_extension(const std::string& extension)
{
    static const std::unordered_map<std::string, highlight_lang> map = {
        { ".cpp", highlight_lang::cpp }, { ".cc", highlight_lang::cpp }, { ".cxx", highlight_lang::cpp },
        { ".hpp", highlight_lang::cpp }, { ".hh", highlight_lang::cpp }, { ".h", highlight_lang::cpp },
        { ".ipp", highlight_lang::cpp }, { ".inl", highlight_lang::cpp }, { ".c", highlight_lang::cpp },
        { ".py", highlight_lang::python }, { ".pyw", highlight_lang::python },
        { ".js", highlight_lang::javascript }, { ".ts", highlight_lang::javascript }, { ".jsx", highlight_lang::javascript },
        { ".tsx", highlight_lang::javascript }, { ".mjs", highlight_lang::javascript },
        { ".rs", highlight_lang::rust },
        { ".cs", highlight_lang::csharp },
        { ".json", highlight_lang::json },
        { ".rc", highlight_lang::generic }, { ".slnx", highlight_lang::generic }, { ".vcxproj", highlight_lang::generic },
        { ".md", highlight_lang::none }, { ".txt", highlight_lang::none }, { ".ini", highlight_lang::none }
    };
    auto found = map.find(extension);
    return found != map.end() ? found->second : highlight_lang::generic;
}

highlight_lang highlight_lang_from_name(const std::string& name)
{
    std::string lowered;
    lowered.reserve(name.size());
    for (char symbol : name)
        lowered.push_back(symbol >= 'A' && symbol <= 'Z' ? static_cast<char>(symbol + 32) : symbol);

    if (lowered == "c++" || lowered == "cpp" || lowered == "cxx" || lowered == "hpp" || lowered == "h")
        return highlight_lang::cpp;
    if (lowered == "python" || lowered == "py")
        return highlight_lang::python;
    if (lowered == "js" || lowered == "javascript" || lowered == "ts" || lowered == "typescript")
        return highlight_lang::javascript;
    if (lowered == "rust" || lowered == "rs")
        return highlight_lang::rust;
    if (lowered == "c#" || lowered == "csharp" || lowered == "cs")
        return highlight_lang::csharp;
    if (lowered == "json")
        return highlight_lang::json;
    if (lowered.empty())
        return highlight_lang::none;
    return highlight_lang::generic;
}

const char* highlight_lang_label(highlight_lang lang)
{
    switch (lang)
    {
    case highlight_lang::cpp: return "c++";
    case highlight_lang::python: return "python";
    case highlight_lang::javascript: return "js/ts";
    case highlight_lang::rust: return "rust";
    case highlight_lang::csharp: return "c#";
    case highlight_lang::json: return "json";
    case highlight_lang::generic: return "code";
    default: return "text";
    }
}

std::vector<text_segment_t> highlight_line(const std::string& line, highlight_lang lang, int state_in, int* state_out)
{
    const syntax_palette_t& palette = syntax_palette();
    keyword_sets_t sets = sets_for(lang);
    std::vector<text_segment_t> segments;

    int state = state_in;
    size_t position = 0;
    size_t plain_begin = 0;
    auto flush = [&](size_t until)
    {
        if (until > plain_begin)
            segments.push_back({ static_cast<int>(plain_begin), static_cast<int>(until), palette.plain });
    };

    if (state & 1)
    {
        size_t close = line.find("*/");
        if (close == std::string::npos)
        {
            segments.push_back({ 0, static_cast<int>(line.size()), palette.comment });
            if (state_out)
                *state_out = state;
            return segments;
        }
        segments.push_back({ 0, static_cast<int>(close + 2), palette.comment });
        position = close + 2;
        plain_begin = position;
        state &= ~1;
    }

    while (position < line.size())
    {
        char current = line[position];

        if (current == ' ' || current == '\t')
        {
            ++position;
            continue;
        }

        if (current == '/' && position + 1 < line.size() && line[position + 1] == '/')
        {
            flush(position);
            segments.push_back({ static_cast<int>(position), static_cast<int>(line.size()), palette.comment });
            break;
        }

        if (current == '/' && position + 1 < line.size() && line[position + 1] == '*')
        {
            flush(position);
            size_t close = line.find("*/", position + 2);
            if (close == std::string::npos)
            {
                segments.push_back({ static_cast<int>(position), static_cast<int>(line.size()), palette.comment });
                state |= 1;
                break;
            }
            segments.push_back({ static_cast<int>(position), static_cast<int>(close + 2), palette.comment });
            position = close + 2;
            plain_begin = position;
            continue;
        }

        if (current == '"' || current == '\'')
        {
            flush(position);
            char quote = current;
            size_t scan = position + 1;
            bool closed = false;
            while (scan < line.size())
            {
                if (line[scan] == '\\')
                {
                    scan += 2;
                    continue;
                }
                if (line[scan] == quote)
                {
                    closed = true;
                    ++scan;
                    break;
                }
                ++scan;
            }
            if (!closed)
                scan = line.size();
            segments.push_back({ static_cast<int>(position), static_cast<int>(scan), palette.string });
            position = scan;
            plain_begin = position;
            continue;
        }

        if (is_digit(current))
        {
            flush(position);
            size_t scan = position;
            while (scan < line.size() && (is_digit(line[scan]) || line[scan] == '.' || line[scan] == 'x' || line[scan] == 'X' || (line[scan] >= 'a' && line[scan] <= 'f') || (line[scan] >= 'A' && line[scan] <= 'F') || line[scan] == '\'' || ((line[scan] == '+' || line[scan] == '-') && scan > position && (line[scan - 1] == 'e' || line[scan - 1] == 'E'))))
                ++scan;
            segments.push_back({ static_cast<int>(position), static_cast<int>(scan), palette.number });
            position = scan;
            plain_begin = position;
            continue;
        }

        if (is_identifier_start(current))
        {
            size_t scan = position;
            while (scan < line.size() && is_identifier_char(line[scan]))
                ++scan;
            std::string word = line.substr(position, scan - position);
            flush(position);

            const ImVec4* color = &palette.plain;
            if (contains_word(sets.keywords, word))
                color = &palette.keyword;
            else if (contains_word(sets.control, word))
                color = &palette.control;
            else if (contains_word(sets.types, word))
                color = &palette.type;
            else if (word.starts_with("c_") || (word.size() > 2 && word.ends_with("_t")))
                color = &palette.type;

            size_t after = scan;
            while (after < line.size() && (line[after] == ' ' || line[after] == '\t'))
                ++after;
            if (color == &palette.plain && after < line.size() && line[after] == '(')
                color = &palette.function;

            segments.push_back({ static_cast<int>(position), static_cast<int>(scan), *color });
            position = scan;
            plain_begin = position;
            continue;
        }

        if (current == '#' && sets.hash_preprocessor)
        {
            size_t scan = position + 1;
            while (scan < line.size() && is_identifier_char(line[scan]))
                ++scan;
            std::string word = line.substr(position + 1, scan - position - 1);
            bool preprocessor = word == "include" || word == "define" || word == "pragma" || word == "if" || word == "ifdef" || word == "ifndef" || word == "else" || word == "elif" || word == "endif" || word == "undef" || word == "import" || word == "using";
            if (preprocessor)
            {
                flush(position);
                segments.push_back({ static_cast<int>(position), static_cast<int>(scan), palette.preprocessor });
                position = scan;
                plain_begin = position;
                continue;
            }
        }

        if (current == '{' || current == '}' || current == '(' || current == ')' || current == '[' || current == ']' || current == ';' || current == ',' || current == ':' || current == '<' || current == '>' || current == '=' || current == '+' || current == '-' || current == '*' || current == '&' || current == '|' || current == '!' || current == '~' || current == '?' || current == '%' || current == '^' || current == '.')
        {
            flush(position);
            segments.push_back({ static_cast<int>(position), static_cast<int>(position + 1), palette.punctuation });
            ++position;
            plain_begin = position;
            continue;
        }

        ++position;
    }

    flush(line.size());
    if (state_out)
        *state_out = state;
    return segments;
}
