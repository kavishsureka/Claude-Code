#ifndef UI_HPP
#define UI_HPP

#include <string>

namespace ui {

    // ANSI Escape Codes for coloring
    const std::string RESET   = "\033[0m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string WHITE   = "\033[37m";
    const std::string BOLD    = "\033[1m";

    void print_welcome_banner();
    
    // Helper to print system or error messages
    void print_error(const std::string& message);
    void print_system(const std::string& message);

    // Helpers for chat loop
    void print_assistant_message(const std::string& message);
    void print_tool_call(const std::string& tool_name, const std::string& arguments);
    void print_reasoning(const std::string& reasoning);

    // Loading Spinner
    void start_spinner(const std::string& text = "Thinking");
    void stop_spinner();

}

#endif // UI_HPP
