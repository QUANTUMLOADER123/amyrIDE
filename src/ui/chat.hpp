#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_client.hpp"
#include "ui/markdown.hpp"

struct tool_card_state_t
{
    bool expanded = false;
    float appear_time = 0.0f;
};

struct message_view_t
{
    float born_time = 0.0f;
    float reveal_bytes = 0.0f;
};

class c_theme;
class c_ide_app;

class c_chat_panel
{
public:
    void render(c_ide_app& app);
    void attach_context_file(const std::string& path);
    void draft_message(const std::string& text);

private:
    void render_history(c_ide_app& app);
    void render_plan_strip(c_ide_app& app);
    void render_composer(c_ide_app& app);
    void render_message(c_ide_app& app, const std::vector<message_t>& history, int message_index, float wrap_width, float ai_width);
    void render_tool_card(c_ide_app& app, const tool_call_t& call, int message_index, int call_index, bool running, const std::string* output, float row_width, bool in_bubble);
    std::string build_context_block(c_ide_app& app) const;

    std::string input;
    float below_height = 0.0f;
    bool input_focus_requested = false;
    bool autoscroll = true;
    int editing_index = -1;
    bool plan_open = false;
    float last_stream_size = 0.0f;
    std::vector<std::string> attached_files;
    std::unordered_map<int, message_view_t> message_views;
    std::unordered_map<std::string, tool_card_state_t> tool_cards;
};
