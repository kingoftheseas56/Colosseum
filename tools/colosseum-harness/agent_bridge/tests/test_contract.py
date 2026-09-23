from __future__ import annotations

import asyncio
import json
import sys
from pathlib import Path
from typing import Sequence

import pytest
from mcp import Client, StdioServerParameters

from colosseum_agent_bridge.backend import (
    BackendError,
    JsonCliBackend,
    command_prefix_from_env,
    timeout_from_env,
)
from colosseum_agent_bridge.server import build_server

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
FAKE_CLI = ROOT / "tests" / "fake_cli.py"


class FakeBackend:
    def __init__(self) -> None:
        self.calls: list[tuple[str, tuple[str, ...]]] = []
        self.error: BackendError | None = None
        self.overrides: dict[str, dict[str, object]] = {}

    async def call(
        self, operation: str, arguments: Sequence[str] = ()
    ) -> dict[str, object]:
        self.calls.append((operation, tuple(arguments)))
        if self.error is not None:
            raise self.error
        if operation in self.overrides:
            return self.overrides[operation]
        return {
            "ok": True,
            "command": operation,
            "repo": {"root": "C:/fake", "head": "abc", "dirty": []},
            "data": {"arguments": list(arguments)},
            "evidence": ["fake-backend"],
            "warnings": [],
        }


def run(coro):
    return asyncio.run(coro)


def result_text(result) -> str:
    return " ".join(
        getattr(block, "text", "")
        for block in result.content
        if hasattr(block, "text")
    )


def test_lists_exact_tools_and_required_optional_arguments() -> None:
    async def scenario():
        async with Client(build_server(FakeBackend())) as client:
            return await client.list_tools()

    listed = run(scenario())
    tools = {tool.name: tool for tool in listed.tools}
    assert set(tools) == {
        "colosseum_status",
        "colosseum_inspect",
        "colosseum_context_for_task",
        "colosseum_test",
        "colosseum_journeys",
        "colosseum_journey",
        "colosseum_verify",
    }

    inspect_schema = tools["colosseum_inspect"].input_schema
    assert inspect_schema["required"] == ["target"]

    context_schema = tools["colosseum_context_for_task"].input_schema
    assert context_schema["required"] == ["task"]
    assert context_schema["properties"]["paths"]["anyOf"][0]["items"]["type"] == "string"

    test_schema = tools["colosseum_test"].input_schema
    assert test_schema["required"] == ["selector"]
    assert test_schema["properties"]["dry_run"]["default"] is True

    verify_schema = tools["colosseum_verify"].input_schema
    assert "required" not in verify_schema
    assert verify_schema["properties"]["dry_run"]["default"] is True


def test_structured_result_survives_mcp_round_trip() -> None:
    backend = FakeBackend()
    payload = {
        "ok": True,
        "command": "status",
        "repo": {"root": "C:/real", "head": "deadbeef", "dirty": ["x"]},
        "data": {"nested": {"count": 2}},
        "evidence": ["observed"],
        "warnings": ["fixture"],
    }
    backend.overrides["status"] = payload

    async def scenario():
        async with Client(build_server(backend)) as client:
            return await client.call_tool("colosseum_status", {})

    result = run(scenario())
    assert result.is_error is False
    assert result.structured_content == payload


def test_routes_all_semantics_without_generic_execution() -> None:
    backend = FakeBackend()

    async def scenario():
        async with Client(build_server(backend)) as client:
            await client.call_tool("colosseum_status", {})
            await client.call_tool("colosseum_inspect", {"target": r"qml\Main.qml"})
            await client.call_tool(
                "colosseum_context_for_task",
                {
                    "task": "Fix ratings opening",
                    "paths": ["qml/Main.qml"],
                    "domain": "ratings-reviews",
                },
            )
            await client.call_tool(
                "colosseum_test", {"selector": "reader2", "dry_run": False}
            )

            await client.call_tool("colosseum_journeys", {})
            await client.call_tool(
                "colosseum_journey", {"name": "smoke-home"}
            )
            await client.call_tool("colosseum_verify", {})

    run(scenario())
    assert backend.calls == [
        ("status", ()),
        ("inspect", (r"qml\Main.qml",)),
        (
            "context-for-task",
            (
                "Fix ratings opening",
                "--path",
                "qml/Main.qml",
                "--domain",
                "ratings-reviews",
            ),
        ),
        ("test", ("reader2", "--run")),
        ("journeys", ()),
        ("journey", ("smoke-home", "--dry-run")),
        ("verify", ("--dry-run",)),
    ]


def test_dry_run_boolean_maps_to_explicit_cli_flags() -> None:
    backend = FakeBackend()

    async def scenario():
        async with Client(build_server(backend)) as client:
            await client.call_tool(
                "colosseum_test", {"selector": "reader2", "dry_run": True}
            )
            await client.call_tool(
                "colosseum_test", {"selector": "reader2", "dry_run": False}
            )
            await client.call_tool(
                "colosseum_journey", {"name": "smoke-home", "dry_run": True}
            )
            await client.call_tool(
                "colosseum_journey", {"name": "smoke-home", "dry_run": False}
            )
            await client.call_tool(
                "colosseum_verify",
                {"dry_run": True, "paths": ["native/account/A.cpp", "qml/Main.qml"]},
            )
            await client.call_tool("colosseum_verify", {"dry_run": False})

    run(scenario())
    assert backend.calls == [
        ("test", ("reader2", "--dry-run")),
        ("test", ("reader2", "--run")),
        ("journey", ("smoke-home", "--dry-run")),
        ("journey", ("smoke-home", "--run")),
        (
            "verify",
            (
                "--path",
                "native/account/A.cpp",
                "--path",
                "qml/Main.qml",
                "--dry-run",
            ),
        ),
        ("verify", ("--run",)),
    ]


