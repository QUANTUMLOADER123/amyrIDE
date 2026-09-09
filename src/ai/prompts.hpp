#pragma once

#include <string>

class c_workspace;
struct editor_summary_t;

std::string build_system_prompt(const c_workspace* workspace, const std::string& open_files, const std::string& active_file);
std::string build_compaction_prompt(const std::string& transcript);
