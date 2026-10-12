#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <map>
#include <vector>
#include <filesystem>

namespace config {
    std::string resolve_alias(const std::string& input);
    bool validate_model(const std::string& model, const std::string& api_key, const std::string& base_url);
    struct McpServerConfig {
        std::string name;
        std::string command;
        std::vector<std::string> args;
        std::filesystem::path cwd;
        std::map<std::string, std::string> env;
        int timeout_ms = 10000;
    };

    struct AppConfig {
        std::string model;
        std::vector<McpServerConfig> mcp_servers;
    };

    // A missing file uses defaults; an existing invalid file reports an error.
    AppConfig load(const std::filesystem::path& config_path);
}

#endif
