#include "tools.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "core/string.hpp"
#include "core/utf8.hpp"
#include "platform/process.hpp"
#include "platform/shell.hpp"

namespace
{
    constexpr int list_depth_default = 3;
    constexpr int list_entry_limit = 500;
    constexpr int dir_entry_limit = 1000;
    constexpr int read_line_limit = 500;
    constexpr int search_hit_limit = 80;
    constexpr int file_search_limit = 120;
    constexpr int text_scan_file_limit = 4000;
    constexpr unsigned int command_timeout_default_ms = 45000;
    constexpr unsigned int command_timeout_max_ms = 180000;
    constexpr long long text_file_size_limit = 1048576;
    constexpr size_t tool_output_limit = 12000;

    std::string path_display(const c_workspace& space, const std::filesystem::path& path)
    {
        return space.relative_to_root(path);
    }

    bool read_text_file(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.good())
            return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = utf8::ensure_utf8(buffer.str());
        return true;
    }

    bool write_text_file(const std::filesystem::path& path, const std::string& content)
    {
        std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::error_code make_error;
            std::filesystem::create_directories(parent, make_error);
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.good())
            return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return file.good();
    }

    std::string numbered_content(const std::vector<std::string>& lines, int begin_line, int end_line)
    {
        int gutter_width = str::format("%d", end_line).size();
        std::string out;
        for (int i = begin_line; i <= end_line; ++i)
        {
            std::string number = str::format("%*d", gutter_width, i);
            out += number + " | " + lines[static_cast<size_t>(i - 1)] + "\n";
        }
        return out;
    }

    void split_lines(const std::string& text, std::vector<std::string>& out)
    {
        out.clear();
        std::string current;
        for (char symbol : text)
        {
            if (symbol == '\n')
            {
                if (!current.empty() && current.back() == '\r')
                    current.pop_back();
                out.push_back(std::move(current));
                current.clear();
            }
            else
            {
                current.push_back(symbol);
            }
        }
        out.push_back(std::move(current));
    }

    void line_delta(const std::string& before, const std::string& after, int& out_added, int& out_removed)
    {
        std::unordered_map<std::string, std::pair<int, int>> counts;
        std::vector<std::string> lines;
        split_lines(before, lines);
        for (const std::string& line : lines)
            counts[line].first++;
        split_lines(after, lines);
        for (const std::string& line : lines)
            counts[line].second++;
        out_added = 0;
        out_removed = 0;
        for (const auto& entry : counts)
        {
            if (entry.second.second > entry.second.first)
                out_added += entry.second.second - entry.second.first;
            if (entry.second.first > entry.second.second)
                out_removed += entry.second.first - entry.second.second;
        }
    }

    tool_result_t make_result(bool ok, const std::string& subtitle, const std::string& details, int added = 0, int removed = 0)
    {
        std::string head = ok ? "OK" : "ERR";
        if (ok && (added > 0 || removed > 0))
            head += str::format(" +%d -%d", added, removed);
        tool_result_t result;
        result.ok = ok;
        result.content = head + " " + subtitle + "\n" + details;
        result.summary_line = subtitle;
        return result;
    }

    json_t param(const char* name, const char* type, const char* description, bool required)
    {
        return json_t::object_t{
            { "name", name },
            { "type", type },
            { "description", description },
            { "required", required }
        };
    }

    json_t function_schema(const char* name, const char* description, std::vector<json_t> params)
    {
        json_t properties = json_t::object();
        json_t required = json_t::array();
        for (json_t& entry : params)
        {
            std::string param_name = entry["name"].as_string();
            bool param_required = entry["required"].as_bool(false);
            entry["name"] = json_t(nullptr);
            entry["required"] = json_t(nullptr);
            json_t cleaned = json_t::object_t{
                { "type", entry["type"].as_string("string") },
                { "description", entry["description"].as_string("") }
            };
            properties[param_name] = std::move(cleaned);
            if (param_required)
                required.push(json_t(param_name));
        }
        json_t parameters = json_t::object_t{
            { "type", "object" },
            { "properties", std::move(properties) },
            { "required", std::move(required) }
        };
        return json_t::object_t{
            { "type", "function" },
            { "function", json_t::object_t{
                { "name", name },
                { "description", description },
                { "parameters", std::move(parameters) }
            } }
        };
    }

    bool glob_match(std::string_view name, std::string_view pattern)
    {
        size_t name_index = 0;
        size_t pattern_index = 0;
        size_t star = std::string_view::npos;
        size_t resume = 0;
        while (name_index < name.size())
        {
            if (pattern_index < pattern.size() && (pattern[pattern_index] == '?' || std::tolower(static_cast<unsigned char>(name[name_index])) == std::tolower(static_cast<unsigned char>(pattern[pattern_index]))))
            {
                ++name_index;
                ++pattern_index;
            }
            else if (pattern_index < pattern.size() && pattern[pattern_index] == '*')
            {
                star = pattern_index++;
                resume = name_index;
            }
            else if (star != std::string_view::npos)
            {
                pattern_index = star + 1;
                name_index = ++resume;
            }
            else
            {
                return false;
            }
        }
        while (pattern_index < pattern.size() && pattern[pattern_index] == '*')
            ++pattern_index;
        return pattern_index == pattern.size();
    }

    std::string normalize_newlines(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (char symbol : text)
        {
            if (symbol != '\r')
                out.push_back(symbol);
        }
        return out;
    }
}

