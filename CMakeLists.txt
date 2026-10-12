cmake_minimum_required(VERSION 3.13)

project(codecrafters-claude-code)

set(CMAKE_CXX_STANDARD 23) # Enable the C++23 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT UNIX)
    message(FATAL_ERROR "The MCP stdio transport currently supports macOS and Linux")
endif()

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(assistant_core
    src/tool_registry.cpp
    src/app_config.cpp
    src/mcp/stdio_transport.cpp
)
target_include_directories(assistant_core PUBLIC src)
target_link_libraries(assistant_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)

add_executable(claude-code src/main.cpp src/builtin_tools.cpp src/config.cpp src/ui.cpp src/web_search.cpp)
target_link_libraries(claude-code PRIVATE assistant_core cpr::cpr)

include(CTest)
if(BUILD_TESTING)
    add_executable(mcp_foundation_tests tests/mcp_foundation_tests.cpp src/builtin_tools.cpp src/ui.cpp src/web_search.cpp)
    target_link_libraries(mcp_foundation_tests PRIVATE assistant_core cpr::cpr)
    add_test(NAME mcp_foundations COMMAND mcp_foundation_tests)
    set_tests_properties(mcp_foundations PROPERTIES TIMEOUT 20)
endif()
