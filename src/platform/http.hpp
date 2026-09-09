#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct http_result_t
{
    int status_code = 0;
    std::string error_text;
    std::string content_type;
    bool ok() const { return status_code >= 200 && status_code < 300 && error_text.empty(); }
};

using http_chunk_fn = std::function<bool(const char* data, unsigned int size)>;
using http_header_t = std::pair<std::string, std::string>;

class c_http_client
{
public:
    c_http_client();
    ~c_http_client();

    c_http_client(const c_http_client&) = delete;
    c_http_client& operator=(const c_http_client&) = delete;

    void set_insecure_skip_verify(bool skip);
    void set_timeout(unsigned int resolve_ms, unsigned int connect_ms, unsigned int send_ms, unsigned int receive_ms);

    bool post_streamed(const std::string& url, const std::vector<http_header_t>& headers, const std::string& body, const http_chunk_fn& on_chunk, http_result_t& result);
    bool get(const std::string& url, const std::vector<http_header_t>& headers, std::string& out_body, http_result_t& result);

private:
    struct impl_t;
    std::unique_ptr<impl_t> impl;
};
