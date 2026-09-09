#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <misc/cpp/imgui_stdlib.h>

#include "app.hpp"
#include "platform/shell.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace
{
    constexpr unsigned long window_min_width = 980;
    constexpr unsigned long window_min_height = 620;
    constexpr unsigned long hit_test_border = 8;
    constexpr unsigned long title_bar_height_logical = 44;

    c_ide_app* g_app = nullptr;
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swap_chain = nullptr;
    ID3D11RenderTargetView* g_render_target = nullptr;
    bool g_running = true;
}

namespace
{
    void create_render_target()
    {
        ID3D11Texture2D* back_buffer = nullptr;
        g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (back_buffer)
        {
            g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
            back_buffer->Release();
        }
    }

    void release_render_target()
    {
        if (g_render_target)
        {
            g_render_target->Release();
            g_render_target = nullptr;
        }
    }

    void resize_swap_chain(unsigned int width, unsigned int height)
    {
        release_render_target();
        g_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        create_render_target();
    }

    void apply_window_rounding(HWND window)
    {
        RECT frame{};
        if (!GetClientRect(window, &frame))
            return;
        HRGN region = CreateRoundRectRgn(0, 0, frame.right + 1, frame.bottom + 1, 22, 22);
        SetWindowRgn(window, region, TRUE);
    }
}

static LRESULT WINAPI window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return true;

    switch (message)
    {
    case WM_NCCALCSIZE:
    {
        if (!wparam)
            break;
        RECT* client_rect = reinterpret_cast<RECT*>(lparam);
        if (IsZoomed(window))
        {
            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            if (GetMonitorInfoW(monitor, &monitor_info))
                *client_rect = monitor_info.rcWork;
        }
        return 0;
    }
    case WM_NCHITTEST:
    {
        POINT cursor{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(window, &cursor);
        RECT client{};
        GetClientRect(window, &client);
        unsigned int dpi = GetDpiForWindow(window);
        unsigned int border = MulDiv(hit_test_border, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);

        bool left = cursor.x < static_cast<long>(border);
        bool right = cursor.x >= client.right - static_cast<long>(border);
        bool top = cursor.y < static_cast<long>(border);
        bool bottom = cursor.y >= client.bottom - static_cast<long>(border);
        bool maximized = IsZoomed(window) != 0;

        if (!maximized)
        {
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
        }

        unsigned int dpi_title = MulDiv(title_bar_height_logical, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        if (cursor.y < static_cast<long>(dpi_title))
        {
            if (g_app && g_app->titlebar_hit(static_cast<float>(cursor.x), static_cast<float>(cursor.y)))
                return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_NCACTIVATE:
        return DefWindowProcW(window, message, wparam, -1);
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = window_min_width;
        info->ptMinTrackSize.y = window_min_height;
        return 0;
    }
    case WM_DPICHANGED:
    {
        RECT* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES:
    {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        unsigned int dropped_count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (unsigned int index = 0; index < dropped_count && g_app; ++index)
        {
            wchar_t path[MAX_PATH]{};
            if (DragQueryFileW(drop, index, path, MAX_PATH) == 0)
                continue;
            std::filesystem::path dropped(path);
            if (index == 0 && std::filesystem::is_directory(dropped))
                g_app->set_workspace(shell::to_utf8(dropped.wstring()));
            else
                g_app->attach_dropped_file(shell::to_utf8(dropped.wstring()));
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SIZE:
    {
        if (wparam == SIZE_MINIMIZED || !g_swap_chain)
            break;
        resize_swap_chain(LOWORD(lparam), HIWORD(lparam));
        apply_window_rounding(window);
        return 0;
    }
    case WM_SYSCOMMAND:
    {
        if ((wparam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    }
    case WM_DESTROY:
    {
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hIconSm = window_class.hIcon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"amyrIDE_window";
    RegisterClassExW(&window_class);

    int window_width = 1560;
    int window_height = 920;
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    int position_x = work_area.left + ((work_area.right - work_area.left) - window_width) / 2;
    int position_y = work_area.top + ((work_area.bottom - work_area.top) - window_height) / 2;

    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"amyrIDE", WS_OVERLAPPEDWINDOW, position_x, position_y, window_width, window_height, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 1;

    BOOL rounded = TRUE;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &rounded, sizeof(rounded));
    BOOL dark_title = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_title, sizeof(dark_title));
    DragAcceptFiles(window, TRUE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.OutputWindow = window;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    unsigned int create_flags = 0;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    HRESULT device_result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags, &feature_level, 1, D3D11_SDK_VERSION, &swap_chain_desc, &g_swap_chain, &g_device, nullptr, &g_context);
    if (FAILED(device_result))
    {
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        HRESULT retry = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &feature_level, 1, D3D11_SDK_VERSION, &swap_chain_desc, &g_swap_chain, &g_device, nullptr, &g_context);
        if (FAILED(retry))
            return 1;
    }
    create_render_target();
    ShowWindow(window, show_command);
    UpdateWindow(window);
    apply_window_rounding(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    static std::string layout_path = shell::appdata_dir() + "\\layout.ini";
    io.IniFilename = layout_path.c_str();

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_context);

    static c_ide_app app;
    g_app = &app;
    app.initialize(window);

    MSG message{};
    while (g_running)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                g_running = false;
        }
        if (!g_running)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.update();
        ImGui::Render();

        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
        g_context->ClearRenderTargetView(g_render_target, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap_chain->Present(1, 0);
    }

    app.shutdown();
    Sleep(180);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_render_target();
    if (g_swap_chain)
        g_swap_chain->Release();
    if (g_context)
        g_context->Release();
    if (g_device)
        g_device->Release();
    CoUninitialize();
    std::_Exit(0);
    return 0;
}
