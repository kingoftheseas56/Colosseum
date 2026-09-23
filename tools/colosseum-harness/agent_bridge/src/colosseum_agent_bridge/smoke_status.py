from __future__ import annotations

import asyncio
import json

from mcp import Client

from .backend import JsonCliBackend, command_prefix_from_env, timeout_from_env
from .server import build_server


async def _run() -> int:
    backend = JsonCliBackend(
        command_prefix_from_env(),
        timeout_seconds=timeout_from_env(),
    )
    async with Client(build_server(backend)) as client:
        result = await client.call_tool("colosseum_status", {})
    if result.structured_content is not None:
        print(json.dumps(result.structured_content, indent=2, ensure_ascii=False))
    else:
        for block in result.content:
            text = getattr(block, "text", None)
            if text:
                print(text)
    return 1 if result.is_error else 0


def main() -> None:
    raise SystemExit(asyncio.run(_run()))


if __name__ == "__main__":
    main()
