#ifndef WEB_SEARCH_HPP
#define WEB_SEARCH_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace web {
    // Perform a web search using the Brave Search API and return formatted text.
    std::string perform_web_search(const std::string& query, const std::string& api_key);
}

#endif
