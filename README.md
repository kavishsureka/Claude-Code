# AI Terminal Assistant (Claude Code Clone)

This is an AI coding assistant built in C++ that uses Large Language Models (LLMs) to understand code and perform actions through tool calls directly in your terminal. It serves as a lightweight, custom implementation inspired by Anthropic's Claude Code.

The assistant is capable of:
- Understanding and engaging in natural language conversations about your codebase.
- Using tools to interact with your system:
  - Reading files
  - Writing files
  - Executing bash commands

## Prerequisites

- C++23 compatible compiler
- [CMake](https://cmake.org/) (version 3.13+)
- [vcpkg](https://github.com/microsoft/vcpkg) (optional but recommended for dependency management)

### Dependencies
- `cpr` (C++ Requests for HTTP calls)
- `nlohmann_json` (JSON parser)

## Setup

This project uses the OpenAI-compatible OpenRouter API for LLM interactions.

1. **Get an OpenRouter API Key**: Sign up at [OpenRouter.ai](https://openrouter.ai/) and generate an API key.
2. **Set Environment Variables**: In your terminal, set your API key and optionally the model you wish to use (defaults to Nemotron 3 Super 120B for fast, free tool-calling).

```bash
export OPENROUTER_API_KEY="your-api-key-here"
export OPENROUTER_MODEL="nvidia/nemotron-3-super-120b-a12b:free"
```

## Build and Run

A convenient shell script is provided to automatically configure CMake, build the executable, and run it.

```bash
./build_and_run.sh -p "Look at the files in this directory and tell me what you see"
```

### Manual Build

If you prefer to build manually:

```bash
mkdir -p build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build ./build
./build/claude-code -p "Your prompt here"
```
