from __future__ import annotations

import ctypes
import os
import time
from ctypes import wintypes
from dataclasses import dataclass
from typing import Callable

from .desktop_lease import DesktopLeaseError


WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


@dataclass(frozen=True, slots=True)
class WindowInfo:
    hwnd: int
    title: str
    left: int
    top: int
    right: int
    bottom: int

    @property
    def width(self) -> int:
        return self.right - self.left

    @property
    def height(self) -> int:
        return self.bottom - self.top

    def contains_relative(self, x: int, y: int) -> bool:
        return 0 <= x < self.width and 0 <= y < self.height

    def to_screen(self, x: int, y: int) -> tuple[int, int]:
        if not self.contains_relative(x, y):
            raise DesktopLeaseError(
                "OUTSIDE_PINNED_WINDOW",
                "requested coordinate is outside the pinned Colosseum window",
                details={
                    "relative": [x, y],
                    "windowSize": [self.width, self.height],
                },
            )
        return self.left + x, self.top + y


class Win32WindowApi:
    def __init__(self, *, title: str | None = None) -> None:
        if os.name != "nt":
            raise DesktopLeaseError(
                "WINDOWS_REQUIRED",
                "Colosseum desktop control currently requires Windows",
            )
        self.title = (title or os.environ.get("COLOSSEUM_WINDOW_TITLE") or "Colosseum").strip()
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._configure_signatures()

    def _configure_signatures(self) -> None:
        self.user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
        self.user32.EnumWindows.restype = wintypes.BOOL
        self.user32.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user32.IsWindowVisible.restype = wintypes.BOOL
        self.user32.IsWindow.argtypes = [wintypes.HWND]
        self.user32.IsWindow.restype = wintypes.BOOL
        self.user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
        self.user32.GetWindowTextLengthW.restype = ctypes.c_int
        self.user32.GetWindowTextW.argtypes = [
            wintypes.HWND,
            wintypes.LPWSTR,
            ctypes.c_int,
        ]
        self.user32.GetWindowTextW.restype = ctypes.c_int
        self.user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        self.user32.GetWindowRect.restype = wintypes.BOOL
        self.user32.GetForegroundWindow.argtypes = []
        self.user32.GetForegroundWindow.restype = wintypes.HWND
        self.user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        self.user32.ShowWindow.restype = wintypes.BOOL
        self.user32.SetForegroundWindow.argtypes = [wintypes.HWND]
        self.user32.SetForegroundWindow.restype = wintypes.BOOL
        self.user32.GetWindowThreadProcessId.argtypes = [
            wintypes.HWND,
            ctypes.POINTER(wintypes.DWORD),
        ]
        self.user32.GetWindowThreadProcessId.restype = wintypes.DWORD
        self.user32.AttachThreadInput.argtypes = [
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.BOOL,
        ]
        self.user32.AttachThreadInput.restype = wintypes.BOOL
        self.user32.BringWindowToTop.argtypes = [wintypes.HWND]
        self.user32.BringWindowToTop.restype = wintypes.BOOL
        self.user32.SetActiveWindow.argtypes = [wintypes.HWND]
        self.user32.SetActiveWindow.restype = wintypes.HWND
        self.user32.SetFocus.argtypes = [wintypes.HWND]
        self.user32.SetFocus.restype = wintypes.HWND
        self.kernel32.GetCurrentThreadId.argtypes = []
        self.kernel32.GetCurrentThreadId.restype = wintypes.DWORD

    def _window_title(self, hwnd: int) -> str:
        length = self.user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return ""
        buffer = ctypes.create_unicode_buffer(length + 1)
        self.user32.GetWindowTextW(hwnd, buffer, length + 1)
        return buffer.value

    def _rect(self, hwnd: int) -> wintypes.RECT:
        rect = wintypes.RECT()
        if not self.user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            raise DesktopLeaseError(
                "WINDOW_UNAVAILABLE",
                "could not read the pinned Colosseum window bounds",
                retryable=True,
                details={"hwnd": int(hwnd)},
            )
        return rect

    def list_matching(self) -> list[WindowInfo]:
        matches: list[WindowInfo] = []
        wanted = self.title.casefold()

        @WNDENUMPROC
        def callback(hwnd, _lparam):
            if not self.user32.IsWindowVisible(hwnd):
                return True
            title = self._window_title(hwnd)
            if title.casefold() != wanted:
                return True
            rect = self._rect(hwnd)
            if rect.right <= rect.left or rect.bottom <= rect.top:
                return True
            matches.append(
                WindowInfo(
                    hwnd=int(hwnd),
                    title=title,
                    left=int(rect.left),
                    top=int(rect.top),
                    right=int(rect.right),
                    bottom=int(rect.bottom),
                )
            )
            return True

        if not self.user32.EnumWindows(callback, 0):
            raise DesktopLeaseError(
                "WINDOW_ENUMERATION_FAILED",
                "Windows could not enumerate top-level windows",
                retryable=True,
            )
        return matches

    def resolve_unique(self) -> WindowInfo:
        matches = self.list_matching()
        if not matches:
            raise DesktopLeaseError(
                "COLOSSEUM_WINDOW_NOT_FOUND",
                f'no visible top-level window named "{self.title}" was found',
                retryable=True,
            )
        if len(matches) != 1:
            raise DesktopLeaseError(
                "COLOSSEUM_WINDOW_AMBIGUOUS",
                f'multiple visible top-level windows named "{self.title}" were found',
                details={"windows": [item.hwnd for item in matches]},
            )
        return matches[0]

    def get(self, hwnd: int) -> WindowInfo:
        if not self.user32.IsWindow(hwnd):
            raise DesktopLeaseError(
                "PINNED_WINDOW_GONE",
                "the pinned Colosseum HWND no longer exists",
                retryable=True,
                details={"hwnd": int(hwnd)},
            )
        title = self._window_title(hwnd)
        if title.casefold() != self.title.casefold():
            raise DesktopLeaseError(
                "PINNED_WINDOW_CHANGED",
                "the pinned HWND no longer belongs to the expected Colosseum window",
                retryable=True,
                details={"hwnd": int(hwnd), "title": title},
            )
        rect = self._rect(hwnd)
        return WindowInfo(
            hwnd=int(hwnd),
            title=title,
            left=int(rect.left),
            top=int(rect.top),
            right=int(rect.right),
            bottom=int(rect.bottom),
        )

    def process_id(self, hwnd: int) -> int:
        if not self.user32.IsWindow(hwnd):
            raise DesktopLeaseError(
                "PINNED_WINDOW_GONE",
                "the Colosseum HWND no longer exists",
                retryable=True,
                details={"hwnd": int(hwnd)},
            )
        pid = wintypes.DWORD()
        thread_id = self.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if not thread_id or not pid.value:
            raise DesktopLeaseError(
                "WINDOW_PROCESS_UNAVAILABLE",
                "could not resolve the process that owns the Colosseum window",
                retryable=True,
                details={"hwnd": int(hwnd)},
            )
        return int(pid.value)

    def foreground_hwnd(self) -> int:
        return int(self.user32.GetForegroundWindow() or 0)

    def ensure_foreground(self, hwnd: int) -> bool:
        self.get(hwnd)
        hwnd = int(hwnd)
        if self.foreground_hwnd() == hwnd:
            return True

        foreground = self.foreground_hwnd()
        current_thread = int(self.kernel32.GetCurrentThreadId())
        foreground_thread = (
            int(self.user32.GetWindowThreadProcessId(foreground, None))
            if foreground
            else 0
        )
        target_thread = int(self.user32.GetWindowThreadProcessId(hwnd, None))
        attached: list[int] = []

        self.user32.ShowWindow(hwnd, 9)  # SW_RESTORE
        try:
            for thread_id in {foreground_thread, target_thread}:
                if thread_id and thread_id != current_thread:
                    if self.user32.AttachThreadInput(current_thread, thread_id, True):
                        attached.append(thread_id)

            self.user32.BringWindowToTop(hwnd)
            self.user32.SetForegroundWindow(hwnd)
            self.user32.SetActiveWindow(hwnd)
            self.user32.SetFocus(hwnd)
            time.sleep(0.08)
            return self.foreground_hwnd() == hwnd
        finally:
            for thread_id in reversed(attached):
                self.user32.AttachThreadInput(current_thread, thread_id, False)
