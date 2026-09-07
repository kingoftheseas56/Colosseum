"""Deterministic replay subject; this file is materialized as LF by P04."""

import json
import os
from pathlib import Path

root = Path(os.environ["LAB_RUN_ROOT"])
response = {"status": 200, "headers": {"Content-Type": "application/json"}, "body": "expected"}
(root / "subject-response.json").write_text(json.dumps(response), encoding="utf-8")
(root / "protocol-response.txt").write_bytes(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\nexpected")
