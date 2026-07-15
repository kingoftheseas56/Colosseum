#!/usr/bin/env python3
"""Record once, replay identically — the input leg of the bakeoff (spec §6).

Physical wheel/touchpad scrolling is captured once via a low-level mouse hook
into a timestamped script; that same script is then replayed (SendInput) into
each reader so every reader sees byte-identical input. Hemanth's blind feel test
stays physical and is NOT replaced by replay (spec §6, §8).

Subcommands:
  record  <script.json> [--seconds N]   capture real wheel input to a script
  replay  <script.json> [--speed 1.0]   inject the script via SendInput
  synth   <script.json> --motion M      write a deterministic synthetic script
                                         (slow_wheel|sustained_wheel|fast_swipe|
                                          boundary) — for pipeline tests w/o hands

A script is: {"motion": str, "events": [{"t_ms": int, "wheel": int}, ...]}
`wheel` is signed WHEEL_DELTA units (120 = one notch; touchpad emits finer).
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)

WH_MOUSE_LL = 14
WM_MOUSEWHEEL = 0x020A
WM_MOUSEHWHEEL = 0x020E
INPUT_MOUSE = 0
MOUSEEVENTF_WHEEL = 0x0800
MOUSEEVENTF_HWHEEL = 0x1000


class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("pt", wt.POINT), ("mouseData", wt.DWORD), ("flags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.POINTER(wt.ULONG))]


LowLevelMouseProc = ctypes.CFUNCTYPE(
    ctypes.c_long, ctypes.c_int, wt.WPARAM, ctypes.POINTER(MSLLHOOKSTRUCT))


def record(path, seconds):
    events = []
    start = [None]

    def proc(nCode, wParam, lParam):
        if nCode == 0 and wParam in (WM_MOUSEWHEEL, WM_MOUSEHWHEEL):
            info = lParam[0]
            delta = ctypes.c_short((info.mouseData >> 16) & 0xFFFF).value
            now = time.perf_counter()
            if start[0] is None:
                start[0] = now
            events.append({"t_ms": int((now - start[0]) * 1000), "wheel": delta,
                           "axis": "h" if wParam == WM_MOUSEHWHEEL else "v"})
        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    callback = LowLevelMouseProc(proc)
    hook = user32.SetWindowsHookExW(WH_MOUSE_LL, callback, None, 0)
    if not hook:
        print("record FAIL: could not install mouse hook", file=sys.stderr)
        return 1
    print("RECORDING %ds — scroll now (wheel + touchpad)..." % seconds, flush=True)
    msg = wt.MSG()
    deadline = time.perf_counter() + seconds
    while time.perf_counter() < deadline:
        if user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
        time.sleep(0.001)
    user32.UnhookWindowsHookEx(hook)
    _write(path, {"motion": "recorded", "events": events})
    print("RECORD_OK %d events -> %s" % (len(events), path))
    return 0


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.POINTER(wt.ULONG))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def _send_wheel(delta, horizontal):
    mi = MOUSEINPUT(0, 0, ctypes.c_uint(delta & 0xFFFFFFFF).value,
                    MOUSEEVENTF_HWHEEL if horizontal else MOUSEEVENTF_WHEEL, 0, None)
    inp = INPUT(INPUT_MOUSE)
    inp.mi = mi
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def replay(path, speed):
    script = _read(path)
    events = script["events"]
    if not events:
        print("replay: empty script", file=sys.stderr)
        return 1
    print("REPLAY %d events (motion=%s speed=%.2f)" % (len(events), script.get("motion"), speed))
    t0 = time.perf_counter()
    for ev in events:
        target = t0 + (ev["t_ms"] / 1000.0) / speed
        while time.perf_counter() < target:
            time.sleep(0.0005)
        _send_wheel(int(ev["wheel"]), ev.get("axis") == "h")
    print("REPLAY_OK")
    return 0


def synth(path, motion):
    """Deterministic scripts so the capture pipeline can be proven without hands."""
    events = []
    if motion == "slow_wheel":            # one notch every ~600ms for 20s
        events = [{"t_ms": i * 600, "wheel": -120, "axis": "v"} for i in range(33)]
    elif motion == "sustained_wheel":     # rapid overlapping notches for 20s
        events = [{"t_ms": i * 80, "wheel": -120, "axis": "v"} for i in range(250)]
    elif motion == "fast_swipe":          # fine pixel-ish deltas w/ momentum, repeated
        for rep in range(10):
            base = rep * 2000
            for i in range(40):
                events.append({"t_ms": base + i * 16, "wheel": -(40 + i % 30), "axis": "v"})
    elif motion == "boundary":            # slow then fast crossings
        t = 0
        for i in range(12):
            step = -120 if i % 2 == 0 else -600
            events.append({"t_ms": t, "wheel": step, "axis": "v"})
            t += 900 if i % 2 == 0 else 300
    else:
        print("unknown motion: " + motion, file=sys.stderr)
        return 1
    _write(path, {"motion": motion, "events": events})
    print("SYNTH_OK %s %d events -> %s" % (motion, len(events), path))
    return 0


def _write(path, obj):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(obj, handle, indent=2)


def _read(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record"); r.add_argument("path"); r.add_argument("--seconds", type=int, default=22)
    p = sub.add_parser("replay"); p.add_argument("path"); p.add_argument("--speed", type=float, default=1.0)
    s = sub.add_parser("synth"); s.add_argument("path"); s.add_argument("--motion", required=True)
    args = parser.parse_args()
    if args.cmd == "record":
        return record(args.path, args.seconds)
    if args.cmd == "replay":
        return replay(args.path, args.speed)
    return synth(args.path, args.motion)


if __name__ == "__main__":
    raise SystemExit(main())
