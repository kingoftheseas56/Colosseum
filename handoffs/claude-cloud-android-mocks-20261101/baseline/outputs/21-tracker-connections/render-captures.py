"""Local, isolated, read-only browser render of this mock batch. No app or provider calls."""
from __future__ import annotations
import pathlib, subprocess, time, shutil, sys
ROOT = pathlib.Path(__file__).resolve().parent
CAPTURES = ROOT / "captures"
CHROME = pathlib.Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")
JOBS = [
    ("connections", "portrait", 412, 900),
    ("connections", "landscape", 900, 412),
    ("connections", "desktop", 1280, 700),
    ("connections", "compact", 354, 792),
    ("connections", "tablet", 800, 1280),
    ("dossier", "portrait", 412, 900),
    ("dossier", "landscape", 900, 412),
    ("dossier", "desktop", 1280, 700),
    ("dossier", "compact", 354, 792),
    ("dossier", "compactland", 792, 354),
    ("preferences", "portrait", 412, 900),
    ("preferences", "landscape", 900, 412),
    ("preferences", "desktop", 1280, 700),
    ("preferences", "compact", 354, 792),
]
def main() -> int:
    failures = 0
    for kind, label, w, h in JOBS:
        out = CAPTURES / f"{kind}-{label}.png"
        source = (CAPTURES / f"_render-{kind}-{label}.html").resolve().as_uri()
        profile = ROOT / ".capture-run-profile"
        args = [str(CHROME), "--headless=new", "--disable-gpu",
                "--no-first-run", "--no-default-browser-check",
                "--disable-background-networking", "--disable-extensions",
                "--hide-scrollbars", "--force-device-scale-factor=1",
                "--noerrdialogs", "--do-not-de-elevate",
                f"--user-data-dir={profile}",
                f"--window-size={w},{h}", "--virtual-time-budget=1500",
                f"--screenshot={out}", source]
        if out.exists():
            out.unlink()
        print(f"RUN {kind}-{label}: {w}x{h}", flush=True)
        try:
            result = subprocess.run(args, capture_output=True, text=True, timeout=20)
        except subprocess.TimeoutExpired:
            print("  TIMEOUT", flush=True)
            failures += 1
            continue
        deadline = time.monotonic() + 8
        while not out.is_file() and time.monotonic() < deadline:
            time.sleep(.25)
        if not out.is_file() or out.stat().st_size < 2000:
            print(f"  MISSING exit={result.returncode} stderr={result.stderr[:400]!r}", flush=True)
            failures += 1
            continue
        print(f"  OK {out.stat().st_size} bytes exit={result.returncode}", flush=True)
    return failures
if __name__ == "__main__":
    sys.exit(main())
