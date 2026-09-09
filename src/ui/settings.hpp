#pragma once

#include <string>

class c_theme;
class c_ide_app;

class c_settings_panel
{
public:
    void draw(c_ide_app& app);
    void open();
    bool visible = false;

private:
    void render_connection(c_ide_app& app);
    void render_context(c_ide_app& app);
    void render_appearance(c_ide_app& app);
    void render_tools(c_ide_app& app);

    char endpoint_buffer[256] = {};
    char key_buffer[256] = {};
    bool show_key = false;
    std::string connection_status;
    float connection_status_time = -100.0f;
    bool testing_connection = false;
};
