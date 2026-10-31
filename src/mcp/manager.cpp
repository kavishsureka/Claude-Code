#include "manager.hpp"
#include <sstream>

namespace mcp {
namespace {
std::string safe_name(const std::string& text) {
    std::string result;
    for (unsigned char c : text) {
        const bool allowed = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '-';
        result += allowed ? static_cast<char>(c) : '_';
    }
    return result;
}
}

Manager::Manager(ToolRegistry& registry,
                 const std::vector<config::McpServerConfig>& settings,
                 Observer on_call)
    : registry_(registry), on_call_(std::move(on_call)) {
    // Discover each server once. A failed server must not stop the others.
    for (const auto& config : settings) {
        Server server;
        server.name = config.name;
        try {
            server.client = std::make_shared<Client>(config);
            register_tools(server);
        } catch (const std::exception& error) {
            server.startup_error = error.what();
            if (server.client) {
                server.client->disconnect(error.what());
            }
            // Remove any tools registered before startup failed.
            for (const auto& [alias, original] : server.tools) {
                registry_.remove(alias);
            }
            server.tools.clear();
        }
        servers_.push_back(std::move(server));
    }
}

std::string Manager::make_alias(const std::string& server, const std::string& tool) {
    const auto prefix = ("mcp_" + safe_name(server) + "_" + safe_name(tool)).substr(0, 48);
    std::string alias;
    do {
        alias = prefix + "_" + std::to_string(next_alias_++);
    } while (registry_.contains(alias));
    return alias;
}

void Manager::register_tools(Server& server) {
    const auto tools = server.client->list_tools();
    for (const auto& tool : tools) {
        const auto original = tool.at("name").get<std::string>();
        const auto alias = make_alias(server.name, original);
        const auto client = server.client;
        const auto observer = on_call_;
        const auto label = server.name + "/" + original;

        // Translate the server's schema into the model API's function format.
        const nlohmann::json definition = {
            {"type", "function"},
            {"function", {
                {"name", alias},
                {"description", "MCP tool from " + server.name + ": " + tool.value("description", original)},
                {"parameters", tool.at("inputSchema")}
            }}
        };

        // Remember the client and original name; never parse the alias to route.
        auto run = [client, original, observer, label](const nlohmann::json& arguments) {
            if (observer) {
                observer(label, arguments.dump());
            }
            return client->call_tool(original, arguments);
        };
        auto available = [client] { return client->available(); };

        registry_.add(definition, run, available);
        server.tools.emplace(alias, original);
    }
}

bool Manager::all_available() const {
    for (const auto& server : servers_) {
        if (!server.client || !server.client->available()) {
            return false;
        }
    }
    return true;
}

std::string Manager::status() const {
    if (servers_.empty()) {
        return "No MCP servers configured.";
    }
    std::ostringstream out;
    for (const auto& server : servers_) {
        out << server.name << ": ";
        if (server.client && server.client->available()) {
            out << "connected (last known), protocol " << server.client->protocol_version()
                << ", " << server.tools.size() << " tools\n";
            for (const auto& [alias, original] : server.tools) {
                out << "  " << alias << " -> " << original << '\n';
            }
        } else {
            const auto error = server.startup_error.empty() && server.client
                ? server.client->failure() : server.startup_error;
            out << "unavailable: " << error << '\n';
        }
    }
    return out.str();
}
}
