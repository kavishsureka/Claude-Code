#include "config.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace config {
namespace {
std::string checked_string(const nlohmann::json& value, const std::string& field, bool empty_ok = true) {
    if (!value.is_string()) throw std::invalid_argument(field + " must be a string");
    auto text = value.get<std::string>();
    if ((!empty_ok && text.empty()) || text.find('\0') != std::string::npos) {
        throw std::invalid_argument(field + " must be nonempty where required and contain no NUL bytes");
    }
    return text;
}
}

AppConfig load(const std::filesystem::path& config_path) {
    AppConfig result;
    const auto absolute = std::filesystem::absolute(config_path).lexically_normal();
    if (!std::filesystem::exists(absolute)) return result;
    try {
        std::ifstream file(absolute);
        if (!file) throw std::runtime_error("Cannot open file");
        const auto root = nlohmann::json::parse(file);
        if (!root.is_object()) throw std::invalid_argument("Configuration must be an object");
        if (root.contains("model")) result.model = checked_string(root.at("model"), "model");
        if (!root.contains("mcpServers")) return result;
        const auto& servers = root.at("mcpServers");
        if (!servers.is_object()) throw std::invalid_argument("mcpServers must be an object");
        for (const auto& [name, settings] : servers.items()) {
            if (name.empty() || name.find('\0') != std::string::npos || !settings.is_object()) {
                throw std::invalid_argument("Each MCP server needs a name and an object configuration");
            }
            McpServerConfig server;
            server.name = name;
            server.command = checked_string(settings.at("command"), name + ".command", false);
            // Bare commands use PATH. Explicit relative command paths are config-relative.
            if (server.command.find('/') != std::string::npos) {
                server.command = (absolute.parent_path() / server.command).lexically_normal().string();
            }
            server.cwd = absolute.parent_path();
            if (settings.contains("cwd")) {
                server.cwd = (absolute.parent_path() / checked_string(settings.at("cwd"), name + ".cwd", false)).lexically_normal();
            }
            if (settings.contains("args")) {
                if (!settings.at("args").is_array()) throw std::invalid_argument(name + ".args must be an array");
                for (const auto& arg : settings.at("args")) server.args.push_back(checked_string(arg, name + ".args"));
            }
            if (settings.contains("env")) {
                if (!settings.at("env").is_object()) throw std::invalid_argument(name + ".env must be an object");
                for (const auto& [key, value] : settings.at("env").items()) {
                    if (key.empty() || key.find('=') != std::string::npos || key.find('\0') != std::string::npos) {
                        throw std::invalid_argument(name + ".env contains an invalid variable name");
                    }
                    server.env[key] = checked_string(value, name + ".env value");
                }
            }
            if (settings.contains("timeoutMs")) {
                const auto& timeout = settings.at("timeoutMs");
                if (!timeout.is_number_integer() || timeout < 1 || timeout > 300000) {
                    throw std::invalid_argument(name + ".timeoutMs must be an integer between 1 and 300000");
                }
                server.timeout_ms = timeout.get<int>();
            }
            result.mcp_servers.push_back(std::move(server));
        }
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid configuration " + absolute.string() + ": " + error.what());
    }
    return result;
}
}
