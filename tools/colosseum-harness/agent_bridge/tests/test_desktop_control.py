from __future__ import annotations

import asyncio
import json
import tempfile
from contextlib import asynccontextmanager
from io import BytesIO
from pathlib import Path
from types import SimpleNamespace

import pytest
from PIL import Image

from colosseum_agent_bridge.cursortouch import (
    CursorTouchSession,
    ScreenshotEvidence,
    scoped_colosseum_tree,
)
from colosseum_agent_bridge.desktop_control import (
    ColosseumDesktopController,
    DesktopControlError,
    _crop_to_window,
)
from colosseum_agent_bridge.desktop_window import WindowInfo


def run(coro):
    return asyncio.run(coro)


class FakeWindows:
    def __init__(self) -> None:
        self.window = WindowInfo(101, "Colosseum", 100, 200, 900, 800)
        self.foreground = 101

    def list_matching(self):
        return [self.window]

    def resolve_unique(self):
        return self.window

    def get(self, hwnd):
        assert hwnd == 101
        return self.window

    def foreground_hwnd(self):
        return self.foreground

    def ensure_foreground(self, hwnd):
        self.foreground = hwnd
        return True


class FakeSession:
    def __init__(self, owner) -> None:
        self.owner = owner

    async def screenshot(self):
        self.owner.capture_count += 1
        payload = f"png-{self.owner.capture_count}".encode()
        return ScreenshotEvidence(payload, "image/png")

    async def snapshot(self):
        return (
            'desktop\n'
            '├── window "Colosseum"\n'
            '│   ├── (200,100) button "Theatre"  [action: click]\n'
            '│   └── (300,100) button "Search"  [action: click]\n'
            '├── window "WhatsApp"\n'
            '│   └── (50,50) edit "Search or start a new chat"\n'
        )

    async def click(self, x, y):
        self.owner.calls.append(("click", x, y))

    async def type_text(self, x, y, text, *, clear):
        self.owner.calls.append(("type", x, y, text, clear))

    async def shortcut(self, shortcut):
        self.owner.calls.append(("shortcut", shortcut))


class FakeCursorTouch:
    def __init__(self) -> None:
        self.calls = []
        self.capture_count = 0

    @asynccontextmanager
    async def open(self):
        yield FakeSession(self)


def controller(tmp_path):
    return ColosseumDesktopController(
        runtime_root=tmp_path,
        windows=FakeWindows(),
        cursortouch=FakeCursorTouch(),
        image_cropper=lambda screenshot, _window: screenshot,
    )


def test_scoped_tree_excludes_background_windows() -> None:
    snapshot = (
        'desktop\n'
        '├── window "Colosseum"\n'
        '│   ├── button "Theatre"\n'
        '│   └── edit "Search"\n'
        '├── window "WhatsApp"\n'
        '│   └── edit "Search or start a new chat"\n'
    )
    scoped = scoped_colosseum_tree(snapshot)
    assert 'window "Colosseum"' in scoped
    assert 'button "Theatre"' in scoped
    assert "WhatsApp" not in scoped


def test_claim_conflict_and_release(tmp_path: Path) -> None:
    first = controller(tmp_path)
    second = controller(tmp_path)
    claimed = first.claim("controller-a")
    assert claimed["hwnd"] == 101
    with pytest.raises(DesktopControlError) as raised:
        second.claim("controller-b")
    assert raised.value.code == "RESOURCE_BUSY"
    assert first.release("controller-a")["released"] is True


def test_click_is_window_relative_and_blocks_until_visual_confirmation(
    tmp_path: Path,
) -> None:
    control = controller(tmp_path)
    control.claim("controller-a")

    summary, screenshot = run(
        control.click("controller-a", x=20, y=30, expect_text="Theatre")
    )
    assert screenshot.data
    assert control.cursortouch.calls == [("click", 120, 230)]
    assert summary["screenPoint"] == [120, 230]
    assert summary["verificationState"] == "PENDING_VISUAL_REVIEW"
    assert Path(summary["receiptPath"]).is_file()

    with pytest.raises(DesktopControlError) as raised:
        run(control.shortcut("controller-a", shortcut="tab"))
    assert raised.value.code == "VISUAL_CONFIRMATION_REQUIRED"

    with pytest.raises(DesktopControlError) as raised:
        control.release("controller-a")
    assert raised.value.code == "VISUAL_CONFIRMATION_REQUIRED"

    confirmed = control.confirm(
        "controller-a",
        action_id=summary["actionId"],
        verdict="pass",
        note="Screenshot shows the intended state.",
    )
    assert confirmed["verificationState"] == "VISUALLY_CONFIRMED"

    run(control.shortcut("controller-a", shortcut="tab"))
    pending = control.lease.status()
    assert pending["pendingActionId"]
    control.confirm(
        "controller-a",
        action_id=pending["pendingActionId"],
        verdict="uncertain",
    )
    assert control.release("controller-a")["released"] is True


def test_outside_window_coordinate_fails_before_input(tmp_path: Path) -> None:
    control = controller(tmp_path)
    control.claim("controller-a")
    with pytest.raises(DesktopControlError) as raised:
        run(control.click("controller-a", x=801, y=10))
    assert raised.value.code == "OUTSIDE_PINNED_WINDOW"
    assert control.cursortouch.calls == []
    assert control.release("controller-a")["released"] is True


class FakeMcpClient:
    def __init__(self) -> None:
        self.calls = []

    async def call_tool(self, name, arguments):
        self.calls.append((name, arguments))
        return SimpleNamespace(is_error=False, content=[])


def test_empty_clear_avoids_cursortouch_type_bug() -> None:
    client = FakeMcpClient()
    session = CursorTouchSession(client)
    run(session.type_text(10, 20, "", clear=True))
    assert [name for name, _ in client.calls] == [
        "Click",
        "Shortcut",
        "Shortcut",
    ]
    assert client.calls[1][1]["shortcut"] == "ctrl+a"
    assert client.calls[2][1]["shortcut"] == "backspace"


def test_screenshot_is_cropped_to_pinned_window() -> None:
    source = Image.new("RGB", (1000, 1000), "white")
    raw = BytesIO()
    source.save(raw, format="PNG")
    evidence = ScreenshotEvidence(raw.getvalue(), "image/png")
    window = WindowInfo(101, "Colosseum", 100, 200, 900, 800)

    cropped = _crop_to_window(evidence, window)

    with Image.open(BytesIO(cropped.data)) as image:
        assert image.size == (800, 600)
    assert cropped.mime_type == "image/png"


def test_screenshot_crop_fails_closed_when_window_is_outside_capture() -> None:
    source = Image.new("RGB", (640, 480), "white")
    raw = BytesIO()
    source.save(raw, format="PNG")
    evidence = ScreenshotEvidence(raw.getvalue(), "image/png")
    window = WindowInfo(101, "Colosseum", 0, 0, 800, 600)

    with pytest.raises(DesktopControlError) as raised:
        _crop_to_window(evidence, window)

    assert raised.value.code == "SCREENSHOT_SCOPE_UNAVAILABLE"