def test_explicit_run_flags_survive_subprocess_backend() -> None:
    backend = JsonCliBackend((sys.executable, str(FAKE_CLI)), timeout_seconds=10)

    async def scenario():
        async with Client(build_server(backend)) as client:
            test_result = await client.call_tool(
                "colosseum_test", {"selector": "reader2", "dry_run": False}
            )
            context_result = await client.call_tool(
                "colosseum_context_for_task",
                {
                    "task": "Fix ratings opening",
                    "paths": ["qml/Main.qml"],
                },
            )
            journey_result = await client.call_tool(
                "colosseum_journey", {"name": "smoke-home", "dry_run": False}
            )
            verify_result = await client.call_tool(
                "colosseum_verify",
                {"dry_run": False, "paths": ["qml/Main.qml"]},
            )
            return test_result, context_result, journey_result, verify_result

    test_result, context_result, journey_result, verify_result = run(scenario())
    assert test_result.structured_content["data"]["arguments"] == ["reader2", "--run", "--json"]
    assert context_result.structured_content["data"]["arguments"] == [
        "Fix ratings opening",
        "--path",
        "qml/Main.qml",
        "--json",
    ]
    assert journey_result.structured_content["data"]["arguments"] == ["smoke-home", "--run", "--json"]
    assert verify_result.structured_content["data"]["arguments"] == [
        "--path",
        "qml/Main.qml",
        "--run",
        "--json",
    ]


def test_missing_and_empty_arguments_fail_explicitly() -> None:
    async def scenario():
        async with Client(build_server(FakeBackend())) as client:
            missing = await client.call_tool("colosseum_inspect", {})
            empty = await client.call_tool(
                "colosseum_test", {"selector": "   "}
            )
            return missing, empty

    missing, empty = run(scenario())
    assert missing.is_error is True
    assert empty.is_error is True
    assert "selector must not be empty" in result_text(empty)


def test_backend_error_and_failure_envelope_are_not_false_success() -> None:
    backend = FakeBackend()
    backend.error = BackendError("fixture backend exploded")

    async def error_scenario():
        async with Client(build_server(backend)) as client:
            return await client.call_tool("colosseum_status", {})

    error_result = run(error_scenario())
    assert error_result.is_error is True
    assert "fixture backend exploded" in result_text(error_result)

    backend.error = None
    backend.overrides["status"] = {
        "ok": False,
        "command": "status",
        "data": {"reason": "fixture failure"},
    }

    async def failure_scenario():
        async with Client(build_server(backend)) as client:
            return await client.call_tool("colosseum_status", {})

    failure_result = run(failure_scenario())
    assert failure_result.is_error is True
    assert "reported failure" in result_text(failure_result)


def test_json_cli_backend_preserves_windowsish_argument_boundaries(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    trace = tmp_path / "argv.json"
    monkeypatch.setenv("COLOSSEUM_FAKE_TRACE", str(trace))
    backend = JsonCliBackend((sys.executable, str(FAKE_CLI)), timeout_seconds=10)
    target = r"C:\Colosseum Worktree\qml\Main.qml"

    result = run(backend.call("inspect", (target,)))
    assert result["ok"] is True
    assert json.loads(trace.read_text(encoding="utf-8")) == [
        "inspect",
        target,
        "--json",
    ]


def test_json_cli_backend_surfaces_nonzero_exit() -> None:
    backend = JsonCliBackend((sys.executable, str(FAKE_CLI)), timeout_seconds=10)
    with pytest.raises(BackendError, match="exited 7"):
        run(backend.call("test", ("backend-fail",)))


def test_startup_config_is_explicit_json_argv() -> None:
    env = {
        "COLOSSEUM_HARNESS_COMMAND_JSON": json.dumps(
            ["python", r"C:\Harness Dir\cli.py", "--root", r"C:\Colosseum"]
        ),
        "COLOSSEUM_HARNESS_TIMEOUT_SECONDS": "42.5",
    }
    assert command_prefix_from_env(env) == (
        "python",
        r"C:\Harness Dir\cli.py",
        "--root",
        r"C:\Colosseum",
    )
    assert timeout_from_env(env) == 42.5

    with pytest.raises(BackendError):
        command_prefix_from_env({})
    with pytest.raises(BackendError):
        command_prefix_from_env({"COLOSSEUM_HARNESS_COMMAND_JSON": '"shell"'})
    with pytest.raises(BackendError):
        timeout_from_env({"COLOSSEUM_HARNESS_TIMEOUT_SECONDS": "0"})


def test_stdio_server_starts_calls_and_stops_cleanly() -> None:
    command_json = json.dumps([sys.executable, str(FAKE_CLI)])
    child_env = {
        "PYTHONPATH": str(SRC),
        "COLOSSEUM_HARNESS_COMMAND_JSON": command_json,
        "COLOSSEUM_HARNESS_TIMEOUT_SECONDS": "10",
    }

    async def scenario():
        params = StdioServerParameters(
            command=sys.executable,
            args=["-m", "colosseum_agent_bridge"],
            env=child_env,
            cwd=str(ROOT),
        )
        async with Client(params) as client:
            listed = await client.list_tools()
            result = await client.call_tool("colosseum_status", {})
            return listed, result

    listed, result = run(scenario())
    assert len(listed.tools) == 7
    assert result.is_error is False
    assert result.structured_content is not None
    assert result.structured_content["command"] == "status"
