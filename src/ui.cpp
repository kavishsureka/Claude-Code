#include "ui.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

namespace ui {

    std::atomic<bool> spinner_running{false};
    std::thread spinner_thread;

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

    void print_reasoning(const std::string& reasoning) {
        std::cout << "\n" << "\033[90m" /* Dark Gray */ << "  [Thinking...]\n";
        std::cout << "  " << reasoning << "\n" << RESET << std::endl;
    }

    void start_spinner(const std::string& text) {
        if (spinner_running) return;
        spinner_running = true;
        spinner_thread = std::thread([text]() {
            const char chars[] = {'|', '/', '-', '\\'};
            int i = 0;
            while (spinner_running) {
                std::cout << "\r" << CYAN << chars[i] << " " << text << "..." << RESET << std::flush;
                i = (i + 1) % 4;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Clear the line when done
            std::cout << "\r\033[K" << std::flush;
        });
    }

    void stop_spinner() {
        if (!spinner_running) return;
        spinner_running = false;
        if (spinner_thread.joinable()) {
            spinner_thread.join();
        }
    }

}
