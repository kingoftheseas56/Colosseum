from __future__ import annotations

import json
import os
import sys
from pathlib import Path


def main() -> int:
    operation = sys.argv[1] if len(sys.argv) > 1 else ""
    arguments = sys.argv[2:]
    trace_path = os.environ.get("COLOSSEUM_FAKE_TRACE")
    if trace_path:
        Path(trace_path).write_text(
            json.dumps(sys.argv[1:]), encoding="utf-8"
        )

    if operation == "test" and "backend-fail" in arguments:
        print(json.dumps({"ok": False, "command": "test", "data": {}}))
        return 7

    result = {
        "ok": True,
        "command": operation,
        "repo": {"root": "C:/fake/Colosseum", "head": "abc123", "dirty": []},
        "data": {"arguments": arguments},
        "evidence": ["fake-cli"],
        "warnings": [],
    }
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
