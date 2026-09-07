"""Scenario fixtures and result classification for P04."""

from __future__ import annotations

from typing import Any


MODES = {"byte", "status", "header", "timeout", "crash", "missing-evidence", "pass"}


def expected_response(mode: str) -> dict[str, Any]:
    return {"status": 200, "headers": {"Content-Type": "application/json"}, "body": "expected"}


def classify(mode: str, response: dict[str, Any] | None, *, timed_out: bool = False, exit_code: int | None = 0) -> tuple[str, list[str]]:
    if timed_out:
        return "ERROR", ["subject timed out"]
    if exit_code not in (0, None):
        return "ERROR", [f"subject exited with {exit_code}"]
    if response is None:
        return "INDETERMINATE", ["required subject evidence is missing"]
    expected = expected_response(mode)
    errors = []
    if response.get("status") != expected["status"]:
        errors.append("status mismatch")
    if response.get("headers", {}).get("Content-Type") != expected["headers"]["Content-Type"]:
        errors.append("header mismatch")
    if response.get("body") != expected["body"]:
        errors.append("byte mismatch")
    return ("FAIL", errors) if errors else ("PASS", [])
