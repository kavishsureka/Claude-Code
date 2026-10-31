#pragma once

#include <functional>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

// Both built-in and MCP tools use the same definition + handler contract.
class ToolRegistry {
public:
    using Json = nlohmann::json;
    using Handler = std::function<std::string(const Json&)>;

    void add(Json definition, Handler handler, std::function<bool()> available = {});
    void remove(const std::string& name) { entries_.erase(name); }
    bool contains(const std::string& name) const { return entries_.contains(name); }
    Json definitions() const;
    Json dispatch(const Json& tool_call) const;

private:
    struct Entry { Json definition; Handler handler; std::function<bool()> available; };
    std::map<std::string, Entry> entries_;
};
