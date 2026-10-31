#include "mcp/manager.hpp"
#include <iostream>
#include <regex>

using Json = nlohmann::json;
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
Json call(const std::string& name, const Json& args = Json::object()) {
    return {{"id", "llm-id-42"}, {"function", {{"name", name}, {"arguments", args.dump()}}}};
}
std::string find_tool(const ToolRegistry& registry, const std::string& fragment) {
    for (const auto& tool : registry.definitions()) {
        auto name = tool.at("function").at("name").get<std::string>();
        if (name.find(fragment) != std::string::npos) return name;
    }
    throw std::runtime_error("Tool missing: " + fragment);
}
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Expected Python and fixture script");
        auto settings = [&](const std::string& mode) {
            config::McpServerConfig config;
            config.name = mode; config.command = argv[1]; config.args = {argv[2], mode};
            config.timeout_ms = 1000;
            return config;
        };
        if (argc == 4 && std::string(argv[3]) == "--sdk") {
            ToolRegistry registry;
            auto config = settings("sdk"); config.args = {argv[2]}; config.timeout_ms = 10000;
            mcp::Manager manager(registry, {config});
            const auto response = registry.dispatch(call(find_tool(registry, "project_info")));
            check(response.at("content").get<std::string>().find("Kavish Code") != std::string::npos, "SDK project_info");
            check(registry.dispatch(call(find_tool(registry, "source_summary"))).at("content").get<std::string>().find("total_lines") != std::string::npos, "SDK source_summary");
            std::cout << "Official Python SDK interoperability passed\n";
            return 0;
        }
        {
            ToolRegistry registry;
            auto first = settings("notify"), second = settings("notify"); second.name = "other";
            mcp::Manager manager(registry, {first, second});
            check(registry.definitions().size() == 4, "paginated tools from two servers");
            const auto name = find_tool(registry, "mcp_notify_echo");
            const auto response = registry.dispatch(call(name, {{"text", "hello MCP"}}));
            check(response.at("tool_call_id") == "llm-id-42", "LLM ID preserved independently of RPC ID");
            check(response.at("content").get<std::string>().find("hello MCP") != std::string::npos, "round trip with ping/notifications");
            check(find_tool(registry, "mcp_notify_echo") == name, "notification does not change aliases");
            check(!find_tool(registry, "mcp_notify_extra").empty(), "catalog remains the startup snapshot");
            check(registry.dispatch(call(name, {{"text", nullptr}})).at("content").get<std::string>().find("null") != std::string::npos, "nullable JSON Schema passed through");
        }
        for (const auto& mode : {"version", "no-tools", "schema", "duplicate", "pagination-loop", "timeout-start"}) {
            ToolRegistry registry;
            mcp::Manager manager(registry, {settings(mode), settings("healthy")});
            check(manager.status().find(std::string(mode) + ": unavailable:") != std::string::npos, "startup failure isolated");
            check(registry.definitions().size() == 2, "healthy server survives failed startup");
        }
        for (const auto& mode : {"exit", "timeout", "wrong-id", "malformed", "invalid-result"}) {
            ToolRegistry registry;
            mcp::Manager manager(registry, {settings(mode), settings("healthy")});
            const auto name = find_tool(registry, "mcp_" + std::string(mode) + "_echo");
            check(registry.dispatch(call(name)).at("content").get<std::string>().starts_with("Error:"), "failed call returns error");
            check(registry.definitions().size() == 2, "failed server tools hidden");
            check(registry.dispatch(call(name)).at("content").get<std::string>().find("unavailable") != std::string::npos, "no retry of failed call");
            check(registry.dispatch(call(find_tool(registry, "mcp_healthy_echo"))).at("content").get<std::string>().find("ok") != std::string::npos, "healthy server still callable");
        }
        for (const auto& mode : {"tool-error", "rpc-error", "media", "resource"}) {
            ToolRegistry registry;
            mcp::Manager manager(registry, {settings(mode)});
            auto response = registry.dispatch(call(find_tool(registry, "echo"))).at("content").get<std::string>();
            const std::string expected = std::string(mode) == "tool-error" ? "Tool error:" : std::string(mode) == "rpc-error" ? "MCP RPC error" : "Unsupported MCP content type";
            check(response.find(expected) != std::string::npos, "result/error conversion");
            check(registry.definitions().size() == 2, "application/RPC error keeps connection usable");
        }
        {
            ToolRegistry registry;
            auto first = settings("names"), second = first; first.name = "a.b"; second.name = "a_b";
            mcp::Manager manager(registry, {first, second});
            check(registry.definitions().size() == 6, "namespaced tools do not collide");
            for (const auto& definition : registry.definitions()) {
                auto name = definition.at("function").at("name").get<std::string>();
                check(name.size() <= 64 && std::regex_match(name, std::regex("[A-Za-z0-9_-]+")), "provider-compatible tool alias");
            }
        }
        std::cout << "MCP client and routing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
