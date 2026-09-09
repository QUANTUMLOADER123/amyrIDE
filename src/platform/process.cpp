#include "process.hpp"

#include <windows.h>

#include "core/string.hpp"

namespace
{
    constexpr unsigned int captured_poll_interval_ms = 30;
    constexpr size_t captured_output_limit = 262144;

    std::wstring to_wide(std::string_view text)
    {
        if (text.empty())
            return {};
        int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
        return out;
    }

    std::string from_console_bytes(const std::string& bytes)
    {
        if (bytes.empty())
            return {};
        int needed = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), needed);
        needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }
}

struct c_process::impl_t
{
    HANDLE child = nullptr;
    HANDLE stdout_read = nullptr;
    unsigned long exit_code = 0;

    ~impl_t()
    {
        if (child)
        {
            TerminateProcess(child, 1);
            WaitForSingleObject(child, 2000);
            CloseHandle(child);
            child = nullptr;
        }
        if (stdout_read)
        {
            CloseHandle(stdout_read);
            stdout_read = nullptr;
        }
    }
};

c_process::c_process()
    : impl(std::make_unique<impl_t>())
{
}

c_process::~c_process() = default;

bool c_process::launch(const std::string& command, const std::string& working_directory)
{
    terminate();

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0))
        return false;
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write, &security, 0))
    {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return false;
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stdout_write;
    startup.hStdInput = stdin_read;

    std::wstring command_line = L"cmd.exe /c " + to_wide(command);
    std::wstring wide_directory = to_wide(working_directory);

    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, wide_directory.empty() ? nullptr : wide_directory.c_str(), &startup, &process);

    CloseHandle(stdout_write);
    CloseHandle(stdin_read);
    CloseHandle(stdin_write);

    if (!created)
    {
        CloseHandle(stdout_read);
        return false;
    }

    CloseHandle(process.hThread);
    impl->child = process.hProcess;
    impl->stdout_read = stdout_read;
    impl->exit_code = 0;
    return true;
}

void c_process::poll_output(std::string& out_bytes)
{
    out_bytes.clear();
    if (!impl->stdout_read)
        return;

    char buffer[8192];
    unsigned long read_bytes = 0;
    while (PeekNamedPipe(impl->stdout_read, nullptr, 0, nullptr, &read_bytes, nullptr) && read_bytes > 0)
    {
        unsigned long received = 0;
        unsigned long chunk = read_bytes < sizeof(buffer) ? read_bytes : sizeof(buffer);
        if (!ReadFile(impl->stdout_read, buffer, chunk, &received, nullptr) || received == 0)
            break;
        out_bytes.append(buffer, received);
        if (out_bytes.size() > captured_output_limit)
            break;
    }
    out_bytes = from_console_bytes(out_bytes);
}

bool c_process::alive() const
{
    if (!impl->child)
        return false;
    unsigned long code = 0;
    if (!GetExitCodeProcess(impl->child, &code))
        return false;
    return code == STILL_ACTIVE;
}

unsigned long c_process::exit_code() const
{
    if (!impl->child)
        return 0;
    unsigned long code = 0;
    if (GetExitCodeProcess(impl->child, &code))
        impl->exit_code = code;
    return impl->exit_code;
}

void c_process::terminate()
{
    if (impl->child)
    {
        TerminateProcess(impl->child, 1);
        WaitForSingleObject(impl->child, 2000);
        CloseHandle(impl->child);
        impl->child = nullptr;
    }
    if (impl->stdout_read)
    {
        CloseHandle(impl->stdout_read);
        impl->stdout_read = nullptr;
    }
}

bool process_run_captured(const std::string& command, const std::string& working_directory, unsigned int timeout_ms, std::string& out_utf8, int& out_exit_code)
{
    out_utf8.clear();
    out_exit_code = -1;

    c_process process;
    if (!process.launch(command, working_directory))
        return false;

    unsigned long start_tick = GetTickCount();
    std::string raw;
    while (true)
    {
        process.poll_output(raw);
        out_utf8 += raw;
        if (!process.alive())
            break;
        if (GetTickCount() - start_tick > timeout_ms)
        {
            process.terminate();
            out_utf8 += "\n[amyrIDE] command terminated by timeout\n";
            break;
        }
        Sleep(captured_poll_interval_ms);
        if (out_utf8.size() > captured_output_limit)
        {
            process.terminate();
            out_utf8 += "\n[amyrIDE] output truncated, command terminated\n";
            break;
        }
    }

    process.poll_output(raw);
    out_utf8 += raw;
    out_exit_code = static_cast<int>(process.exit_code());
    out_utf8 = str::strip_ansi(out_utf8);
    return true;
}
