#pragma once

#include <memory>
#include <string>

class c_process
{
public:
    c_process();
    ~c_process();

    c_process(const c_process&) = delete;
    c_process& operator=(const c_process&) = delete;

    bool launch(const std::string& command, const std::string& working_directory);
    void poll_output(std::string& out_bytes);
    bool alive() const;
    unsigned long exit_code() const;
    void terminate();

private:
    struct impl_t;
    std::unique_ptr<impl_t> impl;
};

bool process_run_captured(const std::string& command, const std::string& working_directory, unsigned int timeout_ms, std::string& out_utf8, int& out_exit_code);
