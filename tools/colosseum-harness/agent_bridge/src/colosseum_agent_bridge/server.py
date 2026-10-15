from __future__ import annotations

import json
from typing import Any, Sequence

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from .backend import BackendError, HarnessBackend

JsonObject = dict[str, Any]

SERVER_NAME = "colosseum-agent-bridge"
SERVER_INSTRUCTIONS = (
    "This server provides bounded Colosseum semantic context, not generic execution. "
    "Use colosseum_context_for_task before substantive architectural work. "
    "Treat stale or invalid intelligence as non-authoritative. "
    "Use the host's native filesystem, shell, Git, build, and test tools for execution. "
    "Use task-scoped colosseum_verify before completion. "
    "Lanista remains the runtime-journey authority."
)


def build_server(backend: HarnessBackend) -> MCPServer:
    """Build the MCP server around one replaceable Colosseum harness backend."""

    mcp = MCPServer(SERVER_NAME, instructions=SERVER_INSTRUCTIONS)

    async def invoke(
        operation: str, arguments: Sequence[str] = ()
    ) -> JsonObject:
        try:
            result = await backend.call(operation, arguments)
        except BackendError as exc:
            raise ToolError(str(exc)) from exc

        if not isinstance(result, dict):
            raise ToolError("Colosseum harness backend returned a non-object result")
        if result.get("ok") is False:
            raise ToolError(
                "Colosseum harness backend reported failure: "
                + json.dumps(result, ensure_ascii=False, sort_keys=True)
            )
        return result

    def require_text(value: str, label: str) -> str:
        if not value.strip():
            raise ToolError(f"{label} must not be empty")
        return value

    @mcp.tool()
    async def colosseum_status() -> JsonObject:
        """Return current Colosseum repository/build/harness status."""
        return await invoke("status")

    @mcp.tool()
    async def colosseum_inspect(target: str) -> JsonObject:
        """Inspect a Colosseum domain or path through the underlying harness."""
        return await invoke("inspect", (require_text(target, "target"),))

    @mcp.tool()
    async def colosseum_context_for_task(
        task: str,
        paths: list[str] | None = None,
        domain: str | None = None,
    ) -> JsonObject:
        """Return bounded fresh Colosseum context for one engineering task."""
        arguments = [require_text(task, "task")]
        for path in paths or []:
            arguments.extend(("--path", require_text(path, "path")))
        if domain is not None:
            arguments.extend(("--domain", require_text(domain, "domain")))
        return await invoke("context-for-task", arguments)

    @mcp.tool()
    async def colosseum_test(
        selector: str,
        dry_run: bool = True,
    ) -> JsonObject:
        """Resolve or run an existing Colosseum test surface."""
        arguments = [require_text(selector, "selector")]
        arguments.append("--dry-run" if dry_run else "--run")
        return await invoke("test", arguments)

    @mcp.tool()
    async def colosseum_journeys() -> JsonObject:
        """Enumerate existing Colosseum Lanista/runtime journeys."""
        return await invoke("journeys")

    @mcp.tool()
    async def colosseum_journey(
        name: str,
        dry_run: bool = True,
    ) -> JsonObject:
        """Resolve or run one existing Colosseum journey."""
        arguments = [require_text(name, "name")]
        arguments.append("--dry-run" if dry_run else "--run")
        return await invoke("journey", arguments)

    @mcp.tool()
    async def colosseum_verify(
        dry_run: bool = True,
        paths: list[str] | None = None,
    ) -> JsonObject:
        """Propose or run the Colosseum verification surface chosen by the harness."""
        arguments: list[str] = []
        for path in paths or []:
            arguments.extend(("--path", require_text(path, "path")))
        arguments.append("--dry-run" if dry_run else "--run")
        return await invoke("verify", arguments)

    return mcp
