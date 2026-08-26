import ctypes
import json
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
GWL_EXSTYLE = -20
WS_EX_TOPMOST = 0x00000008
rows = []

time.sleep(8)

def callback(hwnd, _):
    if not user32.IsWindowVisible(hwnd):
        return True
    length = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buf, length + 1)
    title = buf.value
    if not title:
        return True
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    exstyle = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    rows.append({"hwnd": hex(hwnd), "pid": pid.value, "title": title,
                 "rect": [rect.left, rect.top, rect.right, rect.bottom],
                 "topmost": bool(exstyle & WS_EX_TOPMOST)})
    return True
PROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(PROC(callback), 0)
foreground = user32.GetForegroundWindow()
payload = {"foreground": hex(foreground), "windows": rows}
with open(r"C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\artifacts\reddit-captures\_window-inspect.json", "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
