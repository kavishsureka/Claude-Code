#pragma once
#include "stdio_transport.hpp"
#include <memory>
#include <stdexcept>

namespace mcp {
class RpcError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Synchronous tools-only MCP client. Owns one persistent connection.
class Client {
public:
    using Json = nlohmann::json;
    explicit Client(const config::McpServerConfig& settings);
    Json list_tools();
    std::string call_tool(const std::string& name, const Json& arguments);
    bool available() const { return transport_ && transport_->is_open(); }
    const std::string& failure() const { return failure_; }
    const std::string& protocol_version() const { return version_; }
    void disconnect(const std::string& reason);
private:
    void initialize();
    void reply_to_server_request(const Json& message, StdioTransport::Deadline until);
    Json request(const std::string& method, const Json& params, StdioTransport::Deadline until);
    std::unique_ptr<StdioTransport> transport_;
    std::int64_t next_id_ = 1;
    std::string version_;
    std::string failure_;
};
}
