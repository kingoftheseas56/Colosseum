from __future__ import annotations

import asyncio
import json
from pathlib import Path

from colosseum_agent_bridge.schema_export import collect_tool_schemas

ROOT = Path(__file__).resolve().parents[1]


def test_tool_schema_snapshot_matches_server_registration() -> None:
    expected = json.loads(
        (ROOT / "tool-schemas.json").read_text(encoding="utf-8")
    )
    actual = asyncio.run(collect_tool_schemas())
    assert actual == expected
