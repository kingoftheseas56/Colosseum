from __future__ import annotations

import base64
import json
import os
import shutil
from contextlib import asynccontextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, AsyncIterator

from mcp import Client, StdioServerParameters


class CursorTouchError(RuntimeError):
    def __init__(
        self,
        code: str,
        message: str,
        *,
        retryable: bool = False,
        details: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.retryable = retryable
        self.details = details or {}


def _content_text(result: Any) -> str:
    chunks: list[str] = []
    for block in getattr(result, "content", []) or []:
        if getattr(block, "type", None) != "text":
            continue
        raw = str(getattr(block, "text", "") or "")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            chunks.append(raw)
            continue
        if isinstance(parsed, list):
            chunks.extend(str(item) for item in parsed)
        elif isinstance(parsed, str):
            chunks.append(parsed)
        else:
            chunks.append(raw)
    return "\n".join(chunks)


def scoped_colosseum_tree(snapshot_text: str) -> str:
    lines = snapshot_text.splitlines()
    start = next(
        (
            index
            for index, line in enumerate(lines)
            if 'window "Colosseum"' in line
        ),
        None,
    )
    if start is None:
        return ""
    output = [lines[start]]
    for line in lines[start + 1 :]:
        stripped = line.lstrip()
        if (
            ('window "' in stripped)
            and (stripped.startswith("├── ") or stripped.startswith("└── "))
        ):
            break
        output.append(line)
    return "\n".join(output).rstrip()


@dataclass(frozen=True, slots=True)
class ScreenshotEvidence:
    data: bytes
    mime_type: str


class CursorTouchSession:
    def __init__(self, client: Client) -> None:
        self.client = client

    async def _call(self, name: str, arguments: dict[str, Any]) -> Any:
        result = await self.client.call_tool(name, arguments)
        if getattr(result, "is_error", False):
            detail = _content_text(result) or f"{name} failed"
            raise CursorTouchError(
                "CURSORTOUCH_TOOL_ERROR",
                detail[:4096],
                retryable=True,
                details={"tool": name},
            )
        return result

    async def snapshot(self) -> str:
        result = await self._call(
            "Snapshot",
            {
                "use_vision": False,
                "use_dom": False,
                "use_annotation": False,
                "use_ui_tree": True,
            },
        )
        return _content_text(result)

    async def screenshot(self) -> ScreenshotEvidence:
        result = await self._call(
            "Screenshot",
            {"use_annotation": False},
        )
        for block in getattr(result, "content", []) or []:
            if getattr(block, "type", None) != "image":
                continue
            data = getattr(block, "data", None)
            mime_type = str(getattr(block, "mimeType", "") or "image/png")
            if not isinstance(data, str) or not data:
                continue
            try:
                decoded = base64.b64decode(data, validate=True)
            except ValueError as exc:
                raise CursorTouchError(
                    "CURSORTOUCH_PROTOCOL_ERROR",
                    "CursorTouch returned invalid screenshot data",
                ) from exc
            if not decoded:
                continue
            return ScreenshotEvidence(decoded, mime_type)
        raise CursorTouchError(
            "CURSORTOUCH_PROTOCOL_ERROR",
            "CursorTouch Screenshot returned no image",
        )

    async def click(self, x: int, y: int) -> None:
        await self._call(
            "Click",
            {"loc": [int(x), int(y)], "button": "left", "clicks": 1},
        )

    async def type_text(self, x: int, y: int, text: str, *, clear: bool) -> None:
        if clear and text == "":
            await self.click(x, y)
            await self.shortcut("ctrl+a")
            await self.shortcut("backspace")
            return
        await self._call(
            "Type",
            {
                "text": text,
                "loc": [int(x), int(y)],
                "clear": bool(clear),
                "press_enter": False,
            },
        )

    async def shortcut(self, shortcut: str) -> None:
        await self._call("Shortcut", {"shortcut": shortcut})


class CursorTouchClient:
    """Launch one bounded windows-mcp stdio child per adapter operation."""

    def __init__(
        self,
        *,
        uv_path: str | None = None,
        python_version: str | None = None,
        read_timeout_seconds: float = 90.0,
    ) -> None:
        self.uv_path = (
            uv_path
            or os.environ.get("COLOSSEUM_UV_PATH")
            or shutil.which("uv")
        )
        self.python_version = (
            python_version
            or os.environ.get("COLOSSEUM_CURSOR_TOUCH_PYTHON")
            or "3.13"
        )
        self.read_timeout_seconds = float(read_timeout_seconds)
        if not self.uv_path:
            raise CursorTouchError(
                "CURSORTOUCH_UNAVAILABLE",
                "uv was not found; CursorTouch cannot be launched",
                retryable=True,
            )

    @asynccontextmanager
    async def open(self) -> AsyncIterator[CursorTouchSession]:
        params = StdioServerParameters(
            command=str(Path(self.uv_path)),
            args=[
                "tool",
                "run",
                "--python",
                self.python_version,
                "windows-mcp",
                "serve",
                "--tools",
                "Snapshot,Screenshot,Click,Type,Shortcut",
            ],
        )
        try:
            async with Client(
                params,
                read_timeout_seconds=self.read_timeout_seconds,
            ) as client:
                yield CursorTouchSession(client)
        except CursorTouchError:
            raise
        except BaseException as exc:
            raise CursorTouchError(
                "CURSORTOUCH_CONNECTION_FAILED",
                f"CursorTouch MCP connection failed: {exc}",
                retryable=True,
            ) from exc
