#pragma once

#include "config.hpp"
#include <chrono>
#include <string>
#include <sys/types.h>
#include <nlohmann/json.hpp>

namespace mcp {

// Owns one server process and its pipes. Sequential use only; no protocol handshake.
class StdioTransport {
public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;
    static constexpr std::size_t max_message_bytes = 1024 * 1024;

    explicit StdioTransport(const config::McpServerConfig& config);
    ~StdioTransport();
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    StdioTransport(StdioTransport&&) = delete;
    StdioTransport& operator=(StdioTransport&&) = delete;

    // Pass the same deadline to send/read to bound an entire future RPC request.
    Deadline deadline() const;
    void send(const nlohmann::json& message, Deadline until);
    nlohmann::json receive(Deadline until);
    void close() noexcept;
    bool is_open() const noexcept { return pid_ > 0 && input_ >= 0 && output_ >= 0; }

private:
    pid_t pid_ = -1;
    int input_ = -1;
    int output_ = -1;
    int timeout_ms_;
    std::string pending_;
};
}
