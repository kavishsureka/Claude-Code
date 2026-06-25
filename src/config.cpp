#include "config.hpp"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "ui.hpp"

using json = nlohmann::json;

namespace config {

    std::string resolve_alias(const std::string& input) {
        static const std::unordered_map<std::string, std::string> aliases = {
            {"sonnet", "anthropic/claude-sonnet-4.6"},
            {"opus", "anthropic/claude-opus-4.7"},
            {"haiku", "anthropic/claude-haiku-4.5"},
            {"llama", "meta-llama/llama-3.3-70b-instruct:free"},
            {"nemotron", "nvidia/nemotron-3-super-120b-a12b:free"},
            {"gpt4o", "openai/gpt-4o"},
            {"gpt4o-mini", "openai/gpt-4o-mini"},
            {"gemini", "google/gemini-pro-1.5"},
            {"deepseek", "deepseek/deepseek-coder"}
        };

        auto it = aliases.find(input);
        if (it != aliases.end()) {
            return it->second;
        }
        return input; // If not an alias, return the input itself
    }

    bool validate_model(const std::string& model, const std::string& api_key, const std::string& base_url) {
        ui::start_spinner("Validating model availability");
        cpr::Response response = cpr::Get(
            cpr::Url{base_url + "/models"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key}
            }
        );
        ui::stop_spinner();

        if (response.status_code != 200) {
            ui::print_error("Failed to fetch models from API. HTTP Error: " + std::to_string(response.status_code));
            return false;
        }

        try {
            json result = json::parse(response.text);
            if (result.contains("data") && result["data"].is_array()) {
                for (const auto& m : result["data"]) {
                    if (m["id"] == model) {
                        return true;
                    }
                }
            }
        } catch (...) {
            ui::print_error("Failed to parse models response.");
            return false;
        }

        return false;
    }

    std::string get_model_from_config(const std::string& config_path) {
        std::ifstream file(config_path);
        if (file.is_open()) {
            try {
                json j;
                file >> j;
                if (j.contains("model") && j["model"].is_string()) {
                    return j["model"].get<std::string>();
                }
            } catch (...) {
                // Return silently, default to next precedence
            }
        }
        return "";
    }
}
