#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/json.hpp"
#include "core/workspace.hpp"

struct tool_result_t
{
    bool ok = true;
    std::string content;
    std::string summary_line;
};

struct plan_step_t
{
    bool done = false;
    std::string text;
};

struct tool_hooks_t
{
    std::function<const c_workspace*()> workspace;
    std::function<int()> context_generation;
    std::function<void()> on_files_changed;
    std::function<void(const std::string& path, const std::string& before, const std::string& after)> on_file_written;
    std::function<bool()> commands_allowed;
};

class c_tool_registry
{
public:
    explicit c_tool_registry(tool_hooks_t hooks);

    tool_result_t execute(const std::string& name, const std::string& arguments_json);
    json_t build_tools_schema() const;

    tool_hooks_t hooks;

    std::vector<plan_step_t> plan_snapshot() const;
    int plan_done_count() const;

private:
    struct file_cache_entry_t
    {
        long long size = 0;
        int generation = -1;
    };

    tool_result_t run_list_files(const json_t& args);
    tool_result_t run_list_dir(const json_t& args);
    tool_result_t run_workspace_info(const json_t& args);
    tool_result_t run_read_file(const json_t& args);
    tool_result_t run_file_info(const json_t& args);
    tool_result_t run_write_file(const json_t& args);
    tool_result_t run_append_file(const json_t& args);
    tool_result_t run_edit_file(const json_t& args);
    tool_result_t run_replace_lines(const json_t& args);
    tool_result_t run_insert_lines(const json_t& args);
    tool_result_t run_delete_lines(const json_t& args);
    tool_result_t run_create_directory(const json_t& args);
    tool_result_t run_copy_path(const json_t& args);
    tool_result_t run_delete_path(const json_t& args);
    tool_result_t run_move_path(const json_t& args);
    tool_result_t run_search_files(const json_t& args);
    tool_result_t run_search_text(const json_t& args);
    tool_result_t run_set_plan(const json_t& args);
    tool_result_t run_run_command(const json_t& args);
    tool_result_t run_powershell(const json_t& args);

    const c_workspace* active_workspace() const { return hooks.workspace ? hooks.workspace() : nullptr; }
    std::string truncate_output(std::string text) const;
    tool_result_t execute_captured(const std::string& command, const std::string& interpreter, const json_t& args);

    std::unordered_map<std::string, file_cache_entry_t> file_cache;
    mutable std::mutex plan_mutex;
    std::vector<plan_step_t> plan_steps;
};
