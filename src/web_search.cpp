#include "web_search.hpp"
#include <cpr/cpr.h>
#include <iostream>
#include "ui.hpp"

using json = nlohmann::json;

namespace web {
    std::string perform_web_search(const std::string& query, const std::string& api_key) {
        if (api_key.empty()) {
            return "Error: TAVILY_API_KEY is not set. Cannot perform web search.";
        }

        ui::start_spinner("Searching the web for: " + query);
        
        json request_body = {
            {"api_key", api_key},
            {"query", query}
        };

        cpr::Response response = cpr::Post(
            cpr::Url{"https://api.tavily.com/search"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{request_body.dump()}
        );
        ui::stop_spinner();

        if (response.status_code != 200) {
            return "Error: Web search failed with HTTP " + std::to_string(response.status_code) + "\nResponse: " + response.text;
        }

        try {
            json result = json::parse(response.text);
            if (!result.contains("results") || !result["results"].is_array()) {
                return "No search results found.";
            }

            std::string formatted_results = "Web Search Results for '" + query + "':\n\n";
            int count = 1;
            
            for (const auto& item : result["results"]) {
                std::string title = item.value("title", "No Title");
                std::string url = item.value("url", "No URL");
                std::string description = item.value("content", "No Description");
                
                formatted_results += std::to_string(count) + ". " + title + "\n";
                formatted_results += "   URL: " + url + "\n";
                formatted_results += "   Snippet: " + description + "\n\n";
                
                count++;
                if (count > 5) break; // Return top 5 results
            }

            return formatted_results;
        } catch (...) {
            return "Error: Failed to parse search results.";
        }
    }
}
