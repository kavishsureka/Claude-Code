#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

namespace config {
    std::string resolve_alias(const std::string& input);
    bool validate_model(const std::string& model, const std::string& api_key, const std::string& base_url);
    std::string get_model_from_config(const std::string& config_path);
}

#endif
