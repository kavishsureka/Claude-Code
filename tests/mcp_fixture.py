"""Deterministic JSON-RPC peer for failure tests; no external packages."""
import json
import sys
import time

mode = sys.argv[1]
initialized = False
calls = 0

def send(value):
    print(json.dumps(value), flush=True)

def result(id, value):
    send({"jsonrpc": "2.0", "id": id, "result": value})

def tool(name):
    return {"name": name, "description": "Fixture echo", "inputSchema": {
        "type": "object", "properties": {"text": {"type": ["string", "null"]}}}}

for line in sys.stdin:
    message = json.loads(line)
    method = message.get("method")
    id = message.get("id")
    if method == "initialize":
        if mode == "timeout-start":
            time.sleep(10)
        assert message["params"]["capabilities"] == {}
        result(id, {"protocolVersion": "bad" if mode == "version" else "2025-11-25",
                    "capabilities": {} if mode == "no-tools" else {"tools": {"listChanged": True}},
                    "serverInfo": {"name": "fixture", "version": "1"}})
    elif method == "notifications/initialized":
        initialized = True
    elif method == "tools/list":
        assert initialized, "tools/list before initialized"
        if mode == "pagination-loop":
            result(id, {"tools": [], "nextCursor": "same"})
        elif mode == "duplicate":
            result(id, {"tools": [tool("echo"), tool("echo")]})
        elif mode == "schema":
            bad = tool("echo")
            bad["inputSchema"] = None
            result(id, {"tools": [bad]})
        elif mode == "names":
            result(id, {"tools": [tool("a.b"), tool("a_b"), tool("x" * 120)]})
        elif "cursor" not in message["params"]:
            result(id, {"tools": [tool("echo")], "nextCursor": "page2"})
        else:
            result(id, {"tools": [tool("new" if calls else "extra")]})
    elif method == "tools/call":
        calls += 1
        if mode == "exit":
            sys.exit(1)
        if mode == "timeout":
            time.sleep(10)
        if mode == "wrong-id":
            result(id + 1, {"content": []})
            continue
        if mode == "malformed":
            print("not json", flush=True)
            continue
        if mode == "rpc-error":
            send({"jsonrpc": "2.0", "id": id, "error": {"code": -32602, "message": "fixture rejected args"}})
            continue
        if mode == "invalid-result":
            result(id, {"content": "bad"})
            continue
        if mode == "notify":
            send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})
            send({"jsonrpc": "2.0", "method": "notifications/message", "params": {"data": "log"}})
            send({"jsonrpc": "2.0", "id": "ping-1", "method": "ping"})
            assert json.loads(next(sys.stdin))["result"] == {}
            send({"jsonrpc": "2.0", "id": "unknown-1", "method": "sampling/createMessage"})
            assert json.loads(next(sys.stdin))["error"]["code"] == -32601
        text = message["params"]["arguments"].get("text", "ok")
        content = [{"type": "text", "text": text or "null"}]
        if mode == "resource":
            content += [{"type": "resource_link", "uri": "file:///example"},
                        {"type": "resource", "resource": {"text": "not-forwarded"}}]
        if mode == "media":
            content += [{"type": "image", "data": "not-forwarded", "mimeType": "image/png"}]
        result(id, {"content": content, "structuredContent": {"calls": calls}, "isError": mode == "tool-error"})
