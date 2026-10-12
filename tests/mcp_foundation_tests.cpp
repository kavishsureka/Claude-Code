#include "builtin_tools.hpp"
#include "config.hpp"
#include "mcp/stdio_transport.hpp"
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;
using namespace std::chrono_literals;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void expect_error(Function function, const std::string& text) {
    try { function(); }
    catch (const std::exception& error) {
        check(std::string(error.what()).find(text) != std::string::npos,
              "Expected '" + text + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("Expected error: " + text);
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "kavish-mcp-XXXXXX").string();
        auto* created = mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

json call(const std::string& name, const std::string& arguments) {
    return {{"id", "call-42"}, {"function", {{"name", name}, {"arguments", arguments}}}};
}

// A raw stdio peer, deliberately not an MCP server: tests transport independently.
int fixture(const std::string& mode, int argc, char** argv) {
    if (mode == "echo") {
        std::string line;
        while (std::getline(std::cin, line)) std::cout << line << '\n' << std::flush;
    } else if (mode == "fragment") {
        std::cout << "{\"id\":" << std::flush;
        std::this_thread::sleep_for(20ms);
        std::cout << "1}\n{\"id\":2}\n" << std::flush;
    } else if (mode == "invalid") std::cout << "bad JSON\n" << std::flush;
    else if (mode == "array") std::cout << "[]\n" << std::flush;
    else if (mode == "partial") std::cout << "{\"id\":1" << std::flush;
    else if (mode == "oversize") std::cout << std::string(mcp::StdioTransport::max_message_bytes + 1, 'x') << '\n' << std::flush;
    else if (mode == "exit") return 0;
    else if (mode == "sleep") std::this_thread::sleep_for(10s);
    else if (mode == "stubborn") {
        signal(SIGTERM, SIG_IGN);
        std::cout << "{\"ready\":true}\n" << std::flush;
        std::this_thread::sleep_for(10s);
    } else if (mode == "closed-input") {
        ::close(STDIN_FILENO);
        std::cout << "{\"ready\":true}\n" << std::flush;
        std::this_thread::sleep_for(10s);
    } else if (mode == "environment") {
        std::cerr << "fixture diagnostic on stderr\n";
        const char* env = std::getenv("KAVISH_MCP_TEST_VALUE");
        std::cout << json{{"cwd", std::filesystem::current_path().string()},
                          {"env", env ? env : ""}, {"arg", argc > 3 ? argv[3] : ""}}.dump() << '\n' << std::flush;
    }
    return 0;
}

void registry_tests(const std::filesystem::path& directory) {
    auto registry = make_builtin_registry();
    check(registry.definitions().size() == 5, "five built-ins registered");
    auto result = registry.dispatch(call("absent", "{}"));
    check(result.at("tool_call_id") == "call-42" && result.at("role") == "tool", "correlated tool response");
    check(result.at("content").get<std::string>().find("Unknown tool") != std::string::npos, "unknown tool error");
    for (const auto& arguments : {"{", "[]", "{}", "{\"file_path\":123}"}) {
        check(registry.dispatch(call("Read", arguments)).at("content").get<std::string>().starts_with("Error:"), "invalid arguments handled");
    }
    const auto filename = (directory / "hello.txt").string();
    registry.dispatch(call("Write", json{{"file_path", filename}, {"content", "hello\n"}}.dump()));
    check(registry.dispatch(call("Read", json{{"file_path", filename}}.dump())).at("content") == "hello\n", "built-in write/read preserved");
    auto definition = registry.definitions().at(0);
    expect_error([&] { registry.add(definition, [](const json&) { return ""; }); }, "Duplicate tool");
    definition["function"]["name"] = "external_example";
    registry.add(definition, [](const json&) -> std::string { throw std::runtime_error("handler failure"); });
    check(registry.dispatch(call("external_example", "{\"command\":\"unused\"}")).at("content") == "Error: handler failure", "handler exception captured");
}

void config_tests(const std::filesystem::path& directory) {
    const auto filename = directory / "config.json";
    check(config::load(filename).mcp_servers.empty(), "missing config defaults");
    auto save = [&](const json& value) { std::ofstream(filename) << value.dump(); };
    save({{"model", "opus"}});
    check(config::load(filename).model == "opus", "legacy model config");
    json server = {{"command", "./bin/server"}, {"args", {"script.py", "literal ; $(text)"}},
                   {"cwd", "work"}, {"env", {{"TOKEN", "test-value"}}}, {"timeoutMs", 250}};
    save({{"mcpServers", {{"demo", server}, {"other", {{"command", "python3"}}}}}});
    const auto loaded = config::load(filename);
    check(loaded.mcp_servers.size() == 2, "multiple server configurations");
    const auto& first = loaded.mcp_servers.at(0);
    check(first.command == (directory / "bin/server").string(), "config-relative command");
    check(first.cwd == directory / "work", "config-relative cwd");
    check(first.args.at(0) == "script.py" && first.timeout_ms == 250 && first.env.at("TOKEN") == "test-value", "args/env/timeout preserved");
    check(loaded.mcp_servers.at(1).command == "python3" && loaded.mcp_servers.at(1).timeout_ms == 10000, "PATH command/default timeout");
    for (const json& timeout : {json(0), json(-1), json(1.5), json("10"), json(300001), json(18446744073709551615ull)}) {
        auto invalid = server; invalid["timeoutMs"] = timeout;
        save({{"mcpServers", {{"demo", invalid}}}});
        expect_error([&] { config::load(filename); }, "timeoutMs");
    }
    for (const auto& field : {"args", "env", "command", "cwd"}) {
        auto invalid = server; invalid[field] = 123;
        save({{"mcpServers", {{"demo", invalid}}}});
        expect_error([&] { config::load(filename); }, "Invalid configuration");
    }
    server["command"] = std::string("bad\0command", 11);
    save({{"mcpServers", {{"demo", server}}}});
    expect_error([&] { config::load(filename); }, "NUL");
    std::ofstream(filename) << "{";
    expect_error([&] { config::load(filename); }, "Invalid configuration");
}

void transport_tests(const std::string& executable, const std::filesystem::path& directory) {
    auto settings = [&](const std::string& mode) {
        config::McpServerConfig result;
        result.name = "fixture"; result.command = executable;
        result.args = {"--fixture", mode}; result.cwd = directory; result.timeout_ms = 1500;
        return result;
    };
    {
        mcp::StdioTransport transport(settings("echo"));
        const json message = {{"jsonrpc", "2.0"}, {"id", 7}, {"text", "line one\nline two"}};
        auto until = transport.deadline();
        transport.send(message, until);
        check(transport.receive(until) == message, "JSON round trip including escaped newlines");
        const json large = {{"text", std::string(200000, 'z')}};
        until = transport.deadline(); transport.send(large, until);
        check(transport.receive(until) == large, "large message crosses multiple reads/writes");
        expect_error([&] { transport.send(json{{"text", std::string(mcp::StdioTransport::max_message_bytes, 'x')}}, transport.deadline()); }, "limit");
        check(transport.is_open(), "invalid outgoing message does not corrupt connection");
        transport.close(); transport.close();
        expect_error([&] { transport.receive(transport.deadline()); }, "closed");
    }
    {
        mcp::StdioTransport transport(settings("fragment"));
        check(transport.receive(transport.deadline()).at("id") == 1, "fragmented input");
        check(transport.receive(transport.deadline()).at("id") == 2, "buffered second message");
    }
    {
        // PATH overrides and simultaneous connections should not share descriptors.
        auto config = settings("echo");
        config.command = std::filesystem::path(executable).filename().string();
        config.env["PATH"] = std::filesystem::path(executable).parent_path().string();
        mcp::StdioTransport first(config);
        mcp::StdioTransport second(config);
        first.send(json{{"server", 1}}, first.deadline());
        second.send(json{{"server", 2}}, second.deadline());
        check(first.receive(first.deadline()).at("server") == 1, "first independent connection");
        first.close();
        check(second.receive(second.deadline()).at("server") == 2, "second connection survives first closing");
    }
    {
        auto config = settings("environment"); config.env["KAVISH_MCP_TEST_VALUE"] = "custom";
        config.args.push_back("literal ; $(touch should-not-exist)");
        mcp::StdioTransport transport(config);
        const auto result = transport.receive(transport.deadline());
        check(std::filesystem::equivalent(result.at("cwd").get<std::string>(), directory), "child cwd");
        check(result.at("env") == "custom" && result.at("arg") == config.args.back(), "env and literal arguments");
        check(!std::filesystem::exists(directory / "should-not-exist"), "no shell evaluation");
    }
    for (const auto& [mode, error] : std::vector<std::pair<std::string, std::string>>{
             {"invalid", "parse_error"}, {"array", "JSON object"}, {"partial", "mid-message"},
             {"oversize", "limit"}, {"exit", "closed stdout"}, {"sleep", "timed out"}}) {
        mcp::StdioTransport transport(settings(mode));
        expect_error([&] { transport.receive(mcp::StdioTransport::Clock::now() + 150ms); }, error);
        check(!transport.is_open(), "failed connection closed: " + mode);
    }
    {
        mcp::StdioTransport transport(settings("closed-input"));
        transport.receive(transport.deadline());
        expect_error([&] { transport.send(json{{"id", 1}}, transport.deadline()); }, "write MCP message");
        check(!transport.is_open(), "broken pipe handled without terminating assistant");
    }
    {
        mcp::StdioTransport transport(settings("sleep"));
        expect_error([&] { transport.send(json{{"text", std::string(500000, 'x')}}, mcp::StdioTransport::Clock::now() + 100ms); }, "timed out");
    }
    {
        mcp::StdioTransport transport(settings("stubborn"));
        transport.receive(transport.deadline());
        const auto start = mcp::StdioTransport::Clock::now();
        transport.close();
        check(mcp::StdioTransport::Clock::now() - start < 2s, "bounded forced shutdown");
    }
    auto missing = settings("echo"); missing.command = "kavish-no-such-executable-12345";
    expect_error([&] { mcp::StdioTransport transport(missing); }, "launch MCP server");
    missing = settings("echo"); missing.cwd = directory / "missing-directory";
    expect_error([&] { mcp::StdioTransport transport(missing); }, "launch MCP server");
    check(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD, "all child processes reaped");
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--fixture") return fixture(argv[2], argc, argv);
    try {
        TempDirectory directory;
        registry_tests(directory.path); std::cout << "Registry tests passed\n";
        config_tests(directory.path); std::cout << "Configuration tests passed\n";
        transport_tests(std::filesystem::absolute(argv[0]).string(), directory.path);
        std::cout << "Transport tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
