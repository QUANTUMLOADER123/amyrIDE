#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class json_t
{
public:
    enum class kind
    {
        null,
        boolean,
        number,
        string,
        array,
        object
    };

    using array_t = std::vector<json_t>;
    using object_t = std::vector<std::pair<std::string, json_t>>;

    json_t() = default;
    json_t(std::nullptr_t) {}
    json_t(bool value) : entry_kind(kind::boolean), boolean_value(value) {}
    json_t(int value) : entry_kind(kind::number), number_value(value) {}
    json_t(unsigned int value) : entry_kind(kind::number), number_value(value) {}
    json_t(long value) : entry_kind(kind::number), number_value(static_cast<double>(value)) {}
    json_t(unsigned long value) : entry_kind(kind::number), number_value(static_cast<double>(value)) {}
    json_t(long long value) : entry_kind(kind::number), number_value(static_cast<double>(value)) {}
    json_t(unsigned long long value) : entry_kind(kind::number), number_value(static_cast<double>(value)) {}
    json_t(float value) : entry_kind(kind::number), number_value(value) {}
    json_t(double value) : entry_kind(kind::number), number_value(value) {}
    json_t(const char* value) : entry_kind(kind::string), string_value(value ? value : "") {}
    json_t(std::string value) : entry_kind(kind::string), string_value(std::move(value)) {}
    json_t(std::string_view value) : entry_kind(kind::string), string_value(value) {}
    json_t(array_t value) : entry_kind(kind::array), array_value(std::move(value)) {}
    json_t(object_t value) : entry_kind(kind::object), object_value(std::move(value)) {}

    static bool parse(std::string_view text, json_t& out);
    static json_t object();
    static json_t array();

    std::string dump(bool pretty = false) const;

    kind type() const { return entry_kind; }
    bool is_null() const { return entry_kind == kind::null; }
    bool is_bool() const { return entry_kind == kind::boolean; }
    bool is_number() const { return entry_kind == kind::number; }
    bool is_string() const { return entry_kind == kind::string; }
    bool is_array() const { return entry_kind == kind::array; }
    bool is_object() const { return entry_kind == kind::object; }

    bool as_bool(bool fallback = false) const { return is_bool() ? boolean_value : fallback; }
    int as_int(int fallback = 0) const;
    long long as_int64(long long fallback = 0) const;
    double as_float(double fallback = 0.0) const { return is_number() ? number_value : fallback; }
    const std::string& as_string() const { return string_value; }
    std::string as_string(std::string_view fallback) const { return is_string() ? string_value : std::string(fallback); }

    bool has(std::string_view key) const { return find(key) != nullptr; }
    const json_t* find(std::string_view key) const;
    json_t* find(std::string_view key);
    json_t& operator[](std::string_view key);
    const json_t& operator[](std::string_view key) const;
    const json_t& operator[](size_t index) const { return array_value[index]; }
    json_t& operator[](size_t index) { return array_value[index]; }

    void push(json_t value) { ensure_array().array_value.push_back(std::move(value)); }
    size_t size() const { return is_array() ? array_value.size() : is_object() ? object_value.size() : 0; }
    const json_t& at(size_t index) const { return array_value[index]; }

    object_t::iterator begin() { return object_value.begin(); }
    object_t::iterator end() { return object_value.end(); }
    object_t::const_iterator begin() const { return object_value.begin(); }
    object_t::const_iterator end() const { return object_value.end(); }

private:
    json_t& ensure_object();
    json_t& ensure_array();

    kind entry_kind = kind::null;
    bool boolean_value = false;
    double number_value = 0.0;
    std::string string_value;
    array_t array_value;
    object_t object_value;
};
