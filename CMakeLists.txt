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
    src/mcp/client.cpp
    src/mcp/manager.cpp
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
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_executable(mcp_client_tests tests/mcp_client_tests.cpp)
        target_link_libraries(mcp_client_tests PRIVATE assistant_core)
        add_test(NAME mcp_client COMMAND mcp_client_tests ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_fixture.py)
        set_tests_properties(mcp_client PROPERTIES TIMEOUT 30)
        add_test(NAME mcp_chat COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_chat_test.py
            $<TARGET_FILE:claude-code> ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_fixture.py)
        set_tests_properties(mcp_chat PROPERTIES TIMEOUT 30)
        set(MCP_SDK_PYTHON "" CACHE FILEPATH "Optional Python interpreter with the official MCP SDK installed")
        if(MCP_SDK_PYTHON)
            add_test(NAME mcp_sdk_interop COMMAND mcp_client_tests ${MCP_SDK_PYTHON}
                ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_server.py --sdk)
            set_tests_properties(mcp_sdk_interop PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
