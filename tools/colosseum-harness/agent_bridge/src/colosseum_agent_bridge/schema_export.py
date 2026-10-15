from __future__ import annotations

import asyncio
import json
from typing import Sequence

from mcp import Client

from .server import build_server


class _NeverCalledBackend:
    async def call(
        self, operation: str, arguments: Sequence[str] = ()
    ) -> dict[str, object]:
        raise AssertionError("schema export must not invoke the harness backend")


async def collect_tool_schemas() -> dict[str, object]:
    async with Client(build_server(_NeverCalledBackend())) as client:
        listed = await client.list_tools()

    tools = []
    for tool in sorted(listed.tools, key=lambda item: item.name):
        tools.append(
            {
                "name": tool.name,
                "description": tool.description,
                "inputSchema": tool.input_schema,
                "outputSchema": tool.output_schema,
            }
        )

    return {
        "sdk": "mcp==2.2.0",
        "source": "generated from MCPServer registration",
        "tools": tools,
    }


def main() -> None:
    print(
        json.dumps(
            asyncio.run(collect_tool_schemas()),
            indent=2,
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
