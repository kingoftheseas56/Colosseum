from __future__ import annotations

import asyncio

from mcp import Client

from colosseum_agent_bridge.cursortouch import ScreenshotEvidence
from colosseum_agent_bridge.desktop_control import DesktopControlError
from colosseum_agent_bridge.desktop_server import build_desktop_server


def run(coro):
    return asyncio.run(coro)


class FakeController:
    def __init__(self):
        self.last_claim = None

    def status(self):
        return {"windows": [{"hwnd": 101}], "lease": None}

    def claim(self, controller_id, *, ttl_seconds=120, run_id=None):
        self.last_claim = (controller_id, ttl_seconds, run_id)
        return {"claimed": True, "controllerId": controller_id, "hwnd": 101}

    async def observe(self, controller_id):
        return (
            {"controllerId": controller_id, "verificationState": "OBSERVATION"},
            ScreenshotEvidence(b"png", "image/png"),
        )

    async def click(self, controller_id, *, x, y, expect_text=None):
        return (
            {
                "controllerId": controller_id,
                "actionId": "a1",
                "screenPoint": [x, y],
                "expectText": expect_text,
                "verificationState": "PENDING_VISUAL_REVIEW",
            },
            ScreenshotEvidence(b"after", "image/png"),
        )

    async def type_text(self, controller_id, *, x, y, text, clear=False, expect_text=None):
        return (
            {
                "controllerId": controller_id,
                "actionId": "a2",
                "verificationState": "PENDING_VISUAL_REVIEW",
            },
            ScreenshotEvidence(b"typed", "image/png"),
        )

    async def shortcut(self, controller_id, *, shortcut, expect_text=None):
        return (
            {
                "controllerId": controller_id,
                "actionId": "a3",
                "verificationState": "PENDING_VISUAL_REVIEW",
            },
            ScreenshotEvidence(b"shortcut", "image/png"),
        )

    def confirm(self, controller_id, *, action_id, verdict, note=None):
        return {
            "controllerId": controller_id,
            "actionId": action_id,
            "verdict": verdict,
            "nextActionAllowed": True,
        }

    def release(self, controller_id):
        return {"released": True, "controllerId": controller_id}


def test_desktop_server_exposes_only_guarded_surface() -> None:
    async def scenario():
        async with Client(build_desktop_server(FakeController())) as client:
            return await client.list_tools()

    listed = run(scenario())
    assert {tool.name for tool in listed.tools} == {
        "colosseum_desktop_status",
        "colosseum_desktop_claim",
        "colosseum_desktop_observe",
        "colosseum_desktop_click",
        "colosseum_desktop_type",
        "colosseum_desktop_shortcut",
        "colosseum_desktop_confirm",
        "colosseum_desktop_release",
    }


def test_desktop_claim_forwards_optional_run_id_without_adding_a_tool() -> None:
    controller = FakeController()

    async def scenario():
        async with Client(build_desktop_server(controller)) as client:
            return await client.call_tool(
                "colosseum_desktop_claim",
                {
                    "controller_id": "controller-a",
                    "ttl_seconds": 300,
                    "run_id": "run_" + "a" * 32,
                },
            )

    result = run(scenario())
    assert result.is_error is False
    assert controller.last_claim == ("controller-a", 300, "run_" + "a" * 32)


def test_action_returns_json_summary_plus_screenshot_image() -> None:
    async def scenario():
        async with Client(build_desktop_server(FakeController())) as client:
            return await client.call_tool(
                "colosseum_desktop_click",
                {
                    "controller_id": "controller-a",
                    "x": 10,
                    "y": 20,
                    "expect_text": "Frieren",
                },
            )

    result = run(scenario())
    assert result.is_error is False
    assert [block.type for block in result.content] == ["text", "image"]
    assert '"verificationState": "PENDING_VISUAL_REVIEW"' in result.content[0].text
    assert result.content[1].mime_type == "image/png"


def test_control_error_is_structured_tool_error() -> None:
    class Broken(FakeController):
        def claim(self, controller_id, *, ttl_seconds=120, run_id=None):
            raise DesktopControlError(
                "RESOURCE_BUSY",
                "desktop already leased",
                retryable=True,
                details={"controller": "other"},
            )

    async def scenario():
        async with Client(build_desktop_server(Broken())) as client:
            return await client.call_tool(
                "colosseum_desktop_claim",
                {"controller_id": "controller-a"},
            )

    result = run(scenario())
    assert result.is_error is True
    text = " ".join(
        block.text for block in result.content if getattr(block, "type", None) == "text"
    )
    assert "RESOURCE_BUSY" in text
    assert "desktop already leased" in text
