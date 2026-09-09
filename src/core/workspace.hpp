#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct workspace_node_t
{
    std::string name;
    std::filesystem::path path;
    bool directory = false;
    bool expanded = false;
    bool loaded = false;
    std::vector<workspace_node_t> children;
};

class c_workspace
{
public:
    std::filesystem::path root;
    bool valid = false;
    workspace_node_t tree;
    std::vector<std::filesystem::path> flat_files;
    long long file_count = 0;
    unsigned long long total_bytes = 0;

    void set_root(const std::filesystem::path& path);
    void close();
    void rescan();
    void load_children(workspace_node_t& node);
    workspace_node_t* find_node(const std::filesystem::path& path) { return find_node_recursive(tree, path); }

    bool contains(const std::filesystem::path& path) const;
    std::filesystem::path resolve(const std::string& relative) const;
    std::string relative_to_root(const std::filesystem::path& path) const;
    std::string display_name() const;

    static bool is_ignored_name(const std::string& name);
    static std::string extension_of(const std::string& name);

private:
    workspace_node_t* find_node(workspace_node_t& node, const std::filesystem::path& path);
    workspace_node_t* find_node_recursive(workspace_node_t& node, const std::filesystem::path& path);
    void scan_node(workspace_node_t& node, int depth);
};
