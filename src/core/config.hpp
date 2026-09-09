#pragma once

#include <string>
#include <vector>

inline constexpr const char* model_list[] = {
    "MiniMax-M3", "antigravity", "apicn/DeepSeek-V4-Flash",
    "apicn/DeepSeek-V4-PRO", "apicn/Kimi-K2.6", "auto/claude",
    "claude-fable-5", "claude-haiku-4-5", "claude-opus-4-1",
    "claude-opus-4-5-20251101", "claude-opus-4.8", "claude-opus-5",
    "claude-sonnet-4-5", "claude-sonnet-4-6", "claude-sonnet-5",
    "deepseek-v4-flash", "gemini-2.5-flash", "gemini-2.5-flash-lite",
    "gemini-3-flash", "gemini-3-flash-preview", "gemini-3.1-flash-lite",
    "gemini-3.5-flash", "gemini-3.6-flash", "gemma-4-26b",
    "gpt-5", "gpt-5-mini", "gpt-5-nano", "gpt-5.1", "gpt-5.2",
    "gpt-5.3-codex", "gpt-5.4", "gpt-5.4-mini", "gpt-5.4-nano",
    "gpt-5.5", "gpt-5.6", "gpt-5.6-luna", "gpt-5.6-luna-max",
    "gpt-5.6-pro", "gpt-5.6-sol", "gpt-5.6-terra",
    "kat-coder-pro-v2", "kimi-k3", "kr/claude-sonnet-4.5-agentic",
    "kr/minimax-m2.1", "mcode/mimo-auto", "minimax-m2.7",
    "minimax-m3", "oc/deepseek-v4-flash", "oc/deepseek-v4-flash-free",
    "opus-4-8", "opus-5", "ox-alpha", "stealth/ox-alpha",
    "step-3.5-flash", "step-3.5-flash-2603", "step-3.7-flash",
    "step-image-edit-2", "step-router-v1", "stepaudio-2.5-asr",
    "stepaudio-2.5-chat", "stepaudio-2.5-realtime", "stepaudio-2.5-tts"
};
inline constexpr int model_list_size = static_cast<int>(sizeof(model_list) / sizeof(model_list[0]));

inline constexpr const char* palette_names[] = { "Sky", "Periwinkle", "Aqua", "Lavender", "Mist", "Powder" };
inline constexpr int palette_count = static_cast<int>(sizeof(palette_names) / sizeof(palette_names[0]));

struct accent_t
{
    float r, g, b;
};

inline constexpr accent_t accent_presets[palette_count] = {
    { 0.612f, 0.780f, 0.961f },
    { 0.647f, 0.690f, 0.961f },
    { 0.561f, 0.847f, 0.910f },
    { 0.753f, 0.659f, 0.941f },
    { 0.722f, 0.804f, 0.898f },
    { 0.659f, 0.804f, 0.925f }
};

class c_config
{
public:
    std::string endpoint_url = "https://amyr-ai.duckdns.org";
    std::string api_key = "sk-YdjEBcC4vzpk6KYfQWWJl5YNOfij402j";
    std::string model = "claude-sonnet-4-6";

    int context_limit = 150000;
    int response_reserve = 16384;
    int keep_recent_messages = 8;
    int max_tool_output = 8000;
    bool auto_compact = true;

    bool allow_commands = true;
    bool confirm_edits = false;

    int accent_index = 0;
    float font_size = 17.0f;
    bool animations = true;

    bool skip_tls_verify = false;

    std::string workspace_path;
    std::vector<std::string> open_tabs;
    int active_tab = 0;

    std::string completion_url() const;
    std::string models_url() const;
    std::string session_path() const;
    std::string config_path() const;
    std::string layout_path() const;

    void load();
    void save() const;
};
