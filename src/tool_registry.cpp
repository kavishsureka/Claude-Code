#include "tool_registry.hpp"
#include <stdexcept>

void ToolRegistry::add(Json definition, Handler handler) {
    const auto name = definition.at("function").at("name").get<std::string>();
    if (name.empty() || definition.at("type") != "function" || !handler) {
        throw std::invalid_argument("Invalid tool registration");
    }
    if (!entries_.emplace(name, Entry{std::move(definition), std::move(handler)}).second) {
        throw std::invalid_argument("Duplicate tool: " + name);
    }
}

ToolRegistry::Json ToolRegistry::definitions() const {
    Json result = Json::array();
    for (const auto& [name, entry] : entries_) result.push_back(entry.definition);
    return result;
}

ToolRegistry::Json ToolRegistry::dispatch(const Json& tool_call) const {
    // A missing call ID is a malformed provider response: it cannot be correlated.
    const auto id = tool_call.at("id").get<std::string>();
    std::string content;
    try {
        const auto& function = tool_call.at("function");
        const auto name = function.at("name").get<std::string>();
        const auto entry = entries_.find(name);
        if (entry == entries_.end()) throw std::invalid_argument("Unknown tool: " + name);
        const auto args = Json::parse(function.at("arguments").get<std::string>());
        if (!args.is_object()) throw std::invalid_argument("Tool arguments must be an object");

        // Lightweight validation for the current built-ins, not a JSON Schema engine.
        const auto& schema = entry->second.definition.at("function").at("parameters");
        for (const auto& required : schema.value("required", Json::array())) {
            if (!args.contains(required.get<std::string>())) {
                throw std::invalid_argument("Missing argument: " + required.get<std::string>());
            }
        }
        const auto properties = schema.value("properties", Json::object());
        for (const auto& [key, property] : properties.items()) {
            if (args.contains(key) && property.value("type", "") == "string" && !args.at(key).is_string()) {
                throw std::invalid_argument("Argument must be a string: " + key);
            }
        }
        content = entry->second.handler(args);
    } catch (const std::exception& error) {
        content = std::string("Error: ") + error.what();
    }
    return {{"role", "tool"}, {"tool_call_id", id}, {"content", content}};
}
