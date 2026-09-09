#include "workspace.hpp"

#include <algorithm>

namespace
{
    constexpr int scan_depth_limit = 14;
    constexpr long long scan_entry_limit = 60000;
    constexpr long long flat_file_limit = 30000;

    const char* ignored_directories[] = {
        ".git", ".svn", ".hg", ".vs", ".vscode", ".idea", "__pycache__",
        "node_modules", "Debug", "Release", "x64", "Win32", ".xcode",
        "target", "dist", "out", "build", "bin", "obj", ".venv", "venv",
        ".gradle", ".cache", "site-packages", ".next", ".nuxt", "vendor\\imgui"
    };

    const char* ignored_extensions[] = {
        ".obj", ".o", ".lib", ".a", ".pdb", ".ilk", ".exp", ".dll", ".exe",
        ".bin", ".so", ".dylib", ".class", ".jar", ".pyc", ".pyo", ".wasm",
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp", ".tga",
        ".psd", ".ttf", ".otf", ".woff", ".woff2", ".eot", ".mp3", ".mp4",
        ".avi", ".mkv", ".wav", ".ogg", ".flac", ".zip", ".rar", ".7z",
        ".tar", ".gz", ".iso", ".db", ".sqlite", ".dll.pdb"
    };
}

bool c_workspace::is_ignored_name(const std::string& name)
{
    if (name.empty())
        return true;
    if (name == "." || name == "..")
        return true;
    for (const char* ignored : ignored_directories)
    {
        if (name == ignored)
            return true;
    }
    return false;
}

std::string c_workspace::extension_of(const std::string& name)
{
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == name.size() - 1)
        return {};
    std::string extension = name.substr(dot);
    for (char& symbol : extension)
    {
        if (symbol >= 'A' && symbol <= 'Z')
            symbol += 'a' - 'A';
    }
    return extension;
}

void c_workspace::set_root(const std::filesystem::path& path)
{
    root = path;
    valid = !path.empty();
    rescan();
}

void c_workspace::close()
{
    root.clear();
    valid = false;
    tree = workspace_node_t{};
    flat_files.clear();
    file_count = 0;
    total_bytes = 0;
}

void c_workspace::rescan()
{
    tree = workspace_node_t{};
    flat_files.clear();
    file_count = 0;
    total_bytes = 0;
    if (!valid)
        return;

    tree.name = root.filename().string();
    if (tree.name.empty())
        tree.name = root.string();
    tree.path = root;
    tree.directory = true;
    tree.expanded = true;
    tree.loaded = true;
    scan_node(tree, 0);

    std::sort(flat_files.begin(), flat_files.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
        std::string left_name = left.string();
        std::string right_name = right.string();
        for (size_t i = 0; i < left_name.size() && i < right_name.size(); ++i)
        {
            char a = left_name[i] >= 'A' && left_name[i] <= 'Z' ? static_cast<char>(left_name[i] + 32) : left_name[i];
            char b = right_name[i] >= 'A' && right_name[i] <= 'Z' ? static_cast<char>(right_name[i] + 32) : right_name[i];
            if (a != b)
                return a < b;
        }
        return left_name.size() < right_name.size();
    });
}

void c_workspace::scan_node(workspace_node_t& node, int depth)
{
    std::error_code error;
    std::filesystem::directory_iterator iterator(node.path, std::filesystem::directory_options::skip_permission_denied, error);
    if (error)
        return;

    for (const std::filesystem::directory_entry& entry : iterator)
    {
        if (file_count >= scan_entry_limit)
            return;
        std::string name = entry.path().filename().string();
        if (is_ignored_name(name))
            continue;

        bool directory = entry.is_directory(error);
        workspace_node_t child;
        child.name = name;
        child.path = entry.path();
        child.directory = directory;
        node.children.push_back(std::move(child));
        ++file_count;

        if (directory)
        {
            continue;
        }

        std::string extension = extension_of(name);
        bool binary = false;
        for (const char* ignored : ignored_extensions)
        {
            if (extension == ignored)
            {
                binary = true;
                break;
            }
        }
        if (!binary && flat_files.size() < static_cast<size_t>(flat_file_limit))
            flat_files.push_back(entry.path());

        unsigned long long size = static_cast<unsigned long long>(entry.file_size(error));
        total_bytes += size;
    }

    std::sort(node.children.begin(), node.children.end(), [](const workspace_node_t& left, const workspace_node_t& right) {
        if (left.directory != right.directory)
            return left.directory;
        std::string left_name = left.name;
        std::string right_name = right.name;
        for (size_t i = 0; i < left_name.size() && i < right_name.size(); ++i)
        {
            char a = left_name[i] >= 'A' && left_name[i] <= 'Z' ? static_cast<char>(left_name[i] + 32) : left_name[i];
            char b = right_name[i] >= 'A' && right_name[i] <= 'Z' ? static_cast<char>(right_name[i] + 32) : right_name[i];
            if (a != b)
                return a < b;
        }
        return left_name.size() < right_name.size();
    });

    if (depth >= scan_depth_limit)
        return;
    for (workspace_node_t& child : node.children)
    {
        if (child.directory)
        {
            child.loaded = true;
            scan_node(child, depth + 1);
        }
    }
}

void c_workspace::load_children(workspace_node_t& node)
{
    if (node.loaded || !node.directory)
        return;
    node.loaded = true;
    scan_node(node, 1);
    for (workspace_node_t& child : node.children)
    {
        if (child.directory)
            child.loaded = true;
    }
}

workspace_node_t* c_workspace::find_node(workspace_node_t& node, const std::filesystem::path& path)
{
    if (!node.directory)
        return nullptr;
    std::error_code error;
    if (!std::filesystem::equivalent(node.path, path, error))
        return nullptr;
    return &node;
}

workspace_node_t* c_workspace::find_node_recursive(workspace_node_t& node, const std::filesystem::path& path)
{
    if (!node.directory)
        return nullptr;
    std::error_code error;
    if (std::filesystem::equivalent(node.path, path, error) && !error)
        return &node;
    std::string path_text = path.string();
    for (workspace_node_t& child : node.children)
    {
        if (!child.directory)
        {
            if (child.path == path)
                return &child;
            continue;
        }
        std::string child_text = child.path.string();
        if (path_text.size() > child_text.size() && path_text.starts_with(child_text) && (path_text[child_text.size()] == '\\' || path_text[child_text.size()] == '/'))
        {
            if (workspace_node_t* hit = find_node_recursive(child, path))
                return hit;
        }
    }
    return nullptr;
}

bool c_workspace::contains(const std::filesystem::path& path) const
{
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty())
        return false;
    std::string text = relative.string();
    return text != ".." && text.find("..") != 0;
}

std::filesystem::path c_workspace::resolve(const std::string& relative) const
{
    std::filesystem::path candidate = std::filesystem::path(relative);
    if (candidate.is_absolute())
        return candidate.lexically_normal();
    return (root / candidate).lexically_normal();
}

std::string c_workspace::relative_to_root(const std::filesystem::path& path) const
{
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty())
        return path.string();
    return relative.string();
}

std::string c_workspace::display_name() const
{
    if (!valid)
        return "no workspace";
    std::string name = root.filename().string();
    return name.empty() ? root.string() : name;
}