c_tool_registry::c_tool_registry(tool_hooks_t hooks)
    : hooks(std::move(hooks))
{
}

json_t c_tool_registry::build_tools_schema() const
{
    json_t tools = json_t::array();
    tools.push(function_schema("list_files", "List files and directories under a path as an indented tree.", { param("path", "string", "Relative path inside the workspace, empty for root", false), param("depth", "integer", "Recursion depth, default 3, max 6", false) }));
    tools.push(function_schema("list_dir", "List one directory non-recursively with entry types and sizes. Faster than list_files for a quick look.", { param("path", "string", "Relative directory path, empty for workspace root", false) }));
    tools.push(function_schema("workspace_info", "Workspace overview: file count, total size, top file types. Call it first when orienting in an unknown project.", {}));
    tools.push(function_schema("read_file", "Read a text file. Returns numbered lines. Cached: re-reading an unchanged file returns a short notice unless force=true.", { param("path", "string", "Relative path of the file", true), param("start_line", "integer", "First line to read, 1-based, default 1", false), param("end_line", "integer", "Last line to read, inclusive, default 500 lines", false), param("force", "boolean", "Bypass the read cache and return full content", false) }));
    tools.push(function_schema("file_info", "File metadata: size, line count, extension.", { param("path", "string", "Relative path of the file", true) }));
    tools.push(function_schema("write_file", "Create a file or fully overwrite an existing one. Parent directories are created automatically. Prefer edit tools for small changes.", { param("path", "string", "Relative path of the file", true), param("content", "string", "Full file content", true) }));
    tools.push(function_schema("append_file", "Append content to the end of an existing file.", { param("path", "string", "Relative path of the file", true), param("content", "string", "Text to append", true) }));
    tools.push(function_schema("edit_file", "Replace an exact snippet inside a file. The search text must match exactly once unless replace_all=true. Keep snippets short and unique.", { param("path", "string", "Relative path of the file", true), param("search", "string", "Exact text to find", true), param("replace", "string", "Replacement text", true), param("replace_all", "boolean", "Replace every occurrence, default false", false) }));
    tools.push(function_schema("replace_lines", "Replace a line range with new content in one shot. Best way to rewrite a whole function or block. Pass empty content to just delete the lines.", { param("path", "string", "Relative path of the file", true), param("start_line", "integer", "First line to replace, 1-based, inclusive", true), param("end_line", "integer", "Last line to replace, inclusive", true), param("content", "string", "Replacement content, may span many lines or be empty", true) }));
    tools.push(function_schema("insert_lines", "Insert new lines before the given line. Use line = total_lines + 1 to append.", { param("path", "string", "Relative path of the file", true), param("line", "integer", "1-based line number to insert before", true), param("content", "string", "Lines to insert", true) }));
    tools.push(function_schema("delete_lines", "Delete a line range from a file.", { param("path", "string", "Relative path of the file", true), param("start_line", "integer", "First line to delete, 1-based, inclusive", true), param("end_line", "integer", "Last line to delete, inclusive", true) }));
    tools.push(function_schema("create_directory", "Create a directory including missing parents.", { param("path", "string", "Relative directory path", true) }));
    tools.push(function_schema("copy_path", "Copy a file or a directory tree to a new path.", { param("from", "string", "Relative source path", true), param("to", "string", "Relative destination path", true) }));
    tools.push(function_schema("delete_path", "Delete a file or a directory tree. Cannot delete the workspace root.", { param("path", "string", "Relative path to delete", true) }));
    tools.push(function_schema("move_path", "Rename or move a file or directory.", { param("from", "string", "Relative source path", true), param("to", "string", "Relative destination path", true) }));
    tools.push(function_schema("search_files", "Find files by name pattern with * and ? wildcards.", { param("pattern", "string", "File name pattern, e.g. *.cpp", true), param("path", "string", "Relative directory to search in, empty for whole workspace", false) }));
    tools.push(function_schema("search_text", "Search file contents across the workspace. Returns matches as path:line:text.", { param("query", "string", "Text or regex to find", true), param("regex", "boolean", "Treat query as ECMAScript regex, default false", false), param("case_sensitive", "boolean", "Case sensitive search, default false", false), param("path", "string", "Relative directory to restrict search to", false) }));
    tools.push(function_schema("set_plan", "Publish or update the step-by-step plan for the current task. Call it before multi-step work and update it as steps complete: pass indexes of finished steps in done.", { param("steps", "array", "Ordered short step descriptions", true), param("done", "array", "0-based indexes of completed steps", false) }));
    tools.push(function_schema("run_command", "Run a cmd.exe command inside the workspace root and capture output. Use for builds, git, package managers.", { param("command", "string", "Command line to execute", true), param("timeout_ms", "integer", "Timeout in milliseconds, default 45000, max 180000", false) }));
    tools.push(function_schema("run_powershell", "Run a PowerShell script inside the workspace root and capture output. Preferred over run_command for anything non-trivial: objects, pipelines, JSON, registry, services, string processing.", { param("script", "string", "PowerShell code to execute, multiple lines allowed", true), param("timeout_ms", "integer", "Timeout in milliseconds, default 45000, max 180000", false) }));
    return tools;
}

