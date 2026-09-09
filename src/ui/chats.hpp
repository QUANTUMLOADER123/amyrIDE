#pragma once

#include <string>

class c_ide_app;

class c_chats_overlay
{
public:
    bool visible = false;

    void draw(c_ide_app& app);

private:
    char search[128] = {};
    std::string selected_project;
};
