"""Contract tests for the generated Arc 41 keyboard/action and scroll censuses."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALID_CLASSIFICATIONS = {"COVERED", "DELEGATED", "EXCEPTION", "BUG"}
ACTION_FIELDS = {"file", "line", "pointer_type", "gesture", "owner", "classification", "evidence"}
SCROLL_FIELDS = {"file", "line", "surface_type", "owner", "classification", "evidence"}


def read_json(relative: str) -> dict:
    path = ROOT / relative
    assert path.exists(), f"missing generated artifact: {relative}"
    return json.loads(path.read_text(encoding="utf-8"))


def assert_rows(payload: dict, fields: set[str], label: str) -> None:
    rows = payload.get("rows")
    assert isinstance(rows, list) and rows, f"{label}: rows must be a non-empty list"
    assert payload.get("schemaVersion") == 1, f"{label}: schemaVersion must be 1"
    assert payload.get("filesScanned", 0) >= 200, f"{label}: census must cover the production QML tree"
    for row in rows:
        assert fields <= row.keys(), f"{label}: incomplete row {row}"
        assert row["classification"] in VALID_CLASSIFICATIONS, f"{label}: invalid classification"
        assert row["file"].startswith("qml/"), f"{label}: row escaped the QML root"
        assert (ROOT / row["file"]).exists(), f"{label}: missing source {row['file']}"
        assert row["line"] > 0, f"{label}: invalid source line"
        assert row["owner"], f"{label}: missing ownership evidence"
        assert row["evidence"], f"{label}: missing evidence"


def test_committed_census_has_no_unexplained_bug_rows():
    action = read_json("docs/keyboard/arc41-action-census.json")
    scroll = read_json("docs/keyboard/arc41-scroll-census.json")
    assert_rows(action, ACTION_FIELDS, "action census")
    assert_rows(scroll, SCROLL_FIELDS, "scroll census")
    for label, payload in (("action", action), ("scroll", scroll)):
        bugs = [row for row in payload["rows"] if row["classification"] == "BUG"]
        assert not bugs, f"{label} census contains unexplained BUG rows: {bugs[:3]}"


def test_generator_reproduces_the_committed_action_shape():
    script = ROOT / "scripts/keyboard_action_census.py"
    assert script.exists(), "action census generator is missing"
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "action.json"
        result = subprocess.run(
            [sys.executable, str(script), "--qml-root", str(ROOT / "qml"), "--output", str(output)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        generated = json.loads(output.read_text(encoding="utf-8"))
        committed = read_json("docs/keyboard/arc41-action-census.json")
        assert generated["filesScanned"] == committed["filesScanned"]
        assert generated["pointerTypes"] == committed["pointerTypes"]
        assert generated["rows"] == committed["rows"]


if __name__ == "__main__":
    test_committed_census_has_no_unexplained_bug_rows()
    test_generator_reproduces_the_committed_action_shape()
    print("KEYBOARD_CENSUS_CONTRACT_OK")