std::string c_tool_registry::truncate_output(std::string text) const
{
    if (text.size() <= tool_output_limit)
        return text;
    size_t head = tool_output_limit * 2 / 3;
    size_t tail = tool_output_limit / 3;
    return text.substr(0, head) + "\n... [output truncated, " + str::format_count(static_cast<long long>(text.size())) + " chars total] ...\n" + text.substr(text.size() - tail);
}

std::vector<plan_step_t> c_tool_registry::plan_snapshot() const
{
    std::lock_guard<std::mutex> guard(plan_mutex);
    return plan_steps;
}

int c_tool_registry::plan_done_count() const
{
    std::lock_guard<std::mutex> guard(plan_mutex);
    int done = 0;
    for (const plan_step_t& step : plan_steps)
    {
        if (step.done)
            ++done;
    }
    return done;
}

tool_result_t c_tool_registry::execute(const std::string& name, const std::string& arguments_json)
{
    const c_workspace* space = active_workspace();
    if (!space || !space->valid)
        return { false, "No workspace is open. Ask the user to open a project folder first.", "" };

    json_t args;
    if (!arguments_json.empty() && !json_t::parse(arguments_json, args))
    {
        json_t repaired;
        if (json_t::parse("{" + arguments_json + "}", repaired))
            args = repaired;
        else
            return { false, "Malformed tool arguments: " + str::truncate_middle(arguments_json, 200), "" };
    }

    tool_result_t result;
    if (name == "list_files")
        result = run_list_files(args);
    else if (name == "list_dir")
        result = run_list_dir(args);
    else if (name == "workspace_info")
        result = run_workspace_info(args);
    else if (name == "read_file")
        result = run_read_file(args);
    else if (name == "file_info")
        result = run_file_info(args);
    else if (name == "write_file")
        result = run_write_file(args);
    else if (name == "append_file")
        result = run_append_file(args);
    else if (name == "edit_file")
        result = run_edit_file(args);
    else if (name == "replace_lines")
        result = run_replace_lines(args);
    else if (name == "insert_lines")
        result = run_insert_lines(args);
    else if (name == "delete_lines")
        result = run_delete_lines(args);
    else if (name == "create_directory")
        result = run_create_directory(args);
    else if (name == "copy_path")
        result = run_copy_path(args);
    else if (name == "delete_path")
        result = run_delete_path(args);
    else if (name == "move_path")
        result = run_move_path(args);
    else if (name == "search_files")
        result = run_search_files(args);
    else if (name == "search_text")
        result = run_search_text(args);
    else if (name == "set_plan")
        result = run_set_plan(args);
    else if (name == "run_command")
        result = run_run_command(args);
    else if (name == "run_powershell")
        result = run_powershell(args);
    else
        result = { false, "Unknown tool: " + name, "" };

    result.content = truncate_output(std::move(result.content));
    return result;
}

tool_result_t c_tool_registry::run_list_files(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path base = args["path"].as_string("").empty() ? space->root : space->resolve(args["path"].as_string(""));
    std::error_code error;
    if (!std::filesystem::is_directory(base, error))
        return make_result(false, "directory not found: " + args["path"].as_string(""), "");

    int depth = args["depth"].as_int(list_depth_default);
    depth = std::clamp(depth, 1, 6);

    std::string out;
    int entries = 0;
    std::function<void(const std::filesystem::path&, int)> walk = [&](const std::filesystem::path& directory, int level) {
        std::error_code iteration_error;
        std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, iteration_error);
        for (const std::filesystem::directory_entry& entry : iterator)
        {
            if (entries >= list_entry_limit)
                return;
            std::string name = entry.path().filename().string();
            if (c_workspace::is_ignored_name(name))
                continue;
            std::error_code entry_error;
            bool is_dir = entry.is_directory(entry_error);
            out += std::string(static_cast<size_t>(level) * 2, ' ') + (is_dir ? "[dir]  " : "       ") + name + "\n";
            ++entries;
            if (is_dir && level + 1 < depth)
                walk(entry.path(), level + 1);
        }
    };
    walk(base, 0);

    std::string header = "Workspace: " + space->display_name() + "\nBase: " + path_display(*space, base) + "\n\n";
    return make_result(true, str::format_count(entries) + " entries", header + (out.empty() ? "(empty)" : out));
}

