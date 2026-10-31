#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "ui.hpp"
#include "config.hpp"
#include "builtin_tools.hpp"
#include "mcp/manager.hpp"

using json = nlohmann::json;


cpr::Response call_api(std::vector<json>&messages,std::string base_url,std::string api_key, std::string model, const json& tools){
    json request_body = {
        {"model", model},
        {"messages", messages},
        {"include_reasoning", true},
        {"tools", tools}
    };
    ui::start_spinner();
    cpr::Response response = cpr::Post(
        cpr::Url{base_url + "/chat/completions"},
        cpr::Header{
            {"Authorization", "Bearer " + api_key},
            {"Content-Type", "application/json"}
        },
        cpr::Body{request_body.dump()}
    );
    ui::stop_spinner();
    return response;
}

int main(int argc, char* argv[]) {
    std::string initial_prompt = "";
    std::string cli_model = "";
    std::string config_path = "config.json";
    bool check_mcp = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--prompt") {
            if (i + 1 < argc) initial_prompt = argv[++i];
        } else if (arg == "-m" || arg == "--model") {
            if (i + 1 < argc) cli_model = argv[++i];
        } else if (arg == "--mcp-check") {
            check_mcp = true;
        } else if (arg == "--config") {
            if (i + 1 >= argc) {
                ui::print_error("--config requires a file path");
                return 1;
            }
            config_path = argv[++i];
        }
    }

    config::AppConfig app_config;
    try {
        app_config = config::load(config_path);
    } catch (const std::exception& error) {
        ui::print_error(error.what());
        return 1;
    }
    ToolRegistry registry = make_builtin_registry();
    // Diagnostic mode uses exactly the same startup/discovery path without an LLM.
    if (check_mcp) {
        mcp::Manager manager(registry, app_config.mcp_servers);
        std::cout << manager.status() << std::endl;
        return manager.all_available() ? 0 : 1;
    }

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");
    const char* model_env = std::getenv("OPENROUTER_MODEL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty()) {
        ui::print_error("OPENROUTER_API_KEY environment variable is not set.");
        std::cerr << "Please set it before running the program:" << std::endl;
        std::cerr << "export OPENROUTER_API_KEY=\"your_key_here\"" << std::endl;
        return 1;
    }

    std::string final_model_raw = "";
    if (!cli_model.empty()) {
        final_model_raw = cli_model;
    } else if (model_env && std::string(model_env) != "") {
        final_model_raw = model_env;
    } else {
        std::string config_model = app_config.model;
        if (!config_model.empty()) {
            final_model_raw = config_model;
        } else {
            final_model_raw = "llama"; // Default fallback
        }
    }

    std::string resolved_model = config::resolve_alias(final_model_raw);

    if (!config::validate_model(resolved_model, api_key, base_url)) {
        ui::print_error("Model '" + resolved_model + "' is not available on OpenRouter.");
        return 1;
    }
    
    ui::print_system("Using model: " + resolved_model);

    mcp::Manager mcp_manager(registry, app_config.mcp_servers,
        [](const std::string& name, const std::string& args) { ui::print_tool_call(name, args); });
    if (!app_config.mcp_servers.empty()) ui::print_system(mcp_manager.status());

    std::vector<json> messages;   
    ui::print_welcome_banner();

    while (true) {
        std::string prompt;
        if (!initial_prompt.empty()) {
            prompt = initial_prompt;
            initial_prompt = "";
            std::cout << ui::BOLD << ui::GREEN << "User: " << ui::RESET << prompt << std::endl;
        } else {
            std::cout << ui::BOLD << ui::GREEN << "User: " << ui::RESET;
            if (!std::getline(std::cin, prompt)) break; // EOF (Ctrl+D)
            if (prompt == "exit" || prompt == "quit") break;
            if (prompt.empty()) continue;
        }

        if (prompt == "/mcp") {
            ui::print_system(mcp_manager.status());
            continue;
        }
        if (prompt == "/tools") {
            for (const auto& tool : registry.definitions()) {
                ui::print_system(tool.at("function").at("name").get<std::string>() + ": " +
                                 tool.at("function").at("description").get<std::string>());
            }
            continue;
        }
        messages.push_back({{"role", "user"}, {"content", prompt}});

        // Agent loop for handling tools
        while(true){
            cpr::Response response = call_api(messages,base_url,api_key, resolved_model, registry.definitions());

            if (response.status_code != 200) {
                ui::print_error("HTTP error: " + std::to_string(response.status_code));
                if (response.status_code == 0) {
                    ui::print_error("CURL Error: " + response.error.message);
                } else if (!response.text.empty()) {
                    ui::print_error("Response body: " + response.text);
                }
                return 1;
            }

            json result = json::parse(response.text);

            if (!result.contains("choices") || result["choices"].empty()) {
                ui::print_error("No choices in response");
                return 1;
            }

            json message = result["choices"][0]["message"].get<json>();
            if (message.contains("reasoning") && !message["reasoning"].is_null()) {
                std::string reasoning = message["reasoning"].get<std::string>();
                if (!reasoning.empty()) {
                    ui::print_reasoning(reasoning);
                }
            }
            messages.push_back(message);
        
            if(!message.contains("tool_calls")||message["tool_calls"].empty()){
                ui::print_assistant_message(message["content"].get<std::string>());
                break; // Break agent loop to ask user again
            }

            for (const auto& tool : message["tool_calls"]) {
                messages.push_back(registry.dispatch(tool));
            }
        }
    }
    return 0;
}
