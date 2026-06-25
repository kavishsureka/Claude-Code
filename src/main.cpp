#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <dirent.h>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "ui.hpp"
#include "config.hpp"

using json = nlohmann::json;


cpr::Response call_api(std::vector<json>&messages,std::string base_url,std::string api_key, std::string model){
    json request_body = {
        {"model", model},
        {"messages", messages},
        {"include_reasoning", true},
        {"tools", json::array({
            {
                {"type", "function"},
                {"function", {
                    {"name", "Read"},
                    {"description", "Read and return the contents of a file"},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"file_path", {
                                {"type", "string"},
                                {"description", "The path to the file to read"}
                            }}
                        }},
                        {"required", json::array({"file_path"})}
                    }}
                }}
            },{
                {"type", "function"},
                {"function", {
                    {"name", "Write"},
                    {"description", "Write content to a file"},
                    {"parameters", {
                        {"type", "object"},
                        {"required",json::array({"file_path", "content"})},
                        {"properties", {
                            {"file_path", {
                                {"type", "string"},
                                {"description", "The path of the file to write to"}
                            }},
                            {"content", {
                                {"type","string"},
                                {"description", "The content to write to the file"}
                            }}
                        }}
                    }}
                }}
            },{
                {"type", "function"},
                {"function", {
                    {"name", "Bash"},
                    {"description", "Execute a shell command"},
                    {"parameters", {
                        {"type", "object"},
                        {"required", json::array({"command"})},
                        {"properties", {
                            {"command", {
                                {"type", "string"},
                                {"description", "The command to execute"}
                            }}
                        }}
                    }}
                }}
            },{
                {"type", "function"},
                {"function", {
                    {"name", "List_Skills"},
                    {"description", "List all custom skills/workflows available in the skills/ directory. Call this to see if a predefined skill exists for the user's task. If you find a relevant skill, use the Read tool to read its contents."},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", json::object()},
                        {"required", json::array()}
                    }}
                }}
            }
        })}
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

void read_tool(json tool,json function_args,std::vector<json>&messages){
    std::string file_path = function_args["file_path"].get<std::string>();
    ui::print_tool_call("Read", file_path);
    std::ifstream file(file_path);
    if(!file.is_open()){
        ui::print_error("Could not open the file!");
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", tool["id"].get<std::string>()},
            {"content", "Error: Could not open the file!"}
        });
        return;
    }
    std::string content;
    std::string line;
    while(std::getline(file,line)){
        content += line + "\n";
    }
    file.close();
    messages.push_back({
        {"role","tool"},
        {"tool_call_id",tool["id"].get<std::string>()},
        {"content",content}
    });
}

void write_tool(json tool,json function_args,std::vector<json>&messages){
    std::string file_path = function_args["file_path"].get<std::string>();
    std::string content = function_args["content"].get<std::string>();
    ui::print_tool_call("Write", file_path);
    std::ofstream file(file_path,std::ios::out | std::ios::trunc);

    if (!file) {
        ui::print_error("Failed to open file for writing.");
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", tool["id"].get<std::string>()},
            {"content", "Error: Failed to open file for writing."}
        });
        return;
    }
    file << content;
    file.close();

    messages.push_back({
        {"role","tool"},
        {"tool_call_id",tool["id"].get<std::string>()},
        {"content",content}
    });
}

void command_tool(json tool,json function_args,std::vector<json>&messages){
    std::string command = function_args["command"].get<std::string>();
    ui::print_tool_call("Bash", command);
    command += " 2>&1";  //redirect stderr into stdout. 2 means stderr,> means redirect, &1 means wherever stdout currently points”

    FILE *pipe = popen(command.c_str(),"r");
    if(!pipe){
        ui::print_error("Error opening pipe.");
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", tool["id"].get<std::string>()},
            {"content", "Error: Could not open pipe to execute command."}
        });
        return;
    }
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    messages.push_back({
        {"role","tool"},
        {"tool_call_id",tool["id"].get<std::string>()},
        {"content",result}
    });
}

void list_skills_tool(json tool, json function_args, std::vector<json>&messages) {
    ui::print_tool_call("List_Skills", "");
    std::string result = "Available skills in skills/ directory:\n";
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("skills")) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            std::string filename = ent->d_name;
            if (filename != "." && filename != "..") {
                result += "- " + filename + "\n";
            }
        }
        closedir(dir);
    } else {
        result = "No skills directory found or it is empty.";
    }
    
    messages.push_back({
        {"role", "tool"},
        {"tool_call_id", tool["id"].get<std::string>()},
        {"content", result}
    });
}

int main(int argc, char* argv[]) {
    std::string initial_prompt = "";
    std::string cli_model = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--prompt") {
            if (i + 1 < argc) initial_prompt = argv[++i];
        } else if (arg == "-m" || arg == "--model") {
            if (i + 1 < argc) cli_model = argv[++i];
        }
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
        std::string config_model = config::get_model_from_config("config.json");
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

        messages.push_back({{"role", "user"}, {"content", prompt}});

        // Agent loop for handling tools
        while(true){
            cpr::Response response = call_api(messages,base_url,api_key, resolved_model);

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

            for(auto it:message["tool_calls"]){
                json tool = it.get<json>();
                std::string tool_name = tool["function"]["name"].get<std::string>();
                json function_args = json::parse(tool["function"]["arguments"].get<std::string>());

                if(tool_name == "Read"){
                    read_tool(tool,function_args,messages);
                }
                else if(tool_name == "Write"){
                    write_tool(tool,function_args,messages);
                }
                else if(tool_name == "Bash"){
                    command_tool(tool,function_args,messages);
                }
                else if(tool_name == "List_Skills"){
                    list_skills_tool(tool,function_args,messages);
                }
            }
        }
    }
    return 0;
}