tool_result_t c_tool_registry::run_list_dir(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path base = args["path"].as_string("").empty() ? space->root : space->resolve(args["path"].as_string(""));
    std::error_code error;
    if (!std::filesystem::is_directory(base, error))
        return make_result(false, "directory not found: " + args["path"].as_string(""), "");

    std::string out;
    int entries = 0;
    std::error_code iteration_error;
    std::filesystem::directory_iterator iterator(base, std::filesystem::directory_options::skip_permission_denied, iteration_error);
    for (const std::filesystem::directory_entry& entry : iterator)
    {
        if (entries >= dir_entry_limit)
        {
            out += "... (limit reached)\n";
            break;
        }
        std::string name = entry.path().filename().string();
        if (c_workspace::is_ignored_name(name))
            continue;
        std::error_code entry_error;
        if (entry.is_directory(entry_error))
            out += "[dir]  " + name + "\n";
        else
            out += str::format("%9s  %s\n", str::format_count(static_cast<long long>(entry.file_size(entry_error))).c_str(), name.c_str());
        ++entries;
    }
    return make_result(true, path_display(*space, base) + " · " + str::format_count(entries) + " entries", out.empty() ? "(empty)" : out);
}

tool_result_t c_tool_registry::run_workspace_info(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::unordered_map<std::string, int> extensions;
    long long total_size = 0;
    int files = 0;
    for (const std::filesystem::path& file : space->flat_files)
    {
        ++files;
        std::string extension = file.extension().string();
        if (extension.empty())
            extension = "(none)";
        extensions[extension]++;
        std::error_code error;
        total_size += static_cast<long long>(std::filesystem::file_size(file, error));
    }
    std::vector<std::pair<std::string, int>> sorted(extensions.begin(), extensions.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    std::string out = "Workspace: " + space->root.string() + "\nFiles: " + str::format_count(files) + "\nTotal size: " + str::format_count(total_size) + " bytes\n\nTop file types:\n";
    int shown = 0;
    for (const auto& entry : sorted)
    {
        out += "  " + entry.first + " · " + std::to_string(entry.second) + "\n";
        if (++shown >= 12)
            break;
    }
    return make_result(true, str::format_count(files) + " files · " + str::format_count(total_size) + " bytes", out);
}

tool_result_t c_tool_registry::run_read_file(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    long long size = static_cast<long long>(std::filesystem::file_size(full, error));
    if (size > text_file_size_limit)
        return make_result(false, "file too large to read fully (" + str::format_count(size) + " bytes)", "Read it in parts with start_line/end_line.");

    int generation = hooks.context_generation ? hooks.context_generation() : 0;
    std::string cache_key = full.string();
    auto cached = file_cache.find(cache_key);
    bool force = args["force"].as_bool(false);
    if (!force && cached != file_cache.end() && cached->second.generation == generation && static_cast<long long>(std::filesystem::file_size(full, error)) == cached->second.size)
        return make_result(true, relative + " · cached", "File " + relative + " is unchanged since it was read earlier in this conversation (" + str::format_count(cached->second.size) + " bytes, already in context). Use force=true to re-read.");

    std::string content;
    if (!read_text_file(full, content))
        return make_result(false, "failed to read " + relative, "");

    std::vector<std::string> lines;
    split_lines(content, lines);
    int total = static_cast<int>(lines.size());
    int start_line = std::max(1, args["start_line"].as_int(1));
    int end_line = args["end_line"].as_int(std::min(total, start_line + read_line_limit - 1));
    end_line = std::min(end_line, total);
    if (start_line > total)
        return make_result(false, str::format("start_line %d is beyond end of file (%d lines)", start_line, total), "");

    std::string out = relative + " (" + std::to_string(total) + " lines" + (start_line > 1 || end_line < total ? str::format(", showing %d-%d", start_line, end_line) : "") + ")\n";
    out += numbered_content(lines, start_line, end_line);
    if (end_line < total)
        out += str::format("... (%d more lines, use start_line/end_line to continue)\n", total - end_line);

    auto& entry = file_cache[cache_key];
    entry.size = static_cast<long long>(content.size());
    entry.generation = generation;
    return make_result(true, relative + " · " + std::to_string(total) + " lines", out);
}

tool_result_t c_tool_registry::run_file_info(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    long long size = static_cast<long long>(std::filesystem::file_size(full, error));
    std::string content;
    read_text_file(full, content);
    std::vector<std::string> lines;
    split_lines(content, lines);
    std::string out = "Path: " + path_display(*space, full) + "\nSize: " + str::format_count(size) + " bytes\nLines: " + std::to_string(lines.size()) + "\nExtension: " + (full.extension().string().empty() ? "(none)" : full.extension().string()) + "\n";
    return make_result(true, relative + " · " + str::format_count(size) + " bytes", out);
}

tool_result_t c_tool_registry::run_write_file(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    std::string content = normalize_newlines(args["content"].as_string(""));
    if (relative.empty())
        return make_result(false, "path is required", "");

    std::filesystem::path full = space->resolve(relative);
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    std::error_code error;
    bool existed = std::filesystem::exists(full, error);
    std::string before;
    if (existed)
        read_text_file(full, before);

    if (!write_text_file(full, content))
        return make_result(false, "failed to write " + relative, "");

    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written && (!existed || before != content))
        hooks.on_file_written(relative, before, content);

    int added = 0;
    int removed = 0;
    line_delta(before, content, added, removed);
    std::vector<std::string> new_lines;
    split_lines(content, new_lines);
    std::string note = existed ? "wrote" : "created";
    return make_result(true, note + " " + relative, str::format("%s %s (%zu lines, %zu bytes).", note.c_str(), relative.c_str(), new_lines.size(), content.size()), added, removed);
}

