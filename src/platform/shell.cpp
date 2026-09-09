#include "shell.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace shell
{
    std::wstring to_wide(std::string_view text)
    {
        if (text.empty())
            return {};
        int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
        return out;
    }

    std::string to_utf8(const std::wstring& text)
    {
        if (text.empty())
            return {};
        int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }

    bool pick_folder(std::string& out_utf8)
    {
        bool success = false;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            if (SUCCEEDED(dialog->Show(nullptr)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        out_utf8 = to_utf8(path);
                        CoTaskMemFree(path);
                        success = true;
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
        return success;
    }

    std::string appdata_dir()
    {
        PWSTR known_path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &known_path)))
            return {};
        std::filesystem::path directory(known_path);
        CoTaskMemFree(known_path);
        directory /= L"amyrIDE";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return to_utf8(directory.wstring());
    }

    std::string executable_dir()
    {
        wchar_t buffer[MAX_PATH];
        unsigned long size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        std::filesystem::path directory(std::wstring(buffer, size));
        directory.remove_filename();
        return to_utf8(directory.wstring());
    }

    void open_in_explorer(const std::string& path_utf8)
    {
        std::wstring wide = to_wide(path_utf8);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        PIDLIST_ABSOLUTE list = ILCreateFromPathW(wide.c_str());
        if (list)
        {
            SHOpenFolderAndSelectItems(list, 0, nullptr, 0);
            ILFree(list);
        }
        CoUninitialize();
    }

    void open_url(const std::string& url_utf8)
    {
        std::wstring wide = to_wide(url_utf8);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        CoUninitialize();
    }

    bool path_exists(const std::string& path_utf8)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(to_wide(path_utf8)), error);
    }

    bool window_is_maximized(void* hwnd)
    {
        return hwnd != nullptr && IsZoomed(static_cast<HWND>(hwnd)) != 0;
    }

    void window_send_command(void* hwnd, unsigned long command)
    {
        if (hwnd)
            SendMessageW(static_cast<HWND>(hwnd), WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
    }

    float window_dpi_scale(void* hwnd)
    {
        if (!hwnd)
            return 1.0f;
        unsigned int dpi = GetDpiForWindow(static_cast<HWND>(hwnd));
        if (dpi == 0)
            dpi = USER_DEFAULT_SCREEN_DPI;
        return static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    }
}
