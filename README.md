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

## Local MCP servers

Kavish Code can launch configured stdio MCP servers, initialize them, discover
paginated tool lists, and expose those tools alongside its built-ins. Tool calls
and results flow through the existing model conversation. A failed server becomes
unavailable while other servers and built-ins continue working.

Each `mcpServers` entry in `config.json` supports:

- `command`: executable; bare names use the child's PATH.
- `args`: optional array of literal strings (no shell expansion).
- `cwd`: optional working directory; defaults to the config directory.
- `env`: optional string overrides for the inherited environment.
- `timeoutMs`: request timeout, 1–300000 ms; default 10000.

Relative executable paths and `cwd` resolve against the config directory.
Relative paths in arguments are interpreted by the child from its working
directory. Configure only trusted local programs; MCP does not sandbox them.

### Try the demo

The read-only demo uses the official Python MCP SDK and provides `project_info`
and `source_summary`. Python is required only for this demo and integration tests,
not for the C++ client itself.

From the repository directory:

```sh
python3 -m venv build/mcp-sdk-venv
build/mcp-sdk-venv/bin/python -m pip install -r examples/requirements.txt
./build/claude-code --config examples/mcp.config.json --mcp-check
./build/claude-code --config examples/mcp.config.json -p "Use the project MCP tools to describe this project and summarize its source files."
```

`--mcp-check` checks startup and tool discovery without an OpenRouter API key. It
exits nonzero if a configured server fails. Chat still requires the existing
OpenRouter environment variables. The default config keeps `mcpServers` empty;
use `--config` to select the example or your own configuration.

In chat:

- `/mcp`: last-known connection status, errors, and alias-to-server tool mappings.
- `/tools`: currently available built-in and MCP tool definitions.
- `exit` or `quit`: exit and clean up server processes.

### Scope and limitations

- macOS/Linux, local stdio, sequential calls; no remote HTTP or OAuth.
- Tools-only client; accepts protocol versions 2025-11-25, 2025-06-18, and 2025-03-26.
- Supports text and structured JSON results. Other content types, including
  resource links, embedded resources, images and audio, get an unsupported notice.
- Supports ping and handles notifications while waiting for RPC responses.
  Tools are discovered once at startup. List-change notifications are ignored;
  restart the assistant to update the catalog. There is no background reader;
  idle server failures are detected during the next exchange. `/mcp` reports last-known status, not a health probe.
- Failed connections are not automatically restarted or calls retried. Restart
  the assistant to reconnect. Tool-level errors and valid JSON-RPC errors leave
  the connection usable.
- Frames are capped at 1 MiB, model-facing tool results at 64,000 bytes, and
  discovery at 100 pages/1,000 tools per server. Task-required tools are omitted.
- Input schemas are forwarded to the model with basic local argument checks;
  full JSON Schema and output-schema validation are not implemented.

### Tests

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The foundation tests require no API credentials. If Python 3 is available, CMake
also enables protocol/routing tests and a full chat integration test using a
localhost mock model API. The latter needs permission to bind a loopback socket.

To include real SDK interoperability after installing the demo dependencies:

```sh
cmake -S . -B build -DMCP_SDK_PYTHON="$PWD/build/mcp-sdk-venv/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
```
