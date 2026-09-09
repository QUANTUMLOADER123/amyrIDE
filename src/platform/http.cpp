#include "http.hpp"

#include <windows.h>
#include <winhttp.h>

#include "core/string.hpp"

#pragma comment(lib, "winhttp.lib")

struct c_http_client::impl_t
{
    HINTERNET session = nullptr;
    bool insecure = false;
    unsigned int resolve_timeout = 10000;
    unsigned int connect_timeout = 20000;
    unsigned int send_timeout = 30000;
    unsigned int receive_timeout = 180000;

    ~impl_t()
    {
        if (session)
            WinHttpCloseHandle(session);
    }
};

namespace
{
    struct url_parts_t
    {
        bool secure = true;
        std::string host;
        int port = 443;
        std::string path;
    };

    bool split_url(const std::string& url, url_parts_t& parts)
    {
        std::string_view view(url);
        size_t scheme_end = view.find("://");
        if (scheme_end == std::string_view::npos)
            return false;
        std::string_view scheme = view.substr(0, scheme_end);
        parts.secure = str::iequals(scheme, "https");
        size_t authority_begin = scheme_end + 3;
        size_t path_begin = view.find('/', authority_begin);
        std::string_view authority = path_begin == std::string_view::npos ? view.substr(authority_begin) : view.substr(authority_begin, path_begin - authority_begin);
        parts.path = path_begin == std::string_view::npos ? "/" : std::string(view.substr(path_begin));

        std::string_view host_part = authority;
        if (!authority.empty() && authority.front() == '[')
        {
            size_t close = authority.find(']');
            if (close == std::string_view::npos)
                return false;
            host_part = authority.substr(1, close - 1);
            if (close + 1 < authority.size() && authority[close + 1] == ':')
                parts.port = std::atoi(std::string(authority.substr(close + 2)).c_str());
        }
        else
        {
            size_t colon = authority.rfind(':');
            if (colon != std::string_view::npos)
            {
                host_part = authority.substr(0, colon);
                parts.port = std::atoi(std::string(authority.substr(colon + 1)).c_str());
            }
        }
        if (parts.port == 0)
            parts.port = parts.secure ? 443 : 80;
        parts.host = std::string(host_part);
        return !parts.host.empty();
    }

    std::wstring to_wide(std::string_view text)
    {
        if (text.empty())
            return {};
        int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
        return out;
    }

    void set_error(http_result_t& result, const char* what, unsigned int error_code)
    {
        result.error_text = str::format("%s failed (0x%08lx)", what, static_cast<unsigned long>(error_code));
    }
}