tool_result_t c_tool_registry::run_append_file(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    std::string content = normalize_newlines(args["content"].as_string(""));
    if (relative.empty() || content.empty())
        return make_result(false, "path and content are required", "");

    std::filesystem::path full = space->resolve(relative);
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative + ". Use write_file to create it.", "");

    std::string before;
    read_text_file(full, before);
    std::string updated = before;
    if (!updated.empty() && updated.back() != '\n')
        updated += "\n";
    updated += content;

    if (!write_text_file(full, updated))
        return make_result(false, "failed to write " + relative, "");

    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written)
        hooks.on_file_written(relative, before, updated);

    int added = 0;
    int removed = 0;
    line_delta(before, updated, added, removed);
    return make_result(true, "appended " + relative, str::format("Appended %zu bytes to %s.", content.size(), relative.c_str()), added, removed);
}

tool_result_t c_tool_registry::run_edit_file(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    std::string search = args["search"].as_string("");
    std::string replace = normalize_newlines(args["replace"].as_string(""));
    bool replace_all = args["replace_all"].as_bool(false);
    if (relative.empty() || search.empty())
        return make_result(false, "path and search are required", "");

    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    std::string content;
    if (!read_text_file(full, content))
        return make_result(false, "failed to read " + relative, "");

    size_t occurrences = 0;
    size_t position = 0;
    while ((position = content.find(search, position)) != std::string::npos)
    {
        ++occurrences;
        position += search.size();
    }
    if (occurrences == 0)
    {
        std::vector<std::string> search_lines;
        split_lines(search, search_lines);
        if (search_lines.size() > 1)
        {
            std::string trimmed_search = str::trim(search_lines.front());
            return make_result(false, "search text not found in " + relative, "Multi-line snippet starts with: \"" + str::truncate_middle(trimmed_search, 80) + "\". Make sure it matches the file byte-for-byte, including indentation.");
        }
        return make_result(false, "search text not found in " + relative, "Read the file again and copy the snippet exactly.");
    }
    if (occurrences > 1 && !replace_all)
        return make_result(false, str::format("search matches %zu times in %s", occurrences, relative.c_str()), "Extend the snippet to make it unique or pass replace_all=true.");

    std::string updated = content;
    if (replace_all)
        str::replace_all(updated, search, replace);
    else
        updated.replace(updated.find(search), search.size(), replace);

    if (!write_text_file(full, updated))
        return make_result(false, "failed to write " + relative, "");

    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written)
        hooks.on_file_written(relative, content, updated);

    int added = 0;
    int removed = 0;
    line_delta(content, updated, added, removed);
    std::string subtitle = (added || removed) ? str::format("edit %s · +%d -%d", relative.c_str(), added, removed) : "edit " + relative;
    return make_result(true, subtitle, str::format("Edited %s (%zu replacement%s).", relative.c_str(), occurrences, occurrences > 1 ? "s" : ""), added, removed);
}

