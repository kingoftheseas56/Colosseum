#!/usr/bin/env python3
"""Wrap the existing checked AppDir; never build, publish, or weaken ABI gates."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from assemble import sha

REQUIRED_FILES = (
    "AppRun", "BUILD-IDENTITY.json", "usr/bin/colosseum", "usr/bin/qml-build.manifest",
    "usr/lib/libQt6Core.so.6", "usr/lib/libQt6WebEngineCore.so.6",
    "usr/lib/libQt6Quick3D.so.6", "usr/qml/QtQuick3D/qmldir",
    "usr/libexec/QtWebEngineProcess", "usr/resources/icudtl.dat",
    "usr/resources/qtwebengine_resources.pak", "usr/plugins/platforms/libqoffscreen.so",
    "usr/plugins/platforms/libqxcb.so", "usr/lib/libmpv.so.2",
    "usr/bin/ffmpeg", "usr/bin/ffprobe", "usr/lib/libssl.so.3", "usr/lib/libcrypto.so.3",
    "assets/icons/colosseum.svg", "licenses/LICENSE", "licenses/THIRD_PARTY_NOTICES.md",
    "licenses/runtime/manifest.json", "licenses/runtime/AppImage-runtime/LICENSE",
)
REQUIRED_NOTICES = ("qt6-main", "qt6-webengine", "qt6-quick3d", "mpv", "ffmpeg", "openssl")


def image_name(identity: dict) -> str:
    version = identity.get("version", "")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Expected numeric CMake project version")
    if identity.get("architecture") != "x86_64":
        raise ValueError("Only x86_64 is qualified by this workflow")
    return f"Colosseum-{version}-x86_64.AppImage"


def verify_abi(report: dict) -> None:
    if report.get("status") != "PASS" or report.get("failures") or report.get("elf_count", 0) <= 0:
        raise ValueError("ELF closure verification did not pass")
    glibc = report.get("required_symbol_versions_max", {}).get("GLIBC")
    if not isinstance(glibc, str) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", glibc):
        raise ValueError("Unknown GLIBC requirement; cannot claim Ubuntu 22.04 baseline")
    if tuple(map(int, glibc.split("."))) > (2, 35):
        raise ValueError(f"GLIBC {glibc} exceeds Ubuntu 22.04 baseline 2.35")


def validate_stage(stage: Path, expected_commit: str) -> dict:
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        raise ValueError("Full source commit required")
    for relative in REQUIRED_FILES:
        path = stage / relative
        if not path.is_file() or not path.resolve().is_relative_to(stage.resolve()):
            raise ValueError("Missing or escaping AppImage input: " + relative)
    identity = json.loads((stage / "BUILD-IDENTITY.json").read_text())
    image_name(identity)
    if identity.get("source_commit") != expected_commit:
        raise ValueError("AppDir source commit differs from the requested source commit")
    notices = stage / "licenses/runtime"
    packages = json.loads((notices / "manifest.json").read_text()).get("packages", [])
    for name in REQUIRED_NOTICES:
        records = [record for record in packages if record.get("name") == name]
        if len(records) != 1 or not records[0].get("license_files"):
            raise ValueError("Missing or ambiguous unchanged license notices for " + name)
        for relative in records[0]["license_files"]:
            path = (notices / relative).resolve()
            if not path.is_relative_to(notices.resolve()) or not path.is_file():
                raise ValueError("Missing or escaping license notice: " + relative)
    return identity


def validate_image(image: Path) -> None:
    with image.open("rb") as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01"
            or header[8:11] != b"AI\x02" or header[18:20] != b"\x3e\x00"):
        raise ValueError("Not an x86_64 type-2 AppImage")


def build_command(tool: Path, runtime: Path, stage: Path, output: Path) -> list[str]:
    return [str(tool), "--appimage-extract-and-run", "--runtime-file", str(runtime),
            "--comp", "xz", "--no-appstream", str(stage), str(output)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--appimagetool", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    output_dir = args.output_dir.resolve()
    if output_dir == stage or output_dir.is_relative_to(stage):
        parser.error("Output must be outside the AppDir")
    identity = validate_stage(stage, args.source_commit)
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / image_name(identity)
    manifest = output.with_suffix(output.suffix + ".json")
    checksum = output.with_suffix(output.suffix + ".sha256")
    if any(path.exists() for path in (output, manifest, checksum)):
        parser.error("Refusing to overwrite an AppImage or its evidence")
    report_path = output_dir / "appimage-package-check.json"
    subprocess.run([sys.executable, str(Path(__file__).with_name("check_package.py")),
                    str(stage), "--report", str(report_path)], check=True)
    report = json.loads(report_path.read_text())
    verify_abi(report)
    if report.get("source_commit") != args.source_commit:
        raise ValueError("Package checker source commit mismatch")
    # Preserve the common launcher at the same root; its HERE-relative paths stay valid.
    original_launcher = stage / "AppRun.colosseum"
    if original_launcher.exists():
        raise ValueError("Refusing to rewrap an already modified AppDir")
    shutil.copy2(stage / "AppRun", original_launcher)
    shutil.copy2(Path(__file__).with_name("AppRun.appimage"), stage / "AppRun")
    (stage / "AppRun").chmod(0o755)
    # AppImage root metadata is distinct from the executable's AppRun layout.
    desktop = stage / "colosseum.desktop"
    desktop.write_text("[Desktop Entry]\nType=Application\nName=Colosseum\n"
                       "Comment=One media app for all art\nExec=colosseum\nIcon=colosseum\n"
                       "Terminal=false\nCategories=AudioVideo;Graphics;\n", encoding="utf-8")
    shutil.copy2(stage / "assets/icons/colosseum.svg", stage / "colosseum.svg")
    shutil.copy2(stage / "colosseum.svg", stage / ".DirIcon")
    (stage / "README-LINUX.txt").write_text(
        f"Colosseum {identity['version']} x86_64 AppImage candidate\n"
        "chmod +x Colosseum-*-x86_64.AppImage; then run the file as a normal user.\n"
        "Without FUSE, append --appimage-extract-and-run.\n"
        "Requires glibc 2.35 or newer and a functioning Linux desktop, system graphics drivers,\n"
        "fonts, audio service and CA trust store. Host graphics drivers and glibc are not bundled.\n"
        "Container offscreen smoke is not proof of GPU rendering, playback, Wayland or every distro.\n"
        "The build identity, ELF closure/ABI report and unchanged dependency notices are included.\n"
        "Updates are manual. No profiles, credentials or private media are bundled.\n"
        "This is a review candidate, not a published release or license-compliance certification.\n",
        encoding="utf-8")
    environment = os.environ.copy()
    environment.update({"ARCH": "x86_64", "VERSION": identity["version"]})
    # No implicit download of the moving AppImage runtime is allowed.
    for key in ("APPIMAGETOOL_RUNTIME_FILE", "APPIMAGETOOL_SIGN_PASSPHRASE"):
        environment.pop(key, None)
    with tempfile.TemporaryDirectory(prefix="colosseum-appimage-", dir=output_dir) as directory:
        intermediate = Path(directory) / output.name
        subprocess.run(build_command(args.appimagetool.resolve(), args.runtime.resolve(),
                                     stage, intermediate), env=environment, check=True, timeout=900)
        validate_image(intermediate)
        intermediate.chmod(0o755)
        intermediate.replace(output)
    digest = sha(output)
    checksum.write_text(digest + "  " + output.name + "\n", encoding="utf-8")
    receipt = {"status": "PACKAGED_NOT_RUNTIME_VERIFIED", "file": output.name,
               "version": identity["version"], "source_commit": args.source_commit,
               "architecture": "x86_64", "sha256": digest, "bytes": output.stat().st_size,
               "glibc_ceiling": "2.35", "required_symbol_versions_max": report["required_symbol_versions_max"],
               "appimagetool_sha256": sha(args.appimagetool), "runtime_sha256": sha(args.runtime),
               "binary_sha256": identity["binary_sha256"]}
    manifest.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
