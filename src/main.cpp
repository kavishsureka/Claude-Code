#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


cpr::Response call_api(std::vector<json>&messages,std::string base_url,std::string api_key, std::string model){
    json request_body = {
        {"model", model},
        {"messages", messages},
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
            }
        })}
    };
    cpr::Response response = cpr::Post(
        cpr::Url{base_url + "/chat/completions"},
        cpr::Header{
            {"Authorization", "Bearer " + api_key},
            {"Content-Type", "application/json"}
        },
        cpr::Body{request_body.dump()}
    );
    return response;
}

void read_tool(json tool,json function_args,std::vector<json>&messages){
    std::string file_path = function_args["file_path"].get<std::string>();
    std::ifstream file(file_path);
    if(!file.is_open()){
        std::cerr << "Could not open the file!" << std::endl;
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
    std::ofstream file(file_path,std::ios::out | std::ios::trunc);

    if (!file) {
        std::cout << "Failed to open file\n";
        return ;
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
     command += " 2>&1";  //redirect stderr into stdout. 2 means stderr,> means redirect, &1 means wherever stdout currently points”

    FILE *pipe = popen(command.c_str(),"r");
    if(!pipe){
        std::cerr << "Error opening pipe" << std::endl;
        return ;
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

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "-p") {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty()) {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");
    const char* model_env = std::getenv("OPENROUTER_MODEL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";
    std::string model = model_env ? model_env : "meta-llama/llama-3.3-70b-instruct:free";

    if (api_key.empty()) {
        std::cerr << "Error: OPENROUTER_API_KEY environment variable is not set." << std::endl;
        std::cerr << "Please set it before running the program:" << std::endl;
        std::cerr << "export OPENROUTER_API_KEY=\"your_key_here\"" << std::endl;
        return 1;
    }

    std::vector<json>messages;   
   
    messages.push_back({
        {"role", "user"}, {"content", prompt}
    });

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!" << std::endl;

    while(true){
        cpr::Response response = call_api(messages,base_url,api_key, model);

        if (response.status_code != 200) {
            std::cerr << "HTTP error: " << response.status_code << std::endl;
            return 1;
        }

        json result = json::parse(response.text);

        if (!result.contains("choices") || result["choices"].empty()) {
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }

        json message = result["choices"][0]["message"].get<json>();
        messages.push_back(message);
    
        if(!message.contains("tool_calls")||message["tool_calls"].empty()){
            std::cout << message["content"].get<std::string>();
            break;
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
        }

    }
    return 0;
}
