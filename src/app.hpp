#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "ai/ai_client.hpp"
#include "ai/tools.hpp"
#include "core/config.hpp"
#include "core/workspace.hpp"
#include "ui/chat.hpp"
#include "ui/settings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

struct chat_meta_t
{
    std::string id;
    std::string title;
};

class c_ide_app
{
public:
    c_ide_app();

    void initialize(void* hwnd);
    void shutdown();
    void update();
    void update(float dt);

    void open_workspace_dialog();
    void set_workspace(const std::string& path);
    std::vector<chat_meta_t>& chats();
    std::string active_chat() const { return active_chat_id; }
    void new_chat();
    void open_chat(const std::string& id);
    void delete_chat(const std::string& id);
    void attach_dropped_file(const std::string& path);
    void clear_conversation();
    void export_conversation();
    void save_session();
    void load_session();
    void apply_code_block(const std::string& path, const std::string& code);
    std::string read_file_for_context(const std::string& path);
    void send_chat_message(const std::string& text, const std::string& context_block = "");
    void on_files_changed_external();

    c_config config;
    c_theme theme;
    c_workspace workspace;
    c_chat_panel chat;
    c_settings_panel settings;
    c_toasts toasts;
    c_tool_registry tools;
    c_ai_client ai;

    float startup_time = 0.0f;

private:
    std::string chats_dir() const;
    std::string chat_file(const std::string& id) const;
    void scan_chats();
    void load_last_chat();

    std::vector<chat_meta_t> chat_list;
    std::string active_chat_id;

    void draw_background();
    void draw_welcome();
    void draw_assistant();
    void draw_titlebar(void* hwnd);
    void draw_statusbar();
    void handle_global_shortcuts(void* hwnd);

    std::atomic<bool> files_changed_flag{ false };
    void* window_handle = nullptr;
};
