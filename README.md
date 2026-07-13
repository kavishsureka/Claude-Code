# AI Terminal Assistant (Claude Code Clone)

This is an AI coding assistant built in C++ that uses Large Language Models (LLMs) to understand code and perform actions through tool calls directly in your terminal. It serves as a lightweight, custom implementation inspired by Anthropic's Claude Code.

The assistant is capable of:
- Understanding and engaging in natural language conversations about your codebase.
- Using an extensible Skills framework to dynamically read custom Markdown guidelines and adapt to your personal workflow.
- Seamlessly switching between different LLMs via CLI flags, environment variables, or a local `config.json` file.
- Using tools to interact with your system:
  - Reading files
  - Writing files
  - Executing bash commands
  - Performing real-time Web Searches (via Tavily API) for factual information.

## Prerequisites

- C++23 compatible compiler
- [CMake](https://cmake.org/) (version 3.13+)
- [vcpkg](https://github.com/microsoft/vcpkg) (optional but recommended for dependency management)

### Dependencies
- `cpr` (C++ Requests for HTTP calls)
- `nlohmann_json` (JSON parser)

## Setup

This project uses the OpenAI-compatible OpenRouter API for LLM interactions.

2. **Get a Tavily API Key**: Sign up at [Tavily](https://tavily.com/) for a free tier API key to enable Web Search capabilities.
3. **Set Environment Variables**: In your terminal, set your API keys and optionally the model you wish to use (defaults to Llama 3 if left empty). You can also use model aliases like `sonnet`, `haiku`, `gpt4o`, etc.

```bash
export OPENROUTER_API_KEY="your-openrouter-key-here"
export TAVILY_API_KEY="tvly-your-tavily-key-here"
export OPENROUTER_MODEL="sonnet"
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