tool_result_t c_tool_registry::run_replace_lines(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    int start_line = args["start_line"].as_int(0);
    int end_line = args["end_line"].as_int(0);
    std::string content = normalize_newlines(args["content"].as_string(""));
    if (relative.empty() || start_line < 1 || end_line < start_line)
        return make_result(false, "path, start_line and end_line are required", "");

    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    std::string before;
    if (!read_text_file(full, before))
        return make_result(false, "failed to read " + relative, "");
    std::vector<std::string> lines;
    split_lines(before, lines);
    if (start_line > static_cast<int>(lines.size()))
        return make_result(false, str::format("start_line %d is beyond end of file (%zu lines)", start_line, lines.size()), "");
    end_line = std::min(end_line, static_cast<int>(lines.size()));

    std::vector<std::string> replacement;
    split_lines(content, replacement);
    std::vector<std::string> updated;
    updated.reserve(lines.size() - static_cast<size_t>(end_line - start_line + 1) + replacement.size());
    for (int i = 1; i < start_line; ++i)
        updated.push_back(lines[static_cast<size_t>(i - 1)]);
    for (const std::string& line : replacement)
        updated.push_back(line);
    for (size_t i = static_cast<size_t>(end_line); i < lines.size(); ++i)
        updated.push_back(lines[i]);

    std::string after;
    for (size_t i = 0; i < updated.size(); ++i)
    {
        if (i)
            after += '\n';
        after += updated[i];
    }
    if (!before.empty() && before.back() == '\n' && !after.empty())
        after += "\n";

    if (!write_text_file(full, after))
        return make_result(false, "failed to write " + relative, "");
    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written)
        hooks.on_file_written(relative, before, after);

    int added = 0;
    int removed = 0;
    line_delta(before, after, added, removed);
    return make_result(true, str::format("rewrote %s:%d-%d · +%d -%d", relative.c_str(), start_line, end_line, added, removed), str::format("Replaced lines %d-%d of %s.", start_line, end_line, relative.c_str()), added, removed);
}

tool_result_t c_tool_registry::run_insert_lines(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    int line = args["line"].as_int(0);
    std::string content = normalize_newlines(args["content"].as_string(""));
    if (relative.empty() || line < 1 || content.empty())
        return make_result(false, "path, line and content are required", "");

    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    std::string before;
    if (!read_text_file(full, before))
        return make_result(false, "failed to read " + relative, "");
    std::vector<std::string> lines;
    split_lines(before, lines);
    if (line > static_cast<int>(lines.size()) + 1)
        return make_result(false, str::format("line %d is beyond end of file (%zu lines)", line, lines.size()), "");

    std::vector<std::string> insertion;
    split_lines(content, insertion);
    lines.insert(lines.begin() + static_cast<size_t>(line - 1), insertion.begin(), insertion.end());

    std::string after;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
            after += '\n';
        after += lines[i];
    }
    if (!before.empty() && before.back() == '\n')
        after += "\n";

    if (!write_text_file(full, after))
        return make_result(false, "failed to write " + relative, "");
    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written)
        hooks.on_file_written(relative, before, after);

    return make_result(true, str::format("insert %s:%d · +%zu lines", relative.c_str(), line, insertion.size()), str::format("Inserted %zu lines before line %d of %s.", insertion.size(), line, relative.c_str()), static_cast<int>(insertion.size()), 0);
}

tool_result_t c_tool_registry::run_delete_lines(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string relative = args["path"].as_string("");
    int start_line = args["start_line"].as_int(0);
    int end_line = args["end_line"].as_int(0);
    if (relative.empty() || start_line < 1 || end_line < start_line)
        return make_result(false, "path, start_line and end_line are required", "");

    std::filesystem::path full = space->resolve(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error))
        return make_result(false, "file not found: " + relative, "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace: " + relative, "");

    std::string before;
    if (!read_text_file(full, before))
        return make_result(false, "failed to read " + relative, "");
    std::vector<std::string> lines;
    split_lines(before, lines);
    if (start_line > static_cast<int>(lines.size()))
        return make_result(false, str::format("start_line %d is beyond end of file (%zu lines)", start_line, lines.size()), "");
    end_line = std::min(end_line, static_cast<int>(lines.size()));

    std::string removed_text;
    for (int i = start_line; i <= end_line; ++i)
        removed_text += lines[static_cast<size_t>(i - 1)] + "\n";
    lines.erase(lines.begin() + static_cast<size_t>(start_line - 1), lines.begin() + static_cast<size_t>(end_line));

    std::string after;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
            after += '\n';
        after += lines[i];
    }
    if (!before.empty() && before.back() == '\n' && !after.empty())
        after += "\n";

    if (!write_text_file(full, after))
        return make_result(false, "failed to write " + relative, "");
    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    if (hooks.on_file_written)
        hooks.on_file_written(relative, before, after);

    int count = end_line - start_line + 1;
    return make_result(true, str::format("trimmed %s · -%d lines", relative.c_str(), count), str::format("Deleted lines %d-%d of %s.", start_line, end_line, relative.c_str()), 0, count);
}

tool_result_t c_tool_registry::run_create_directory(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path full = space->resolve(args["path"].as_string(""));
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace", "");
    std::error_code error;
    std::filesystem::create_directories(full, error);
    if (error)
        return make_result(false, "failed to create directory: " + error.message(), "");
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    return make_result(true, "mkdir " + path_display(*space, full), "Created directory " + path_display(*space, full));
}

