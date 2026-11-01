"""Launch the matching Windows candidate only in a marked synthetic profile.

The normal startup resolver verifies the build's QML manifest. No QML override,
personal profile, installed application, or live-reload mode is used.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from generate_fixture_media import require_fixture_root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--phase", required=True)
    parser.add_argument("--pipe", default="CollectionsIntegration-20261029")
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build", type=Path)
    parser.add_argument("--phone-shell", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9_-]+", args.phase) or not args.pipe.startswith("CollectionsIntegration-"):
        parser.error("Use an isolated pipe and simple phase name")
    tag = Path(args.root).name.removeprefix("Colosseum-dltest-")
    os.environ["COLOSSEUM_APPDATA_TAG"] = tag
    os.environ["COLOSSEUM_COLLECTIONS_TEST_FIXTURES"] = "1"
    root = require_fixture_root(args.root)
    source = args.source.resolve()
    build = (args.build or source.parent / "build").resolve()
    executable = build / "colosseum.exe"
    assert executable.is_file() and (source / "qml/Main.qml").is_file()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for key in ("COLOSSEUM_DEV", "QT_ENABLE_HIGHDPI_SCALING", "QT_SCALE_FACTOR"):
        env.pop(key, None)
    env.update(COLOSSEUM_LANISTA_PIPE=args.pipe, COLOSSEUM_LANISTA_DRIVE="1",
               COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0", QML_DISABLE_DISK_CACHE="1",
               QT_FORCE_STDERR_LOGGING="1")
    paths = [r"C:\Qt\6.11.1\msvc2022_64\bin",
             r"C:\tools\mpvqt-feasibility\libmpv-prefix\bin",
             r"C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin",
             r"C:\tools\libtorrent-2.0-msvc\bin", r"C:\tools\openssl-msvc\bin", str(build)]
    env["PATH"] = os.pathsep.join(paths + [env.get("PATH", "")])
    arguments = [str(executable)] + (["--phone-shell=520x820"] if args.phone_shell else [])
    receipt = {"phase": args.phase, "exe": str(executable), "source": str(source),
               "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
               "qmlManifest": (build / "qml-build.manifest").read_text(),
               "arguments": arguments[1:], "pipe": args.pipe, "appDataRoot": str(root),
               "appDataTag": tag, "nativeDpi": True, "automationFocusDiagnostics": False,
               "startedAt": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    with (evidence / (args.phase + ".stdout.txt")).open("xb") as stdout, \
         (evidence / (args.phase + ".stderr.txt")).open("xb") as stderr:
        process = subprocess.Popen(arguments, cwd=source, env=env, stdout=stdout, stderr=stderr)
        receipt["pid"] = process.pid
        receipt_path = evidence / ("launch-" + args.phase + ".json")
        receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(json.dumps(receipt), flush=True)
        result = process.wait()
    receipt["exitCode"] = result
    receipt["exitedAt"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
