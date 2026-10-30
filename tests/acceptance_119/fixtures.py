#!/usr/bin/env python3
"""Generate owned 1.1.9 acceptance assets; no personal libraries or profile discovery.

Only Python's standard library and ffmpeg are required. CBZ/PDF bytes are stable
across hosts. Media is deterministic within the recorded ffmpeg version; its
actual hash is recorded because codec builds can differ. Paths/identities are
bound on the receiving machine. Never copy fixture-manifest.json across hosts.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import zipfile
import zlib

OWNER = ".acceptance-119-fixtures.json"
EPOCH = 1700000000
SOURCE_SHA = "a6104f7a8d9aab9766e71f031b7908beaeca85ad"
# 5x7 glyphs keep CBZ pages visibly numbered without platform font dependencies.
GLYPHS = {
    "0": ["01110","10001","10011","10101","11001","10001","01110"],
    "1": ["00100","01100","00100","00100","00100","00100","01110"],
    "2": ["01110","10001","00001","00010","00100","01000","11111"],
    "3": ["11110","00001","00001","01110","00001","00001","11110"],
    "4": ["00010","00110","01010","10010","11111","00010","00010"],
    "5": ["11111","10000","10000","11110","00001","00001","11110"],
    "6": ["01110","10000","10000","11110","10001","10001","01110"],
    "7": ["11111","00001","00010","00100","01000","01000","01000"],
    "8": ["01110","10001","10001","01110","10001","10001","01110"],
    "9": ["01110","10001","10001","01111","00001","00001","01110"],
    "A": ["01110","10001","10001","11111","10001","10001","10001"],
    "B": ["11110","10001","10001","11110","10001","10001","11110"],
}


def stamp(path: Path) -> None:
    os.utime(path, (EPOCH, EPOCH))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    stamp(path)


def page_png(page: int, identity: str) -> bytes:
    width, height = 480, 640
    base = ((30 + page * 17) % 160, 55 if identity == "A" else 135, 120 + page * 9)
    pixels = bytearray(bytes(base) * width * height)
    def rect(x, y, w, h, color):
        for yy in range(y, y + h):
            start = (yy * width + x) * 3
            pixels[start:start + w * 3] = bytes(color) * w
    rect(12, 12, width - 24, 10, (255, 230, 100))
    rect(12, height - 22, width - 24, 10, (255, 230, 100))
    for index, char in enumerate(identity + f"{page:02d}"):
        for y, row in enumerate(GLYPHS[char]):
            for x, bit in enumerate(row):
                if bit == "1":
                    rect(70 + index * 118 + x * 18, 240 + y * 18, 18, 18, (255, 255, 245))
    # Physical bar count is an independent visual distinction for pages 1..8.
    for n in range(page):
        rect(40 + n * 50, 460, 30, 50, (255, 230, 100))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    scan = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(scan, 9)) + chunk(b"IEND", b"")


def write_cbz(path: Path, identity: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        for page in range(1, 9):
            entry = zipfile.ZipInfo(f"page-{page:02d}.png", (2023, 11, 14, 22, 13, 20))
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, page_png(page, identity))
    stamp(path)


def write_pdf(path: Path) -> None:
    """A real PDF 1.4 with Helvetica text, valid object offsets, and four pages."""
    pages = [
        ["ACCEPTANCE PDF PAGE 1", "ORBIT common marker on first page.", "ALPHAONLY is unique to page one."],
        ["ACCEPTANCE PDF PAGE 2", "ORBIT common marker on second page.", "BRAVOONLY is unique to page two."],
        ["ACCEPTANCE PDF PAGE 3", "NEPTUNE is unique to page three.", "This page has no repeated search term."],
        ["ACCEPTANCE PDF PAGE 4", "ORBIT common marker on last page.", "DELTAONLY is unique to page four."],
    ]
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R 10 0 R] /Count 4 >>", b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    for i, lines in enumerate(pages):
        objects.append(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 420 594] /Resources << /Font << /F1 3 0 R >> >> /Contents {5 + i * 2} 0 R >>".encode())
        instructions = ["BT /F1 16 Tf 40 520 Td"]
        for n, line in enumerate(lines):
            if n:
                instructions.append("0 -44 Td")
            instructions.append("(" + line.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ") Tj")
        stream = ("\n".join(instructions) + "\nET\n").encode("ascii")
        objects.append(f"<< /Length {len(stream)} >>\nstream\n".encode() + stream + b"endstream")
    objects.append(b"<< /Title (Acceptance 1.1.9 Search PDF) /Author (Synthetic fixture) /CreationDate (D:20231114221320Z) >>")
    output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for i, item in enumerate(objects, 1):
        offsets.append(len(output))
        output.extend(f"{i} 0 obj\n".encode() + item + b"\nendobj\n")
    xref = len(output)
    output.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        output.extend(f"{offset:010d} 00000 n \n".encode())
    output.extend(f"trailer\n<< /Size {len(offsets)} /Root 1 0 R /Info 12 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)
    stamp(path)


def normalized(path: Path) -> str:
    value = path.resolve().as_posix()
    return value.lower() if os.name == "nt" else value


def vault_id(path: Path) -> str:
    stat = path.stat()
    basis = f"{normalized(path)}::{stat.st_size}::{stat.st_mtime_ns // 1000000}"
    return "vault:" + hashlib.sha1(basis.encode("utf-8")).hexdigest()


def _plain_absolute(path: Path) -> Path:
    """Reject redirects before resolve() can erase the evidence of a redirect."""
    path = Path(path)
    if ".." in path.parts:
        raise ValueError(f"Fixture paths must not traverse parents: {path}")
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            # No later component can exist below a missing plain directory.
            break
        except OSError as error:
            raise ValueError(f"Cannot safely inspect fixture path: {current}") from error
        if stat.S_ISLNK(info.st_mode) or (
            getattr(info, "st_file_attributes", 0)
            & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        ):
            raise ValueError(f"Fixture path contains a symlink/junction/reparse point: {current}")
        if current != absolute and not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"Fixture ancestor is not a directory: {current}")
    return absolute


def safe_fixture_path(root: Path, path: str | Path, *, must_exist: bool = False) -> Path:
    """Resolve one source/destination only after validating all path components.

    Both absolute children and root-relative children are supported. Even a link
    to another location *inside* root is rejected; a root alias is rejected too.
    Windows junctions and other reparse points are rejected via lstat attributes.
    """
    root = _plain_absolute(root)
    path = Path(path)
    if ".." in path.parts:
        raise ValueError(f"Fixture paths must not traverse parents: {path}")
    candidate = path if path.is_absolute() else root / path
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise ValueError(f"Fixture path escapes owned root: {candidate}") from error
    candidate = _plain_absolute(candidate)
    try:
        resolved_root = root.resolve(strict=False)
        resolved = candidate.resolve(strict=must_exist)
        resolved.relative_to(resolved_root)
    except (OSError, ValueError) as error:
        raise ValueError(f"Fixture path is unavailable or escapes owned root: {candidate}") from error
    return resolved


def assert_owned(root: Path) -> Path:
    root = _plain_absolute(root)
    if not root.is_dir():
        raise ValueError(f"Fixture root is not a directory: {root}")
    root = root.resolve(strict=True)
    marker = safe_fixture_path(root, OWNER, must_exist=True)
    try:
        ownership = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid fixture ownership marker: {marker}") from error
    if (not isinstance(ownership, dict)
            or ownership.get("schema") != "colosseum.acceptance119.fixtures.v1"
            or ownership.get("source_sha") != SOURCE_SHA
            or ownership.get("root") != root.as_posix()):
        raise ValueError(f"Fixture ownership/source/root does not match: {root}")
    return root


def apply_mutation(root: Path, mutation: str) -> None:
    root = assert_owned(root)
    if mutation == "comic-a-missing":
        original = safe_fixture_path(root, "comics/a/twin.cbz", must_exist=True)
        away = safe_fixture_path(root, "comics/a/twin.cbz.unavailable")
        if away.exists():
            raise ValueError("Missing-file mutation already applied")
        original.rename(away)
    elif mutation == "comic-a-restore":
        away = safe_fixture_path(root, "comics/a/twin.cbz.unavailable", must_exist=True)
        original = safe_fixture_path(root, "comics/a/twin.cbz")
        if original.exists():
            raise ValueError("Restore would overwrite an existing comic")
        away.rename(original)
    elif mutation == "vault-add-clip":
        reference = safe_fixture_path(root, "media/reference.mp4", must_exist=True)
        added = safe_fixture_path(root, "vault/clips/loose/clip-gamma.mp4")
        if added.exists():
            raise ValueError("Refresh fixture already exists")
        shutil.copy2(reference, added)
    elif mutation == "vault-remove-added-clip":
        safe_fixture_path(root, "vault/clips/loose/clip-gamma.mp4", must_exist=True).unlink()
    else:
        raise ValueError(f"Unknown bounded fixture mutation: {mutation}")


def generate(root: Path, ffmpeg: str = "ffmpeg") -> dict:
    root = _plain_absolute(root).resolve(strict=False)
    if root.exists() and any(root.iterdir()):
        # Regeneration could destroy a pending restart/missing-file phase; refuse.
        raise ValueError(f"Use a fresh, empty fixture directory: {root}")
    executable = shutil.which(ffmpeg)
    if not executable:
        raise RuntimeError("ffmpeg is required; playable media must not be replaced by stubs")
    root.mkdir(parents=True, exist_ok=True)
    write_json(root / OWNER, {"schema": "colosseum.acceptance119.fixtures.v1", "source_sha": SOURCE_SHA, "root": root.as_posix()})
    write_cbz(root / "comics/a/twin.cbz", "A")
    write_cbz(root / "comics/b/twin.cbz", "B")
    write_pdf(root / "books/search.pdf")
    (root / "books/malformed.pdf").write_bytes(b"%PDF-1.4\nThis intentionally has no valid objects, pages, or xref.\n")
    stamp(root / "books/malformed.pdf")
    video = root / "media/reference.mp4"
    video.parent.mkdir()
    cmd = [executable, "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=5", "-t", "90", "-an", "-c:v", "mpeg4", "-q:v", "7", "-threads", "1", "-fflags", "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1", "-movflags", "+faststart", str(video)]
    encoded = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=90)
    stamp(video)
    shapes = {
        "empty": [],
        "film": ["acceptance-film-2024/acceptance-film-2024.mp4"],
        "sibling": ["acceptance-show-s01/acceptance-show-s01e01.mp4", "acceptance-show-s01/acceptance-show-s01e02.mp4", "acceptance-show-s02/acceptance-show-s02e01.mp4", "acceptance-show-s02/acceptance-show-s02e02.mp4"],
        "nested": ["acceptance-nested/season 1/acceptance-nested-s01e01.mp4", "acceptance-nested/season 1/acceptance-nested-s01e02.mp4", "acceptance-nested/season 2/acceptance-nested-s02e01.mp4"],
        "anime": ["acceptance-anime/acceptance-anime-001.mp4", "acceptance-anime/acceptance-anime-002.mp4"],
        "clips": ["loose/clip-alpha.mp4", "loose/clip-beta.mp4"],
    }
    for shape, files in shapes.items():
        (root / "vault" / shape).mkdir(parents=True)
        for relative in files:
            destination = root / "vault" / shape / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(video, destination)
    tokens = {"FIXTURE_ROOT": root.as_posix()}
    for shape in [*shapes, "unavailable"]:
        media_root = root / "vault" / shape
        config = {"version": 1, "roots": [{"path": normalized(media_root), "confirmed": True, "addedAtMs": 1}], "scanIgnore": [], "hidden": [], "kinds": {}}
        if shape == "anime":
            config["kinds"][normalized(media_root)] = "anime"
        write_json(root / "seeds" / f"vault-{shape}" / "vault/config.json", config)
        tokens[f"VAULT_{shape.upper()}_ROOT"] = normalized(media_root)
    recent_sets = {"comic": [("comics/a/twin.cbz", "comic"), ("comics/b/twin.cbz", "comic")], "pdf": [("books/search.pdf", "book")], "pdf-malformed": [("books/malformed.pdf", "book")], "pdf-unavailable": [("books/unavailable.pdf", "book")]}
    for seed, files in recent_sets.items():
        entries = []
        for relative, kind in files:
            path = root / relative
            entries.append({"path": path.as_posix(), "title": path.stem, "kind": kind, "vaultId": vault_id(path) if path.exists() else "vault:acceptance119-missing-pdf"})
        write_json(root / "seeds" / seed / "vault/open-recent.json", {"items": entries})
    tokens["COMIC_A_ID"] = vault_id(root / "comics/a/twin.cbz")
    tokens["COMIC_B_ID"] = vault_id(root / "comics/b/twin.cbz")
    tokens["PDF_ID"] = vault_id(root / "books/search.pdf")
    manifest = {
        "schema": "colosseum.acceptance119.fixtures.v1", "source_sha": SOURCE_SHA,
        "tokens": tokens, "comic": {"pages": 8, "interior_page": 4, "same_basename": "twin.cbz"},
        "pdf": {"pages": 4, "queries": {"ORBIT": {"count": 3, "pages": [1, 2, 4]}, "NEPTUNE": {"count": 1, "pages": [3]}, "NO_SUCH_TOKEN": {"count": 0, "pages": []}}},
        "media": {"width": 160, "height": 90, "duration_seconds": 90, "codec": "mpeg4", "command": cmd, "stderr": encoded.stderr, "ffmpeg_version": subprocess.run([executable, "-version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]},
        "seeds": {name: (root / "seeds" / name).as_posix() for name in [*recent_sets, *(f"vault-{name}" for name in [*shapes, "unavailable"])]},
        "mutations": ["comic-a-missing", "comic-a-restore", "vault-add-clip", "vault-remove-added-clip"],
        "files": {},
    }
    for file in sorted(root.rglob("*")):
        if file.is_file():
            manifest["files"][file.relative_to(root).as_posix()] = {"sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "bytes": file.stat().st_size}
    write_json(root / "fixture-manifest.json", manifest)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--mutate", choices=["comic-a-missing", "comic-a-restore", "vault-add-clip", "vault-remove-added-clip"])
    args = parser.parse_args()
    if args.mutate:
        apply_mutation(args.root, args.mutate)
        print(json.dumps({"mutation": args.mutate, "status": "APPLIED"}))
    else:
        manifest = generate(args.root, args.ffmpeg)
        print(json.dumps({"manifest": str(args.root.resolve() / "fixture-manifest.json"), "files": len(manifest["files"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
