"""Small read-only project overview server built with the official MCP Python SDK."""
from pathlib import Path
from mcp.server.fastmcp import FastMCP

server = FastMCP("Kavish project demo")
ROOT = Path(__file__).resolve().parents[1]

@server.tool()
def project_info() -> dict:
    """Return this project's name, purpose, and implementation language."""
    return {"name": "Kavish Code", "language": "C++23",
            "purpose": "Terminal coding assistant with local MCP tools"}

@server.tool()
def source_summary() -> dict:
    """List C++ source/header files and line counts in this project's src directory."""
    files = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.is_file() and path.suffix in {".cpp", ".hpp"}:
            files.append({"path": str(path.relative_to(ROOT)),
                          "lines": len(path.read_text().splitlines())})
    return {"files": files, "total_lines": sum(item["lines"] for item in files)}

if __name__ == "__main__":
    server.run(transport="stdio")
