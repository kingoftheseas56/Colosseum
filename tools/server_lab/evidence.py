"""Machine-readable P04 run receipts and raw/normalized evidence lanes."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class EvidenceSchema:
    STATUSES = {"PASS", "FAIL", "ERROR", "UNSUPPORTED", "INDETERMINATE", "NOT_RUN"}

    @classmethod
    def receipt(cls, *, result: str, **fields: Any) -> dict[str, Any]:
        if result not in cls.STATUSES:
            raise ValueError(f"unknown result status: {result}")
        required = {
            "run_id", "engine", "source", "scenario", "environment", "configuration", "fixture_identifiers",
            "request_sequence", "timestamps", "raw_lane", "normalized_lane", "observations",
            "errors", "replay",
        }
        missing = required.difference(fields)
        if missing:
            raise ValueError(f"missing receipt fields: {sorted(missing)}")
        fields.setdefault("torrent", None)
        fields.setdefault("infohash", None)
        fields.setdefault("selected_file", None)
        fields.setdefault("responses", [])
        fields.setdefault("byte_counts", [])
        fields.setdefault("peer_observations", [])
        fields.setdefault("resource_observations", [])
        return {"schema": "colosseum-server1-lab-run/v1", **fields, "result": result}

    @staticmethod
    def validate(receipt: dict[str, Any], schema: dict[str, Any]) -> None:
        missing = [field for field in schema["required"] if field not in receipt]
        if missing:
            raise ValueError(f"schema required fields missing: {missing}")
        expected_types = {"object": dict, "array": list, "string": str, "null": type(None)}
        for field, declaration in schema["properties"].items():
            declared = declaration.get("type")
            allowed = declared if isinstance(declared, list) else [declared] if declared else []
            if field in receipt and allowed and not any(isinstance(receipt[field], expected_types[item]) for item in allowed):
                raise ValueError(f"schema type mismatch for {field}")
        if receipt["schema"] != schema["properties"]["schema"]["const"]:
            raise ValueError("schema identifier mismatch")
        if receipt["result"] not in schema["properties"]["result"]["enum"]:
            raise ValueError("schema result mismatch")

    @staticmethod
    def write(path: Path, receipt: dict[str, Any]) -> None:
        schema_path = Path(__file__).parent / "schemas" / "run.schema.json"
        EvidenceSchema.validate(receipt, json.loads(schema_path.read_text(encoding="utf-8")))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
