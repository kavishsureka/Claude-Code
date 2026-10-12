#include "builtin_tools.hpp"
#include "ui.hpp"
#include "web_search.hpp"
#include <cstdlib>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <vector>

using json = nlohmann::json;

namespace {
std::string read_tool(const json& function_args){
    std::string file_path = function_args.at("file_path").get<std::string>();
    ui::print_tool_call("Read", file_path);
    std::ifstream file(file_path);
    if(!file.is_open()){
        ui::print_error("Could not open the file!");
        return "Error: Could not open the file!";
    }
    std::string content;
    std::string line;
    while(std::getline(file,line)){
        content += line + "\n";
    }
    file.close();
    return content;
}

std::string write_tool(const json& function_args){
    std::string file_path = function_args.at("file_path").get<std::string>();
    std::string content = function_args.at("content").get<std::string>();
    ui::print_tool_call("Write", file_path);
    std::ofstream file(file_path,std::ios::out | std::ios::trunc);

    if (!file) {
        ui::print_error("Failed to open file for writing.");
        return "Error: Failed to open file for writing.";
    }
    file << content;
    file.close();

    return content;
}

std::string command_tool(const json& function_args){
    std::string command = function_args.at("command").get<std::string>();
    ui::print_tool_call("Bash", command);
    command += " 2>&1";  //redirect stderr into stdout. 2 means stderr,> means redirect, &1 means wherever stdout currently points”

    FILE *pipe = popen(command.c_str(),"r");
    if(!pipe){
        ui::print_error("Error opening pipe.");
        return "Error: Could not open pipe to execute command.";
    }
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    return result;
}

std::string list_skills_tool(const json&) {
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
    
    return result;
}

std::string web_search_tool(const json& function_args) {
    std::string query = function_args.at("query").get<std::string>();
    ui::print_tool_call("Web_Search", query);
    
    const char* env_key = std::getenv("TAVILY_API_KEY");
    std::string tavily_key = env_key ? env_key : "";
    
    std::string result = web::perform_web_search(query, tavily_key);
    
    return result;
}


} // namespace

ToolRegistry make_builtin_registry() {
    ToolRegistry registry;
    const json definitions = json::array({
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
                    {"name", "Web_Search"},
                    {"description", "Perform a web search to find real-time information. Use this when the user asks about current events or needs factual information from the internet."},
                    {"parameters", {
                        {"type", "object"},
                        {"required", json::array({"query"})},
                        {"properties", {
                            {"query", {
                                {"type", "string"},
                                {"description", "The search query to look up on the web"}
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
        });
    const std::vector<ToolRegistry::Handler> handlers = {
        read_tool, write_tool, command_tool, web_search_tool, list_skills_tool
    };
    for (std::size_t i = 0; i < handlers.size(); ++i) {
        registry.add(definitions.at(i), handlers[i]);
    }
    return registry;
}
