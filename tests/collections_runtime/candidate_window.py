"""Operate only the Windows window belonging to the verified isolated app PID."""
import argparse
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import time

from drive_app import Candidate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--pipe", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--action", choices=("close", "resize"), required=True)
    parser.add_argument("--width", type=int, default=900, help="Logical pixels")
    parser.add_argument("--height", type=int, default=650, help="Logical pixels")
    args = parser.parse_args()
    client = Candidate(args.cli, args.pipe, args.evidence)
    pid = client.call("ping")["pid"]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    assert user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)), "Per-monitor V2 coordinate context required"
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x100000 | 0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        image, capacity = ctypes.create_unicode_buffer(32768), w.DWORD(32768)
        assert kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity))
        assert Path(image.value).resolve() == (args.cli.parent / "colosseum.exe").resolve()
        user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
        user.IsWindowVisible.argtypes = [w.HWND]
        windows = []
        callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        @callback_type
        def collect(hwnd, _):
            found = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(found))
            if found.value == pid and user.IsWindowVisible(hwnd):
                windows.append(hwnd)
            return True
        user.EnumWindows.argtypes = [callback_type, w.LPARAM]
        assert user.EnumWindows(collect, 0)
        assert len(windows) == 1, windows
        hwnd = windows[0]
        user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
        user.GetDpiForWindow.argtypes = [w.HWND]
        rect = w.RECT()
        assert user.GetWindowRect(hwnd, ctypes.byref(rect))
        dpi = user.GetDpiForWindow(hwnd)
        receipt = {"pid": pid, "exe": image.value, "hwnd": hwnd, "action": args.action,
                   "dpi": dpi, "beforePhysicalRect": [rect.left, rect.top, rect.right, rect.bottom]}
        if args.action == "close":
            user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
            assert user.PostMessageW(hwnd, 0x10, 0, 0)
            assert kernel.WaitForSingleObject(handle, 30000) == 0, "Graceful close did not exit in 30 seconds"
            code = w.DWORD()
            assert kernel.GetExitCodeProcess(handle, ctypes.byref(code))
            receipt["exitCode"] = code.value
            assert code.value == 0, receipt
        else:
            assert 480 <= args.width <= 1600 and 480 <= args.height <= 1000
            client.call("window-set-state", {"state": "normal"})
            user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT]
            assert user.SetWindowPos(hwnd, None, 0, 0, round(args.width * dpi / 96), round(args.height * dpi / 96), 0x4 | 0x10)
            time.sleep(.4)
            receipt["requestedLogicalSize"] = [args.width, args.height]
            receipt["observedState"] = client.call("get-state")
        path = args.evidence / (args.action + "-receipt.json")
        path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(json.dumps(receipt))
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    main()