c_http_client::c_http_client()
    : impl(std::make_unique<impl_t>())
{
    impl->session = WinHttpOpen(L"amyrIDE/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (impl->session)
    {
        WinHttpSetTimeouts(impl->session, impl->resolve_timeout, impl->connect_timeout, impl->send_timeout, impl->receive_timeout);
    }
}

c_http_client::~c_http_client() = default;

void c_http_client::set_insecure_skip_verify(bool skip)
{
    impl->insecure = skip;
}

void c_http_client::set_timeout(unsigned int resolve_ms, unsigned int connect_ms, unsigned int send_ms, unsigned int receive_ms)
{
    impl->resolve_timeout = resolve_ms;
    impl->connect_timeout = connect_ms;
    impl->send_timeout = send_ms;
    impl->receive_timeout = receive_ms;
    if (impl->session)
        WinHttpSetTimeouts(impl->session, resolve_ms, connect_ms, send_ms, receive_ms);
}

bool c_http_client::post_streamed(const std::string& url, const std::vector<http_header_t>& headers, const std::string& body, const http_chunk_fn& on_chunk, http_result_t& result)
{
    result = http_result_t{};
    if (!impl->session)
    {
        result.error_text = "http session unavailable";
        return false;
    }

    url_parts_t parts;
    if (!split_url(url, parts))
    {
        result.error_text = "malformed url: " + url;
        return false;
    }

    std::wstring wide_host = to_wide(parts.host);
    std::wstring wide_path = to_wide(parts.path);
    HINTERNET connection = WinHttpConnect(impl->session, wide_host.c_str(), static_cast<INTERNET_PORT>(parts.port), 0);
    if (!connection)
    {
        set_error(result, "connect", GetLastError());
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"POST", wide_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request)
    {
        set_error(result, "open request", GetLastError());
        WinHttpCloseHandle(connection);
        return false;
    }

    if (impl->insecure)
    {
        unsigned long security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));
    }

    std::string header_text;
    for (const auto& [name, value] : headers)
        header_text += name + ": " + value + "\r\n";
    std::wstring wide_headers = to_wide(header_text);

    bool success = false;
    if (WinHttpSendRequest(request, wide_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wide_headers.c_str(), -1L, const_cast<char*>(body.data()), static_cast<unsigned long>(body.size()), static_cast<unsigned long>(body.size()), 0))
    {
        if (WinHttpReceiveResponse(request, nullptr))
        {
            unsigned long status_code = 0;
            unsigned long status_size = sizeof(status_code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
            result.status_code = static_cast<int>(status_code);

            unsigned long content_type_size = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &content_type_size, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && content_type_size > 0)
            {
                std::wstring wide_type(content_type_size, L'\0');
                WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, wide_type.data(), &content_type_size, WINHTTP_NO_HEADER_INDEX);
                int needed = WideCharToMultiByte(CP_UTF8, 0, wide_type.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (needed > 1)
                    result.content_type.assign(needed - 1, '\0'), WideCharToMultiByte(CP_UTF8, 0, wide_type.c_str(), -1, result.content_type.data(), needed, nullptr, nullptr);
            }

            if (result.status_code >= 400)
            {
                std::string error_body;
                char read_buffer[8192];
                unsigned long available = 0;
                while (WinHttpQueryDataAvailable(request, &available) && available > 0)
                {
                    unsigned long to_read = available < sizeof(read_buffer) ? available : sizeof(read_buffer);
                    unsigned long read_bytes = 0;
                    if (!WinHttpReadData(request, read_buffer, to_read, &read_bytes) || read_bytes == 0)
                        break;
                    error_body.append(read_buffer, read_bytes);
                }
                result.error_text = str::truncate_middle(str::trim(error_body), 1200);
                success = false;
            }
            else
            {
                success = true;
                char read_buffer[16384];
                unsigned long available = 0;
                while (success)
                {
                    if (!WinHttpQueryDataAvailable(request, &available))
                    {
                        set_error(result, "query data", GetLastError());
                        success = false;
                        break;
                    }
                    if (available == 0)
                        break;
                    unsigned long to_read = available < sizeof(read_buffer) ? available : sizeof(read_buffer);
                    unsigned long read_bytes = 0;
                    if (!WinHttpReadData(request, read_buffer, to_read, &read_bytes))
                    {
                        set_error(result, "read data", GetLastError());
                        success = false;
                        break;
                    }
                    if (read_bytes == 0)
                        break;
                    if (!on_chunk(read_buffer, static_cast<unsigned int>(read_bytes)))
                    {
                        result.error_text = "cancelled";
                        success = false;
                        break;
                    }
                }
            }
        }
        else
        {
            set_error(result, "receive response", GetLastError());
        }
    }
    else
    {
        set_error(result, "send request", GetLastError());
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return success;
}

bool c_http_client::get(const std::string& url, const std::vector<http_header_t>& headers, std::string& out_body, http_result_t& result)
{
    result = http_result_t{};
    out_body.clear();
    if (!impl->session)
    {
        result.error_text = "http session unavailable";
        return false;
    }

    url_parts_t parts;
    if (!split_url(url, parts))
    {
        result.error_text = "malformed url: " + url;
        return false;
    }

    std::wstring wide_host = to_wide(parts.host);
    std::wstring wide_path = to_wide(parts.path);
    HINTERNET connection = WinHttpConnect(impl->session, wide_host.c_str(), static_cast<INTERNET_PORT>(parts.port), 0);
    if (!connection)
    {
        set_error(result, "connect", GetLastError());
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", wide_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request)
    {
        set_error(result, "open request", GetLastError());
        WinHttpCloseHandle(connection);
        return false;
    }

    if (impl->insecure)
    {
        unsigned long security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));
    }

    std::string header_text;
    for (const auto& [name, value] : headers)
        header_text += name + ": " + value + "\r\n";
    std::wstring wide_headers = to_wide(header_text);

    bool success = false;
    if (WinHttpSendRequest(request, wide_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wide_headers.c_str(), -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        if (WinHttpReceiveResponse(request, nullptr))
        {
            unsigned long status_code = 0;
            unsigned long status_size = sizeof(status_code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
            result.status_code = static_cast<int>(status_code);

            char read_buffer[16384];
            unsigned long available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0)
            {
                unsigned long to_read = available < sizeof(read_buffer) ? available : sizeof(read_buffer);
                unsigned long read_bytes = 0;
                if (!WinHttpReadData(request, read_buffer, to_read, &read_bytes) || read_bytes == 0)
                    break;
                out_body.append(read_buffer, read_bytes);
            }
            success = result.status_code >= 200 && result.status_code < 300;
            if (!success)
                result.error_text = str::truncate_middle(str::trim(out_body), 800);
        }
        else
        {
            set_error(result, "receive response", GetLastError());
        }
    }
    else
    {
        set_error(result, "send request", GetLastError());
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    return success;
}
