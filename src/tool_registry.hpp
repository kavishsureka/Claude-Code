#pragma once

#include <functional>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

// Both built-in and future MCP tools use the same definition + handler contract.
class ToolRegistry {
public:
    using Json = nlohmann::json;
    using Handler = std::function<std::string(const Json&)>;

    void add(Json definition, Handler handler);
    Json definitions() const;
    Json dispatch(const Json& tool_call) const;

private:
    struct Entry { Json definition; Handler handler; };
    std::map<std::string, Entry> entries_;
};
