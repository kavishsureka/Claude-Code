#pragma once
#include "client.hpp"
#include "tool_registry.hpp"
#include <functional>

namespace mcp {
class Manager {
public:
    using Observer = std::function<void(const std::string&, const std::string&)>;
    Manager(ToolRegistry& registry, const std::vector<config::McpServerConfig>& settings, Observer on_call = {});
    std::string status() const;
    bool all_available() const;
private:
    struct Server {
        std::string name;
        std::string startup_error;
        std::shared_ptr<Client> client;
        // Model-facing alias -> original name on this server.
        std::map<std::string, std::string> tools;
    };
    void register_tools(Server& server);
    std::string make_alias(const std::string& server, const std::string& tool);
    ToolRegistry& registry_;
    Observer on_call_;
    std::vector<Server> servers_;
    std::size_t next_alias_ = 1;
};
}
