#pragma once

#include <string>
#include <string_view>

namespace shell
{
    bool pick_folder(std::string& out_utf8);
    std::string appdata_dir();
    std::string executable_dir();
    std::wstring to_wide(std::string_view text);
    std::string to_utf8(const std::wstring& text);
    void open_in_explorer(const std::string& path_utf8);
    void open_url(const std::string& url_utf8);
    bool path_exists(const std::string& path_utf8);
    bool window_is_maximized(void* hwnd);
    void window_send_command(void* hwnd, unsigned long command);
    float window_dpi_scale(void* hwnd);
}
