#!/usr/bin/env bash
# Thin 1.1.9 acceptance adapter over tools/linux-release; no downloads or uploads.
# Repeated --dep-lib/--runtime-lib/--binary/--dependency-notices and plugin options
# are forwarded unchanged to the existing assembler. All paths are caller supplied.
set -euo pipefail
exec python3 - "$@" <<'PY'
import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description="Stage and verify a fresh Colosseum 1.1.9 Linux review artifact; not GUI qualification.")
for name in ("source", "build", "qt", "stage", "commit", "patchelf", "report"):
    parser.add_argument("--" + name, required=True)
parser.add_argument("--binary", action="append", default=[])
args, forwarded = parser.parse_known_args()
for binary in args.binary:
    if Path(binary).name in ("colosseum", "lanista"):
        raise SystemExit("Reserved executable cannot be replaced via --binary: " + Path(binary).name)
source, build, stage, report = (Path(getattr(args, n)).resolve() for n in ("source", "build", "stage", "report"))
if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
    raise SystemExit("A full 40-character source commit is required")
head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
if head != args.commit:
    raise SystemExit("Supplied commit does not match source HEAD")
cmake = (source / "native/CMakeLists.txt").read_text()
match = re.search(r"project\(colosseum\s+VERSION\s+([0-9.]+)\b", cmake)
if not match or match.group(1) != "1.1.9":
    raise SystemExit("This acceptance adapter requires source version 1.1.9")
version = match.group(1)
cache = (build / "CMakeCache.txt").read_text()
if not re.search(r"^CMAKE_PROJECT_VERSION:STATIC=1\.1\.9$", cache, re.M):
    raise SystemExit("Build cache is not Colosseum 1.1.9")
if not (build / "lanista").is_file():
    raise SystemExit("Fresh Lanista executable is required alongside colosseum")
qml_root = source / "qml"
material = b""
for path in sorted((p for p in qml_root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(qml_root).as_posix()):
    material += path.relative_to(qml_root).as_posix().encode() + b"\n" + hashlib.sha256(path.read_bytes()).hexdigest().encode() + b"\n"
qml_hash = hashlib.sha256(material).hexdigest()
if "qmlTreeSha256=" + qml_hash not in (build / "qml-build.manifest").read_text():
    raise SystemExit("QML build manifest differs from source QML; refresh the supported build manifest first")
tools = source / "tools/linux-release"
command = [sys.executable, str(tools / "assemble.py")]
for name in ("source", "build", "qt", "stage", "commit", "patchelf"):
    command += ["--" + name, getattr(args, name)]
command += ["--binary", str(build / "lanista")]
for binary in args.binary:
    command += ["--binary", binary]
command += forwarded
subprocess.run(command, check=True)
# The pinned generic assembler contains historical 1.1.8 text. Correct only the
# staged review metadata; never rewrite production packaging or an old artifact.
identity_path = stage / "BUILD-IDENTITY.json"
identity = json.loads(identity_path.read_text())
identity["version"] = version
identity.pop("original_source_commit", None)
identity["artifact_purpose"] = "isolated non-Feria 1.1.9 acceptance review"
identity["packaging_host"] = platform.freedesktop_os_release()
identity["qml_tree_sha256"] = qml_hash
identity["source_worktree_changes"] = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain=v1"], text=True).splitlines()
runtime_diff = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--", "native", "qml"])
identity["source_runtime_diff_sha256"] = hashlib.sha256(runtime_diff).hexdigest()
identity["source_runtime_diff_empty"] = not runtime_diff
identity["lanista_sha256"] = hashlib.sha256((stage / "usr/bin/lanista").read_bytes()).hexdigest()
identity_path.write_text(json.dumps(identity, indent=2) + "\n")
readme = stage / "README-LINUX.txt"
readme.write_text(readme.read_text().replace("Colosseum 1.1.8 Linux candidate", "Colosseum " + version + " Linux acceptance candidate") + "\nBuilt and dependency-checked on the packaging_host recorded in BUILD-IDENTITY.json. No Ubuntu compatibility, graphical acceptance or release readiness is implied.\n")
report.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(tools / "check_package.py"), str(stage), "--report", str(report)], check=True)
print(json.dumps({"status": "PASS", "layer": "package structure and relocated dependency closure", "stage": str(stage), "version": version, "source_commit": args.commit, "qml_tree_sha256": qml_hash, "report": str(report), "gui_status": "NOT RUN"}, indent=2))
PY
