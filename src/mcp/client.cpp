#include "client.hpp"
#include <set>

namespace mcp {
using Json = nlohmann::json;
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
std::string format_result(const Json& result) {
    require(result.is_object() && result.contains("content") && result.at("content").is_array(), "Invalid MCP tool result");
    std::string text = result.value("isError", false) ? "Tool error:\n" : "";
    for (const auto& block : result.at("content")) {
        const auto type = block.at("type").get<std::string>();
        if (type == "text") {
            text += block.at("text").get<std::string>() + "\n";
        } else {
            text += "[Unsupported MCP content type: " + type + "]\n";
        }
    }
    if (result.contains("structuredContent")) {
        require(result.at("structuredContent").is_object(), "Invalid MCP structuredContent");
        text += "Structured result:\n" + result.at("structuredContent").dump() + "\n";
    }
    if (text.empty()) text = "Tool completed with no content.";
    // Limit model context, separately from the transport frame limit.
    constexpr std::size_t limit = 64000;
    if (text.size() > limit) {
        std::size_t end = limit;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        text.resize(end);
        text += "\n[Tool output truncated at 64000 bytes]";
    }
    return text;
}
}

Client::Client(const config::McpServerConfig& settings)
    : transport_(std::make_unique<StdioTransport>(settings)) {
    initialize();
}

void Client::initialize() {
    const auto until = transport_->deadline();
    const auto result = request("initialize", {
        {"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()},
        {"clientInfo", {{"name", "kavishcode"}, {"version", "0.1.0"}}}
    }, until);
    version_ = result.at("protocolVersion").get<std::string>();
    require(version_ == "2025-11-25" || version_ == "2025-06-18" || version_ == "2025-03-26",
            "Unsupported MCP protocol version");
    require(result.at("capabilities").is_object() && result.at("capabilities").contains("tools") &&
            result.at("capabilities").at("tools").is_object(), "MCP server does not support tools");
    require(result.at("serverInfo").at("name").is_string() && result.at("serverInfo").at("version").is_string(),
            "Invalid MCP serverInfo");
    transport_->send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, until);
}

void Client::disconnect(const std::string& reason) {
    failure_ = reason;
    if (transport_) {
        transport_->close();
    }
}

// Servers can ask us questions while we are waiting for our own reply.
// We support ping only; optional client features were not advertised.
void Client::reply_to_server_request(const Json& message, StdioTransport::Deadline until) {
    const auto& id = message.at("id");
    require(id.is_string() || id.is_number_integer(), "Invalid server request ID");

    Json reply = {{"jsonrpc", "2.0"}, {"id", id}};
    if (message.at("method") == "ping") {
        reply["result"] = Json::object();
    } else {
        reply["error"] = {{"code", -32601}, {"message", "Client method not supported"}};
    }
    transport_->send(reply, until);
}

Json Client::request(const std::string& method, const Json& params, StdioTransport::Deadline until) {
    if (!available()) {
        throw std::runtime_error("MCP server unavailable: " + failure_);
    }
    // IDs belong to this connection, independently of model tool-call IDs.
    const auto id = next_id_++;
    try {
        transport_->send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}, until);
        for (;;) {
            require(StdioTransport::Clock::now() < until, "MCP request timed out");
            const auto message = transport_->receive(until);
            require(message.value("jsonrpc", "") == "2.0", "Invalid JSON-RPC version");
            if (message.contains("method")) {
                require(!message.contains("result") && !message.contains("error"), "Invalid JSON-RPC request");
                require(message.at("method").is_string(), "Invalid JSON-RPC method");
                if (message.contains("id")) {
                    reply_to_server_request(message, until);
                }
                // Catalogs are loaded once at startup. Notifications, including
                // tools/list_changed, are ignored without extending the deadline.
                continue;
            }
            require(message.contains("id") && message.at("id") == id, "Unexpected MCP response ID");
            require(message.contains("result") != message.contains("error"), "Invalid JSON-RPC response");
            if (message.contains("error")) {
                const auto& error = message.at("error");
                require(error.at("code").is_number_integer() && error.at("message").is_string(), "Invalid JSON-RPC error");
                throw RpcError("MCP RPC error " + error.at("code").dump() + ": " + error.at("message").get<std::string>());
            }
            require(message.at("result").is_object(), "Invalid MCP result object");
            return message.at("result");
        }
    } catch (const RpcError&) {
        throw; // A valid RPC error does not mean the connection is broken.
    } catch (const std::exception& error) {
        disconnect(error.what());
        throw;
    }
}

// Called once during startup. Follow pagination to collect the whole catalog.
Json Client::list_tools() {
    if (!available()) {
        throw std::runtime_error("MCP server unavailable: " + failure_);
    }
    try {
        Json tools = Json::array();
        Json params = Json::object();
        std::set<std::string> cursors;
        std::set<std::string> names;
        const auto until = transport_->deadline(); // Bound the entire paginated discovery.
        for (int page = 0; page < 100; ++page) {
            const auto result = request("tools/list", params, until);
            require(result.at("tools").is_array(), "Invalid MCP tools list");
            for (const auto& tool : result.at("tools")) {
                const auto name = tool.at("name").get<std::string>();
                require(!name.empty() && names.insert(name).second, "Empty or duplicate MCP tool name");
                require(tool.at("inputSchema").is_object() && tool.at("inputSchema").value("type", "") == "object", "Invalid MCP inputSchema");
                if (tool.contains("description")) {
                    require(tool.at("description").is_string(), "Invalid MCP tool description");
                }
                require(names.size() <= 1000, "MCP discovery exceeds 1000 tools");
                // Tasks are not negotiated by this synchronous client.
                if (tool.contains("execution") && tool.at("execution").value("taskSupport", "") == "required") {
                    continue;
                }
                tools.push_back(tool);
            }
            if (!result.contains("nextCursor")) {
                return tools;
            }
            const auto cursor = result.at("nextCursor").get<std::string>();
            require(cursors.insert(cursor).second, "Repeated MCP pagination cursor");
            params["cursor"] = cursor;
        }
        throw std::runtime_error("MCP discovery exceeds 100 pages");
    } catch (const std::exception& error) {
        disconnect(error.what());
        throw;
    }
}

std::string Client::call_tool(const std::string& name, const Json& arguments) {
    if (!available()) {
        throw std::runtime_error("MCP server unavailable: " + failure_);
    }
    try {
        const Json params = {{"name", name}, {"arguments", arguments}};
        const auto result = request("tools/call", params, transport_->deadline());
        return format_result(result);
    } catch (const RpcError&) {
        throw;
    } catch (const std::exception& error) {
        disconnect(error.what());
        throw;
    }
}
}
