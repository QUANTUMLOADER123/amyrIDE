#include "config.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "platform/shell.hpp"

std::string c_config::completion_url() const
{
    std::string base = endpoint_url;
    while (!base.empty() && (base.back() == '/' || base.back() == ' '))
        base.pop_back();
    return base + "/v1/chat/completions";
}

std::string c_config::models_url() const
{
    std::string base = endpoint_url;
    while (!base.empty() && (base.back() == '/' || base.back() == ' '))
        base.pop_back();
    return base + "/v1/models";
}

std::string c_config::session_path() const
{
    return shell::appdata_dir() + "\\session.json";
}

std::string c_config::config_path() const
{
    return shell::appdata_dir() + "\\config.json";
}

std::string c_config::layout_path() const
{
    return shell::appdata_dir() + "\\layout.ini";
}

void c_config::load()
{
    std::ifstream file(config_path(), std::ios::binary);
    if (!file.good())
        return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    json_t data;
    if (!json_t::parse(buffer.str(), data))
        return;

    endpoint_url = data["endpoint_url"].as_string(endpoint_url);
    api_key = data["api_key"].as_string(api_key);
    model = data["model"].as_string(model);
    context_limit = data["context_limit"].as_int(context_limit);
    response_reserve = data["response_reserve"].as_int(response_reserve);
    keep_recent_messages = data["keep_recent_messages"].as_int(keep_recent_messages);
    max_tool_output = data["max_tool_output"].as_int(max_tool_output);
    auto_compact = data["auto_compact"].as_bool(auto_compact);
    allow_commands = data["allow_commands"].as_bool(allow_commands);
    confirm_edits = data["confirm_edits"].as_bool(confirm_edits);
    accent_index = data["accent_index"].as_int(accent_index);
    font_size = static_cast<float>(data["font_size"].as_float(font_size));
    animations = data["animations"].as_bool(animations);
    skip_tls_verify = data["skip_tls_verify"].as_bool(skip_tls_verify);
    workspace_path = data["workspace_path"].as_string("");

    open_tabs.clear();
    if (const json_t* tabs = data.find("open_tabs"))
    {
        for (size_t i = 0; i < tabs->size(); ++i)
            open_tabs.push_back(tabs->at(i).as_string(""));
    }
    active_tab = data["active_tab"].as_int(0);

    if (context_limit < 4096)
        context_limit = 4096;
    if (accent_index < 0 || accent_index >= palette_count)
        accent_index = 0;
    if (font_size < 12.0f)
        font_size = 12.0f;
    if (font_size > 28.0f)
        font_size = 28.0f;
}

void c_config::save() const
{
    json_t tabs = json_t::array();
    for (const std::string& tab : open_tabs)
        tabs.push(json_t(tab));

    json_t data = json_t::object_t{
        { "endpoint_url", endpoint_url },
        { "api_key", api_key },
        { "model", model },
        { "context_limit", context_limit },
        { "response_reserve", response_reserve },
        { "keep_recent_messages", keep_recent_messages },
        { "max_tool_output", max_tool_output },
        { "auto_compact", auto_compact },
        { "allow_commands", allow_commands },
        { "confirm_edits", confirm_edits },
        { "accent_index", accent_index },
        { "font_size", font_size },
        { "animations", animations },
        { "skip_tls_verify", skip_tls_verify },
        { "workspace_path", workspace_path },
        { "open_tabs", std::move(tabs) },
        { "active_tab", active_tab }
    };

    std::ofstream file(config_path(), std::ios::binary | std::ios::trunc);
    if (file.good())
        file << data.dump(true);
}
