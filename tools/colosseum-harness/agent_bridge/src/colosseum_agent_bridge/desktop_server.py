from __future__ import annotations

import base64
import json
from typing import Any

from mcp import types
from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from .desktop_control import ColosseumDesktopController, DesktopControlError
from .cursortouch import ScreenshotEvidence


SERVER_NAME = "colosseum-desktop-bridge"
SERVER_INSTRUCTIONS = (
    "This server is a guarded Colosseum-only desktop-control adapter over CursorTouch/windows-mcp. "
    "Claim control before observation or input. Coordinates are relative to the pinned Colosseum HWND. "
    "Every mutating desktop action creates before/after visual evidence and blocks the next action until "
    "colosseum_desktop_confirm records a visual verdict. UI Automation is advisory; screenshots are the "
    "visual evidence. Release control when the bounded journey is complete."
)


def _tool_error(exc: DesktopControlError) -> ToolError:
    return ToolError(
        json.dumps(
            {
                "code": exc.code,
                "message": str(exc),
                "retryable": exc.retryable,
                "details": exc.details,
            },
            ensure_ascii=False,
            sort_keys=True,
        )
    )


def _content(
    summary: dict[str, Any],
    screenshot: ScreenshotEvidence,
) -> list[types.TextContent | types.ImageContent]:
    return [
        types.TextContent(
            text=json.dumps(summary, indent=2, ensure_ascii=False, sort_keys=True)
        ),
        types.ImageContent(
            data=base64.b64encode(screenshot.data).decode("ascii"),
            mimeType=screenshot.mime_type,
        ),
    ]


def build_desktop_server(controller: ColosseumDesktopController) -> MCPServer:
    mcp = MCPServer(SERVER_NAME, instructions=SERVER_INSTRUCTIONS)

    @mcp.tool()
    async def colosseum_desktop_status() -> dict[str, Any]:
        """Show the Colosseum window and current shared desktop-controller lease."""
        try:
            return controller.status()
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_claim(
        controller_id: str,
        ttl_seconds: int = 300,
        run_id: str | None = None,
    ) -> dict[str, Any]:
        """Claim Colosseum desktop control; run_id pins the HWND to that run's recorded PID."""
        try:
            return controller.claim(
                controller_id,
                ttl_seconds=ttl_seconds,
                run_id=run_id,
            )
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_observe(
        controller_id: str,
    ) -> list[types.TextContent | types.ImageContent]:
        """Focus the pinned Colosseum window and return scoped UIA plus a current screenshot."""
        try:
            summary, screenshot = await controller.observe(controller_id)
            return _content(summary, screenshot)
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_click(
        controller_id: str,
        x: int,
        y: int,
        expect_text: str | None = None,
    ) -> list[types.TextContent | types.ImageContent]:
        """Click a Colosseum-relative coordinate and require visual confirmation before any next action."""
        try:
            summary, screenshot = await controller.click(
                controller_id,
                x=x,
                y=y,
                expect_text=expect_text,
            )
            return _content(summary, screenshot)
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_type(
        controller_id: str,
        x: int,
        y: int,
        text: str,
        clear: bool = False,
        expect_text: str | None = None,
    ) -> list[types.TextContent | types.ImageContent]:
        """Type into a Colosseum-relative point, capture after-state pixels, then require visual confirmation."""
        try:
            summary, screenshot = await controller.type_text(
                controller_id,
                x=x,
                y=y,
                text=text,
                clear=clear,
                expect_text=expect_text,
            )
            return _content(summary, screenshot)
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_shortcut(
        controller_id: str,
        shortcut: str,
        expect_text: str | None = None,
    ) -> list[types.TextContent | types.ImageContent]:
        """Send one keyboard shortcut to the pinned Colosseum window and require visual confirmation."""
        try:
            summary, screenshot = await controller.shortcut(
                controller_id,
                shortcut=shortcut,
                expect_text=expect_text,
            )
            return _content(summary, screenshot)
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_confirm(
        controller_id: str,
        action_id: str,
        verdict: str,
        note: str | None = None,
    ) -> dict[str, Any]:
        """Record the model's screenshot review for the pending action: pass, fail, or uncertain."""
        try:
            return controller.confirm(
                controller_id,
                action_id=action_id,
                verdict=verdict,
                note=note,
            )
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    @mcp.tool()
    async def colosseum_desktop_release(
        controller_id: str,
    ) -> dict[str, Any]:
        """Release Colosseum desktop control; pending unreviewed actions block release."""
        try:
            return controller.release(controller_id)
        except DesktopControlError as exc:
            raise _tool_error(exc) from exc

    return mcp
