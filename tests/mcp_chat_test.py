"""End-to-end agent loop test using a localhost mock model API (no credentials)."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary, python, fixture = sys.argv[1:]
requests = []
errors = []

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def respond(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.respond({"data": [{"id": "test/model"}]})

    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            requests.append(body)
            tools = {item["function"]["name"] for item in body["tools"]}
            assert {"Read", "Write", "Bash", "Web_Search", "List_Skills"} <= tools
            if len(requests) == 1:
                good = next(name for name in tools if name.startswith("mcp_healthy_echo"))
                bad = next(name for name in tools if name.startswith("mcp_crash_echo"))
                self.respond({"choices": [{"message": {"role": "assistant", "content": None,
                    "tool_calls": [{"id": "bad-id", "type": "function", "function": {
                        "name": bad, "arguments": "{}"}},
                        {"id": "good-id", "type": "function", "function": {
                        "name": good, "arguments": json.dumps({"text": "hello from model"})}}]}}]})
            else:
                assert len(requests) == 2, "Unexpected additional model call"
                results = {item["tool_call_id"]: item["content"] for item in body["messages"] if item["role"] == "tool"}
                assert results["bad-id"].startswith("Error:")
                assert "hello from model" in results["good-id"]
                assert not any(name.startswith("mcp_crash_") for name in tools)
                self.respond({"choices": [{"message": {"role": "assistant", "content": "MCP integration complete"}}]})
        except Exception as error:
            errors.append(repr(error))
            self.send_error(500)

with http.server.HTTPServer(("127.0.0.1", 0), Handler) as server, tempfile.TemporaryDirectory() as tmp:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        config = Path(tmp) / "config.json"
        config.write_text(json.dumps({"mcpServers": {
            "healthy": {"command": python, "args": [fixture, "healthy"]},
            "crash": {"command": python, "args": [fixture, "exit"]}}}))
        env = dict(os.environ, OPENROUTER_API_KEY="local-test-only",
                   OPENROUTER_BASE_URL=f"http://127.0.0.1:{server.server_port}")
        result = subprocess.run([binary, "--config", str(config), "-m", "test/model", "-p", "Use the MCP tools"],
                                input="/mcp\n/tools\nexit\n", text=True, capture_output=True, env=env, timeout=20)
        assert result.returncode == 0, result.stderr + result.stdout
        assert not errors, errors
        assert len(requests) == 2
        assert "MCP integration complete" in result.stdout
        assert "crash: unavailable:" in result.stdout
        assert "Read and return" in result.stdout, "/tools did not list tools"
        print("Chat integration passed: model -> MCP -> model, failure isolation, /mcp, /tools")
    finally:
        server.shutdown()
        thread.join()
