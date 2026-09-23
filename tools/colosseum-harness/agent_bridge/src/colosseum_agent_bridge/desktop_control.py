from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
import uuid
from io import BytesIO
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Awaitable, Callable

from PIL import Image

from .cursortouch import CursorTouchClient, CursorTouchError, ScreenshotEvidence, scoped_colosseum_tree
from .desktop_lease import DesktopLeaseError, DesktopLeaseManager
from .desktop_window import Win32WindowApi, WindowInfo


class DesktopControlError(RuntimeError):
    def __init__(
        self,
        code: str,
        message: str,
        *,
        retryable: bool = False,
        details: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.retryable = retryable
        self.details = details or {}


def default_runtime_root() -> Path:
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("TEMP") or tempfile.gettempdir()
    return Path(base) / "PreflightAgentRuntime" / "tools"


def default_repo_root() -> Path:
    configured = os.environ.get("COLOSSEUM_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    for candidate in Path(__file__).resolve().parents:
        if (candidate / "native" / "CMakeLists.txt").is_file() and (candidate / "qml").is_dir():
            return candidate
    raise DesktopControlError(
        "COLOSSEUM_ROOT_NOT_FOUND",
        "could not resolve the Colosseum repository root for run-bound desktop control",
    )


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _bounded_text(value: str | None, *, max_length: int = 2000) -> str | None:
    if value is None:
        return None
    return value[:max_length]


def _crop_to_window(
    screenshot: ScreenshotEvidence,
    window: WindowInfo,
) -> ScreenshotEvidence:
    try:
        with Image.open(BytesIO(screenshot.data)) as image:
            width, height = image.size
            if (
                window.left < 0
                or window.top < 0
                or window.right > width
                or window.bottom > height
                or window.right <= window.left
                or window.bottom <= window.top
            ):
                raise DesktopControlError(
                    "SCREENSHOT_SCOPE_UNAVAILABLE",
                    "the pinned Colosseum window is outside the captured screenshot bounds",
                    retryable=True,
                    details={
                        "screenshotSize": [width, height],
                        "windowRect": [
                            window.left,
                            window.top,
                            window.right,
                            window.bottom,
                        ],
                    },
                )
            cropped = image.crop(
                (window.left, window.top, window.right, window.bottom)
            ).convert("RGB")
            output = BytesIO()
            cropped.save(output, format="PNG")
    except DesktopControlError:
        raise
    except BaseException as exc:
        raise DesktopControlError(
            "SCREENSHOT_SCOPE_UNAVAILABLE",
            "CursorTouch screenshot could not be cropped to the pinned Colosseum window",
            retryable=True,
        ) from exc
    return ScreenshotEvidence(output.getvalue(), "image/png")


class ColosseumDesktopController:
    def __init__(
        self,
        *,
        runtime_root: Path | None = None,
        repo_root: Path | None = None,
        windows: Win32WindowApi | None = None,
        cursortouch: CursorTouchClient | None = None,
        image_cropper: Callable[[ScreenshotEvidence, WindowInfo], ScreenshotEvidence] | None = None,
    ) -> None:
        self.runtime_root = Path(runtime_root or default_runtime_root())
        self.repo_root = Path(repo_root).resolve() if repo_root is not None else None
        self.windows = windows or Win32WindowApi()
        self.cursortouch = cursortouch or CursorTouchClient()
        self.image_cropper = image_cropper or _crop_to_window
        self.lease = DesktopLeaseManager(self.runtime_root)
        self.control_root = self.runtime_root / "colosseum-cursortouch"
        self.evidence_root = self.control_root / "evidence"
        self.actions_root = self.control_root / "actions"
        self.evidence_root.mkdir(parents=True, exist_ok=True)
        self.actions_root.mkdir(parents=True, exist_ok=True)

    def _translate_error(self, exc: BaseException) -> DesktopControlError:
        if isinstance(exc, DesktopControlError):
            return exc
        if isinstance(exc, (DesktopLeaseError, CursorTouchError)):
            return DesktopControlError(
                exc.code,
                str(exc),
                retryable=getattr(exc, "retryable", False),
                details=getattr(exc, "details", {}),
            )
        return DesktopControlError(
            "DESKTOP_CONTROL_FAILED",
            str(exc),
            retryable=True,
        )

    def _safe_lease_view(self, record: dict[str, Any] | None) -> dict[str, Any] | None:
        if record is None:
            return None
        return {
            key: value
            for key, value in record.items()
            if key not in {"nonce"}
        }

    def status(self) -> dict[str, Any]:
        try:
            matches = self.windows.list_matching()
            lease = self.lease.status()
        except BaseException as exc:
            raise self._translate_error(exc) from exc
        return {
            "lease": self._safe_lease_view(lease),
            "windows": [
                {
                    "hwnd": item.hwnd,
                    "title": item.title,
                    "rect": [item.left, item.top, item.right, item.bottom],
                    "size": [item.width, item.height],
                    "foreground": self.windows.foreground_hwnd() == item.hwnd,
                }
                for item in matches
            ],
        }

    def _run_pid(self, run_id: str) -> int:
        repo_root = self.repo_root or default_repo_root()
        repo_root = repo_root.resolve()
        self.repo_root = repo_root
        if not re.fullmatch(r"run_[0-9a-f]{32}", run_id):
            raise DesktopControlError("RUN_ID_INVALID", f"invalid run id: {run_id}")
        receipt_path = repo_root / "artifacts" / "harness-runs" / run_id / "run.json"
        try:
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise DesktopControlError(
                "RUN_RECEIPT_INVALID",
                f"cannot read run receipt: {receipt_path}",
            ) from exc
        if (
            not isinstance(receipt, dict)
            or receipt.get("schema") != "colosseum.harness.run.v1"
            or receipt.get("runId") != run_id
            or Path(str(receipt.get("repo", {}).get("root", ""))).resolve() != repo_root
        ):
            raise DesktopControlError(
                "RUN_RECEIPT_INVALID",
                f"run receipt identity does not match this Colosseum checkout: {receipt_path}",
            )
        runtime = receipt.get("runtime")
        pid = runtime.get("pid") if isinstance(runtime, dict) else None
        if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 0:
            raise DesktopControlError(
                "RUN_RUNTIME_NOT_BOUND",
                f"run has no bound Lanista runtime PID: {run_id}",
            )
        return pid

    def _resolve_window_for_pid(self, expected_pid: int) -> WindowInfo:
        matches = self.windows.list_matching()
        if not matches:
            raise DesktopControlError(
                "COLOSSEUM_WINDOW_NOT_FOUND",
                "no visible top-level Colosseum window was found",
                retryable=True,
            )
        observed: list[dict[str, int]] = []
        owned: list[WindowInfo] = []
        for window in matches:
            pid = self.windows.process_id(window.hwnd)
            observed.append({"hwnd": window.hwnd, "pid": pid})
            if pid == expected_pid:
                owned.append(window)
        if not owned:
            raise DesktopControlError(
                "COLOSSEUM_WINDOW_PID_MISMATCH",
                "visible Colosseum window does not belong to the run's recorded PID",
                retryable=True,
                details={"expectedPid": expected_pid, "windows": observed},
            )
        if len(owned) != 1:
            raise DesktopControlError(
                "COLOSSEUM_WINDOW_AMBIGUOUS",
                "multiple visible Colosseum windows belong to the run's recorded PID",
                details={"expectedPid": expected_pid, "windows": [item.hwnd for item in owned]},
            )
        return owned[0]

    def claim(
        self,
        controller_id: str,
        *,
        ttl_seconds: int = 300,
        run_id: str | None = None,
    ) -> dict[str, Any]:
        try:
            expected_pid = self._run_pid(run_id) if run_id is not None else None
            window = (
                self._resolve_window_for_pid(expected_pid)
                if expected_pid is not None
                else self.windows.resolve_unique()
            )
            record = self.lease.acquire(
                controller_id,
                ttl_seconds=ttl_seconds,
                hwnd=window.hwnd,
                title=window.title,
                run_id=run_id,
                pid=expected_pid,
            )
        except BaseException as exc:
            raise self._translate_error(exc) from exc
        result = {
            "claimed": True,
            "controllerId": controller_id,
            "hwnd": window.hwnd,
            "title": window.title,
            "rect": [window.left, window.top, window.right, window.bottom],
            "size": [window.width, window.height],
            "expiresAt": record["expiresAt"],
            "sharedLease": "PreflightAgentRuntime/tools/locks/desktop-raw-input.lease",
        }
        if run_id is not None:
            result["runId"] = run_id
            result["pid"] = expected_pid
        return result

    def _owned_window(self, controller_id: str) -> tuple[dict[str, Any], WindowInfo]:
        record = self.lease.assert_owner(controller_id)
        hwnd = record.get("hwnd")
        if not isinstance(hwnd, int):
            raise DesktopControlError(
                "LEASE_STATE_UNCERTAIN",
                "desktop lease does not contain a pinned HWND",
            )
        window = self.windows.get(hwnd)
        return record, window

    def _ensure_ready(
        self,
        controller_id: str,
        *,
        require_no_pending: bool,
    ) -> tuple[dict[str, Any], WindowInfo]:
        record, window = self._owned_window(controller_id)
        if require_no_pending and record.get("pendingActionId"):
            raise DesktopControlError(
                "VISUAL_CONFIRMATION_REQUIRED",
                "confirm the previous desktop action before issuing another action",
                details={"actionId": record["pendingActionId"]},
            )
        if not self.windows.ensure_foreground(window.hwnd):
            raise DesktopControlError(
                "FOREGROUND_NOT_CONFIRMED",
                "could not prove the pinned Colosseum window is foreground",
                retryable=True,
                details={
                    "hwnd": window.hwnd,
                    "foregroundHwnd": self.windows.foreground_hwnd(),
                },
            )
        return record, self.windows.get(window.hwnd)

    async def _capture(
        self,
        session: Any,
        action_id: str,
        label: str,
        window: WindowInfo,
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        screenshot = await session.screenshot()
        screenshot = self.image_cropper(screenshot, window)
        path = self.evidence_root / f"{action_id}-{label}.png"
        path.write_bytes(screenshot.data)
        sha = hashlib.sha256(screenshot.data).hexdigest().upper()

        snapshot_text = ""
        scoped_tree = ""
        snapshot_error = None
        try:
            snapshot_text = await session.snapshot()
            scoped_tree = scoped_colosseum_tree(snapshot_text)
        except CursorTouchError as exc:
            snapshot_error = {
                "code": exc.code,
                "message": _bounded_text(str(exc)),
            }

        state = {
            "capturedAt": _utc_now(),
            "screenshotPath": str(path),
            "screenshotSha256": sha,
            "screenshotBytes": len(screenshot.data),
            "window": {
                "hwnd": window.hwnd,
                "title": window.title,
                "rect": [window.left, window.top, window.right, window.bottom],
                "foreground": self.windows.foreground_hwnd() == window.hwnd,
            },
            "scopedUiTree": scoped_tree[:32000],
            "snapshotError": snapshot_error,
        }
        return state, screenshot

    async def observe(
        self,
        controller_id: str,
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        try:
            with self.lease.operation_lock(controller_id):
                _, window = self._ensure_ready(
                    controller_id,
                    require_no_pending=False,
                )
                action_id = f"observe-{uuid.uuid4().hex}"
                async with self.cursortouch.open() as session:
                    state, screenshot = await self._capture(
                        session,
                        action_id,
                        "current",
                        window,
                    )
                return {
                    "controllerId": controller_id,
                    "verificationState": "OBSERVATION",
                    "state": state,
                }, screenshot
        except BaseException as exc:
            raise self._translate_error(exc) from exc

    async def _perform_action(
        self,
        controller_id: str,
        *,
        kind: str,
        relative: tuple[int, int] | None,
        expect_text: str | None,
        execute: Callable[[Any, tuple[int, int] | None], Awaitable[None]],
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        action_id = uuid.uuid4().hex
        try:
            with self.lease.operation_lock(controller_id):
                _, window = self._ensure_ready(
                    controller_id,
                    require_no_pending=True,
                )
                screen_point = (
                    window.to_screen(*relative)
                    if relative is not None
                    else None
                )
                async with self.cursortouch.open() as session:
                    before, _before_image = await self._capture(
                        session,
                        action_id,
                        "before",
                        window,
                    )
                    tool_error: dict[str, Any] | None = None
                    try:
                        await execute(session, screen_point)
                    except CursorTouchError as exc:
                        tool_error = {
                            "code": exc.code,
                            "message": _bounded_text(str(exc)),
                        }
                    window = self.windows.get(window.hwnd)
                    after, after_image = await self._capture(
                        session,
                        action_id,
                        "after",
                        window,
                    )

                expected_present = (
                    None
                    if expect_text is None
                    else expect_text.casefold()
                    in str(after.get("scopedUiTree", "")).casefold()
                )
                receipt = {
                    "actionId": action_id,
                    "controllerId": controller_id,
                    "kind": kind,
                    "createdAt": _utc_now(),
                    "hwnd": window.hwnd,
                    "relative": list(relative) if relative is not None else None,
                    "screenPoint": list(screen_point) if screen_point is not None else None,
                    "expectText": expect_text,
                    "uiaExpectedTextPresent": expected_present,
                    "pixelBytesChanged": (
                        before["screenshotSha256"] != after["screenshotSha256"]
                    ),
                    "uiaTreeChanged": (
                        before.get("scopedUiTree") != after.get("scopedUiTree")
                    ),
                    "toolError": tool_error,
                    "verificationState": "PENDING_VISUAL_REVIEW",
                    "before": before,
                    "after": after,
                }
                receipt_path = self.actions_root / f"{action_id}.json"
                receipt_path.write_text(
                    json.dumps(receipt, indent=2, ensure_ascii=True) + "\n",
                    encoding="utf-8",
                )
                self.lease.update(
                    controller_id,
                    pendingActionId=action_id,
                )
                receipt["receiptPath"] = str(receipt_path)
                return receipt, after_image
        except BaseException as exc:
            raise self._translate_error(exc) from exc

    async def click(
        self,
        controller_id: str,
        *,
        x: int,
        y: int,
        expect_text: str | None = None,
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        async def execute(session: Any, point: tuple[int, int] | None) -> None:
            assert point is not None
            await session.click(*point)

        return await self._perform_action(
            controller_id,
            kind="click",
            relative=(int(x), int(y)),
            expect_text=_bounded_text(expect_text),
            execute=execute,
        )

    async def type_text(
        self,
        controller_id: str,
        *,
        x: int,
        y: int,
        text: str,
        clear: bool = False,
        expect_text: str | None = None,
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        if len(text) > 16000:
            raise DesktopControlError(
                "INVALID_ARGUMENT",
                "text is too long",
            )

        async def execute(session: Any, point: tuple[int, int] | None) -> None:
            assert point is not None
            await session.type_text(*point, text, clear=clear)

        return await self._perform_action(
            controller_id,
            kind="type",
            relative=(int(x), int(y)),
            expect_text=_bounded_text(expect_text),
            execute=execute,
        )

    async def shortcut(
        self,
        controller_id: str,
        *,
        shortcut: str,
        expect_text: str | None = None,
    ) -> tuple[dict[str, Any], ScreenshotEvidence]:
        if not shortcut.strip() or len(shortcut) > 128:
            raise DesktopControlError(
                "INVALID_ARGUMENT",
                "shortcut must be a non-empty string up to 128 characters",
            )

        async def execute(session: Any, _point: tuple[int, int] | None) -> None:
            await session.shortcut(shortcut)

        return await self._perform_action(
            controller_id,
            kind="shortcut",
            relative=None,
            expect_text=_bounded_text(expect_text),
            execute=execute,
        )

    def confirm(
        self,
        controller_id: str,
        *,
        action_id: str,
        verdict: str,
        note: str | None = None,
    ) -> dict[str, Any]:
        verdict = verdict.strip().lower()
        if verdict not in {"pass", "fail", "uncertain"}:
            raise DesktopControlError(
                "INVALID_ARGUMENT",
                "verdict must be pass, fail, or uncertain",
            )
        try:
            record = self.lease.assert_owner(controller_id)
            pending = record.get("pendingActionId")
            if pending != action_id:
                raise DesktopControlError(
                    "ACTION_MISMATCH",
                    "action_id does not match the pending visual review",
                    details={"pendingActionId": pending},
                )
            receipt_path = self.actions_root / f"{action_id}.json"
            try:
                receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            except (FileNotFoundError, json.JSONDecodeError) as exc:
                raise DesktopControlError(
                    "ACTION_RECEIPT_UNAVAILABLE",
                    "pending action receipt could not be read",
                    details={"path": str(receipt_path)},
                ) from exc
            if not isinstance(receipt, dict):
                raise DesktopControlError(
                    "ACTION_RECEIPT_UNAVAILABLE",
                    "pending action receipt is invalid",
                )
            receipt["verificationState"] = {
                "pass": "VISUALLY_CONFIRMED",
                "fail": "VISUALLY_REJECTED",
                "uncertain": "VISUAL_OUTCOME_UNCERTAIN",
            }[verdict]
            receipt["visualVerdict"] = verdict
            receipt["visualNote"] = _bounded_text(note, max_length=4000)
            receipt["confirmedAt"] = _utc_now()
            receipt_path.write_text(
                json.dumps(receipt, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
            self.lease.update(controller_id, pendingActionId=None)
            return {
                "actionId": action_id,
                "verdict": verdict,
                "verificationState": receipt["verificationState"],
                "receiptPath": str(receipt_path),
                "nextActionAllowed": True,
            }
        except BaseException as exc:
            raise self._translate_error(exc) from exc

    def release(self, controller_id: str) -> dict[str, Any]:
        try:
            released = self.lease.release(controller_id)
            return {
                "released": bool(released),
                "controllerId": controller_id,
            }
        except BaseException as exc:
            raise self._translate_error(exc) from exc
