#include "ui.hpp"
#include <iostream>

namespace ui {

    void print_welcome_banner() {
        std::cout << BOLD << CYAN;
        std::cout << "========================================\n";
        std::cout << "        WELCOME TO KAVISH CODE          \n";
        std::cout << "========================================\n" << RESET;
        std::cout << MAGENTA << "Type your prompt to start chatting. Type 'exit' to quit.\n" << RESET << std::endl;
    }

    void print_error(const std::string& message) {
        std::cerr << BOLD << RED << "[ERROR] " << RESET << RED << message << RESET << std::endl;
    }

    void print_system(const std::string& message) {
        std::cout << BOLD << YELLOW << "[SYSTEM] " << RESET << YELLOW << message << RESET << std::endl;
    }

    void print_assistant_message(const std::string& message) {
        std::cout << "\n" << BOLD << CYAN << "Assistant: " << RESET << message << "\n" << std::endl;
    }

    void print_tool_call(const std::string& tool_name, const std::string& arguments) {
        std::cout << MAGENTA << "  [Tool Call: " << tool_name << "] " << RESET << arguments << std::endl;
    }

}
