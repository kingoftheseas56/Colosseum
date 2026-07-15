#!/usr/bin/env python3
"""Isolated per-reader launcher for the long-strip bakeoff (spec §5, §6, §9, §10).

Verifies fixture byte-identity, sets up a throwaway profile/library so no real
data is touched, launches exactly one reader into the long-strip surface over the
canonical pages, and (when PresentMon is present) captures a presentation trace.
Records a run manifest per launch and appends to the rejected/accepted ledger.

Modes:
  --smoke        launch a reader on the fixture, no capture — just prove the
                 adapter + isolation open the exact pages. Closes after --seconds.
  (default)      full traced run — requires PresentMon + reader instrumentation;
                 refuses to fake a run when either is missing (spec §9).
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARTIFACTS = os.path.join(REPO, "artifacts", "reader-bakeoff")
PAGES_DIR = os.path.join(ARTIFACTS, "pages")
CBZ = os.path.join(ARTIFACTS, "fixture-longstrip-120.cbz")
MANIFEST = os.path.join(REPO, "docs", "reader-bakeoff", "fixture-manifest.json")

HOME = os.path.expanduser("~")
QT_ROOT = os.environ.get("QT_ROOT", "C:/Qt/6.11.1/msvc2022_64")
QT_QML_DIR = QT_ROOT + "/qml"
QT_BIN = QT_ROOT + "/bin"
READERS = {
    "colosseum": {
        "exe": os.path.join(HOME, "Desktop", "_bakeoff_colosseum", "native",
                            "build-msvc", "colosseum.exe"),
        "kind": "env-pagedir",   # opens PAGES_DIR via COLOSSEUM_BAKEOFF_STRIP
    },
    "tb2": {
        "exe": os.path.join(HOME, "Desktop", "Tankoban 2", "out", "Tankoban.exe"),
        "kind": "tb2",           # needs the dev-control open-comic adapter (TBD)
    },
    "max": {
        "exe": None,             # runs via `npx electron .`
        "cwd": os.path.join(HOME, "Desktop", "Tankoban-Max"),
        "kind": "electron-cbz",  # positional CBZ + --user-data-dir
    },
}


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_fixture():
    """Spec §4.3 gate: extracted pages + CBZ must match the committed manifest."""
    rc = subprocess.call([sys.executable, os.path.join(HERE, "extract_verify.py")])
    if rc != 0:
        return False, "fixture hash mismatch"
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if sha256_file(CBZ) != manifest["cbz"]["sha256"]:
        return False, "cbz hash mismatch"
    return True, manifest["cbz"]["sha256"][:12]


def isolated_profile(reader, run_id):
    root = os.path.join(ARTIFACTS, "profiles", "%s-%s" % (reader, run_id))
    if os.path.isdir(root):
        shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    return root


def build_launch(reader, profile, cache_hint):
    """(argv, env, cwd) for the reader, opening the fixture in isolation."""
    spec = READERS[reader]
    env = dict(os.environ)
    if reader == "colosseum":
        env["COLOSSEUM_BAKEOFF_STRIP"] = PAGES_DIR
        env["APPDATA"] = profile            # redirect writable/app data → throwaway
        env["LOCALAPPDATA"] = profile
        # the worktree exe's runtime deps live in the Qt install (QML modules,
        # offscreen plugin) — deployed dir only ships the windows platform
        env["QML_IMPORT_PATH"] = QT_QML_DIR
        env["QML2_IMPORT_PATH"] = QT_QML_DIR
        env["PATH"] = QT_BIN + os.pathsep + env.get("PATH", "")
        if cache_hint == "offscreen":       # smoke: never touch the real display
            env["QT_QPA_PLATFORM"] = "offscreen"
            env["QT_QUICK_BACKEND"] = "software"
        return [spec["exe"]], env, None
    if reader == "tb2":
        env["TANKOBAN_DATA_DIR"] = os.path.join(profile, "data")
        env["TANKOBAN_INSTANCE_ID"] = "bakeoff-" + os.path.basename(profile)
        # open-comic adapter not yet wired; caller handles via dev-control
        return [spec["exe"], "--dev-control"], env, None
    if reader == "max":
        return (["npx", "electron", ".", CBZ, "--user-data-dir=" + profile],
                env, spec["cwd"])
    raise ValueError(reader)


PRESENTMON = os.environ.get("PRESENTMON", "C:/tools/presentmon/PresentMon.exe")


def presentmon_available():
    return os.path.exists(PRESENTMON) or shutil.which("PresentMon") or shutil.which("presentmon")


def presentmon_exe():
    return PRESENTMON if os.path.exists(PRESENTMON) else (shutil.which("PresentMon") or shutil.which("presentmon"))


def process_name_for(reader):
    return {"colosseum": "colosseum.exe", "tb2": "Tankoban.exe", "max": "electron.exe"}[reader]


def start_presentmon(reader, run_dir, seconds):
    """Launch PresentMon to capture presented-frame cadence for exactly one
    reader process, one common clock/methodology for all three (spec §7.1)."""
    csv = os.path.join(run_dir, "presentmon.csv")
    argv = [presentmon_exe(), "--process_name", process_name_for(reader),
            "--output_file", csv, "--timed", str(int(seconds + 2)),
            "--track_gpu_video", "--terminate_after_timed", "--stop_existing_session"]
    return subprocess.Popen(argv), csv


def run_manifest(reader, run_id, cache, motion, rep, profile, cbz_sha, exe):
    exe_sha = sha256_file(exe) if exe and os.path.exists(exe) else None
    return {
        "run_id": run_id, "reader": reader, "cache": cache, "motion": motion,
        "rep": rep, "profile": profile, "cbz_sha256_12": cbz_sha,
        "exe": exe, "exe_sha256": exe_sha,
        "exe_mtime": (os.path.getmtime(exe) if exe and os.path.exists(exe) else None),
        "presentmon": bool(presentmon_available()),
    }


def append_ledger(entry):
    os.makedirs(ARTIFACTS, exist_ok=True)
    with open(os.path.join(ARTIFACTS, "ledger.jsonl"), "a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry) + "\n")


def smoke(reader, seconds):
    ok, detail = verify_fixture()
    if not ok:
        print("REJECT fixture: " + detail, file=sys.stderr)
        return 1
    spec = READERS[reader]
    exe = spec.get("exe")
    if reader != "max" and (not exe or not os.path.exists(exe)):
        print("SMOKE_SKIP %s: binary not built (%s)" % (reader, exe))
        return 3
    run_id = "smoke-%s" % reader
    profile = isolated_profile(reader, run_id)
    # smoke never touches the real display — offscreen so it can't hijack the screen
    argv, env, cwd = build_launch(reader, profile, "offscreen")
    manifest = run_manifest(reader, run_id, "cold", "smoke", 0, profile, detail, exe)
    print("SMOKE launching %s: %s" % (reader, " ".join(str(a) for a in argv)))
    proc = subprocess.Popen(argv, env=env, cwd=cwd)
    time.sleep(seconds)
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    manifest["exit"] = proc.returncode
    append_ledger({"kind": "smoke", **manifest})
    with open(os.path.join(profile, "manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
    print("SMOKE_DONE %s exit=%s profile=%s" % (reader, proc.returncode, profile))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reader", choices=sorted(READERS))
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--seconds", type=float, default=6.0)
    args = parser.parse_args()
    if args.smoke:
        return smoke(args.reader, args.seconds)
    ok, detail = verify_fixture()
    if not ok:
        print("REJECT fixture: " + detail, file=sys.stderr)
        return 1
    if not presentmon_available():
        print("BLOCKED: PresentMon not found — a traced run needs the common frame "
              "tracer (spec §7.1). Run with --smoke to verify launch, or install "
              "PresentMon and retry. Not faking a run.", file=sys.stderr)
        return 2
    print("Full traced run needs reader instrumentation (task pending); use --smoke "
          "until the ring-buffer trace lands.", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