tool_result_t c_tool_registry::run_copy_path(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path from = space->resolve(args["from"].as_string(""));
    std::filesystem::path to = space->resolve(args["to"].as_string(""));
    if (!space->contains(from) || !space->contains(to))
        return make_result(false, "path escapes the workspace", "");
    std::error_code error;
    if (!std::filesystem::exists(from, error))
        return make_result(false, "source path not found", "");
    std::filesystem::create_directories(to.parent_path(), error);
    std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, error);
    if (error)
        return make_result(false, "copy failed: " + error.message(), "");
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    return make_result(true, "copied to " + path_display(*space, to), "Copied " + path_display(*space, from) + " -> " + path_display(*space, to));
}

tool_result_t c_tool_registry::run_delete_path(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path full = space->resolve(args["path"].as_string(""));
    if (full == space->root)
        return make_result(false, "refusing to delete the workspace root", "");
    if (!space->contains(full))
        return make_result(false, "path escapes the workspace", "");
    std::error_code error;
    if (!std::filesystem::exists(full, error))
        return make_result(false, "path not found", "");
    std::filesystem::remove_all(full, error);
    if (error)
        return make_result(false, "failed to delete: " + error.message(), "");
    file_cache.erase(full.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    return make_result(true, "deleted " + path_display(*space, full), "Deleted " + path_display(*space, full));
}

tool_result_t c_tool_registry::run_move_path(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::filesystem::path from = space->resolve(args["from"].as_string(""));
    std::filesystem::path to = space->resolve(args["to"].as_string(""));
    if (!space->contains(from) || !space->contains(to))
        return make_result(false, "path escapes the workspace", "");
    std::error_code error;
    if (!std::filesystem::exists(from, error))
        return make_result(false, "source path not found", "");
    std::filesystem::path parent = to.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, error);
    std::filesystem::rename(from, to, error);
    if (error)
    {
        std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, error);
        if (error)
            return make_result(false, "move failed: " + error.message(), "");
        std::filesystem::remove_all(from, error);
    }
    file_cache.erase(from.string());
    if (hooks.on_files_changed)
        hooks.on_files_changed();
    return make_result(true, path_display(*space, from) + " -> " + path_display(*space, to), "Moved " + path_display(*space, from) + " -> " + path_display(*space, to));
}

tool_result_t c_tool_registry::run_search_files(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string pattern = args["pattern"].as_string("");
    if (pattern.empty())
        return make_result(false, "pattern is required", "");
    std::filesystem::path base = args["path"].as_string("").empty() ? space->root : space->resolve(args["path"].as_string(""));

    std::string out;
    int found = 0;
    for (const std::filesystem::path& file : space->flat_files)
    {
        if (!file.string().starts_with(base.string()))
            continue;
        if (!glob_match(file.filename().string(), pattern))
            continue;
        out += path_display(*space, file) + "\n";
        if (++found >= file_search_limit)
        {
            out += "... (result limit reached)\n";
            break;
        }
    }
    if (found == 0)
        return make_result(true, "0 matches · " + pattern, "No files match pattern: " + pattern);
    return make_result(true, std::to_string(found) + " files · " + pattern, out);
}

tool_result_t c_tool_registry::run_search_text(const json_t& args)
{
    const c_workspace* space = active_workspace();
    std::string query = args["query"].as_string("");
    if (query.empty())
        return make_result(false, "query is required", "");
    bool use_regex = args["regex"].as_bool(false);
    bool case_sensitive = args["case_sensitive"].as_bool(false);
    std::filesystem::path base = args["path"].as_string("").empty() ? space->root : space->resolve(args["path"].as_string(""));

    std::regex expression;
    if (use_regex)
    {
        auto flags = std::regex::ECMAScript;
        if (!case_sensitive)
            flags |= std::regex::icase;
        try
        {
            expression.assign(query, flags);
        }
        catch (const std::regex_error&)
        {
            return make_result(false, "invalid regex: " + query, "");
        }
    }
    std::string needle = case_sensitive ? query : str::to_lower(query);

    std::string out;
    int found = 0;
    int scanned = 0;
    for (const std::filesystem::path& file : space->flat_files)
    {
        if (found >= search_hit_limit)
            break;
        if (scanned >= text_scan_file_limit)
            break;
        if (!file.string().starts_with(base.string()))
            continue;
        std::error_code error;
        if (std::filesystem::file_size(file, error) > text_file_size_limit)
            continue;
        ++scanned;
        std::string content;
        if (!read_text_file(file, content))
            continue;
        std::vector<std::string> lines;
        split_lines(content, lines);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            bool hit = false;
            if (use_regex)
                hit = std::regex_search(lines[i], expression);
            else if (case_sensitive)
                hit = lines[i].find(needle) != std::string::npos;
            else
                hit = str::to_lower(lines[i]).find(needle) != std::string::npos;
            if (!hit)
                continue;
            out += path_display(*space, file) + ":" + std::to_string(i + 1) + ": " + str::truncate_middle(str::trim(lines[i]), 160) + "\n";
            if (++found >= search_hit_limit)
            {
                out += "... (hit limit reached, refine the query)\n";
                break;
            }
        }
    }
    if (found == 0)
        return make_result(true, "0 matches · " + str::truncate_middle(query, 40), "No matches for: " + query);
    return make_result(true, std::to_string(found) + " matches · " + str::truncate_middle(query, 40), "Scanned " + std::to_string(scanned) + " files.\n\n" + out);
}

tool_result_t c_tool_registry::run_set_plan(const json_t& args)
{
    std::vector<plan_step_t> steps;
    if (args["steps"].is_array())
    {
        for (size_t i = 0; i < args["steps"].size(); ++i)
        {
            plan_step_t step;
            step.text = args["steps"].at(i).as_string("");
            if (!step.text.empty())
                steps.push_back(std::move(step));
        }
    }
    if (args["done"].is_array())
    {
        for (size_t i = 0; i < args["done"].size(); ++i)
        {
            int index = args["done"].at(i).as_int(-1);
            if (index >= 0 && index < static_cast<int>(steps.size()))
                steps[static_cast<size_t>(index)].done = true;
        }
    }
    std::string out;
    for (size_t i = 0; i < steps.size(); ++i)
        out += str::format("[%c] %zu. %s\n", steps[i].done ? 'x' : ' ', i + 1, steps[i].text.c_str());
    {
        std::lock_guard<std::mutex> guard(plan_mutex);
        plan_steps = steps;
    }
    if (steps.empty())
        return make_result(true, "plan cleared", "Plan cleared.");
    int done = 0;
    for (const plan_step_t& step : steps)
    {
        if (step.done)
            ++done;
    }
    return make_result(true, str::format("plan %d/%d", done, static_cast<int>(steps.size())), out);
}

tool_result_t c_tool_registry::execute_captured(const std::string& command, const std::string& interpreter, const json_t& args)
{
    if (hooks.commands_allowed && !hooks.commands_allowed())
        return make_result(false, "command execution is disabled", "Enable it in settings or do the change with file tools.");

    unsigned int timeout = static_cast<unsigned int>(std::clamp<long long>(args["timeout_ms"].as_int64(command_timeout_default_ms), 1000, command_timeout_max_ms));
    const c_workspace* space = active_workspace();
    std::string full_command = interpreter.empty() ? command : interpreter + " " + command;
    std::string output;
    int exit_code = 0;
    process_run_captured(full_command, space->root.string(), timeout, output, exit_code);
    std::string out = "$ " + full_command + "\n" + (output.empty() ? "(no output)" : output) + "\n[exit code: " + std::to_string(exit_code) + "]";
    return make_result(exit_code == 0, str::format("exit %d · %s", exit_code, str::truncate_middle(full_command, 48).c_str()), out);
}

tool_result_t c_tool_registry::run_run_command(const json_t& args)
{
    std::string command = args["command"].as_string("");
    if (command.empty())
        return make_result(false, "command is required", "");
    return execute_captured(command, "", args);
}

tool_result_t c_tool_registry::run_powershell(const json_t& args)
{
    std::string script = args["script"].as_string("");
    if (script.empty())
        return make_result(false, "script is required", "");

    if (hooks.commands_allowed && !hooks.commands_allowed())
        return make_result(false, "command execution is disabled", "Enable it in settings or do the change with file tools.");

    std::string script_path = shell::appdata_dir() + "\\agent_command.ps1";
    std::ofstream file(std::filesystem::path(shell::to_wide(script_path)), std::ios::binary | std::ios::trunc);
    if (!file.good())
        return make_result(false, "failed to write the script file", "");
    file << "\xEF\xBB\xBF";
    std::string flat;
    for (char symbol : script)
    {
        if (symbol != '\r')
            flat.push_back(symbol);
    }
    std::string normalized;
    for (char symbol : flat)
    {
        if (symbol == '\n')
            normalized += "\r\n";
        else
            normalized.push_back(symbol);
    }
    file.write(normalized.data(), static_cast<std::streamsize>(normalized.size()));
    file.close();

    unsigned int timeout = static_cast<unsigned int>(std::clamp<long long>(args["timeout_ms"].as_int64(command_timeout_default_ms), 1000, command_timeout_max_ms));
    const c_workspace* space = active_workspace();
    std::string full_command = "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script_path + "\"";
    std::string output;
    int exit_code = 0;
    process_run_captured(full_command, space->root.string(), timeout, output, exit_code);
    std::string out = "$ " + full_command + "\n" + (output.empty() ? "(no output)" : output) + "\n[exit code: " + std::to_string(exit_code) + "]";
    return make_result(exit_code == 0, str::format("exit %d · powershell", exit_code), out);
}
