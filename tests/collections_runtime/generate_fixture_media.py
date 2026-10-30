#!/usr/bin/env python3
"""Create deterministic synthetic media only inside an existing marked test root.

Python 3.9+ standard library only. Does not launch a player, execute a codec,
contact the network, create an app profile, or write Library/Vault state.
The actual VinylLocalSource importer owns the resulting music identities.

AVI layout: https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import wave


MARKER = ".collections-synthetic-fixture"
MARKER_BYTES = b"collections-synthetic-fixture-v1\n"
OUTPUT_DIRECTORY = "collections-fixture-media"
FIXED_MTIME_NS = int(datetime.datetime(2026, 10, 29, tzinfo=datetime.timezone.utc).timestamp()) * 1_000_000_000
SAMPLE_RATE = 8000
SECONDS = 2
WIDTH, HEIGHT, FPS = 160, 96, 10


class FixtureError(Exception):
    pass


def no_link(path: Path) -> os.stat_result:
    info = path.lstat()
    reparse = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & reparse:
        raise FixtureError(f"Linked/reparse path is not allowed: {path}")
    return info


def require_fixture_root(root_text: str) -> Path:
    tag = os.environ.get("COLOSSEUM_APPDATA_TAG", "")
    if os.environ.get("COLOSSEUM_COLLECTIONS_TEST_FIXTURES") != "1" or not re.fullmatch(
        r"collections-fixture-[A-Za-z0-9_-]{1,48}", tag
    ):
        raise FixtureError("The existing Collections fixture environment gates are required.")
    root = Path(root_text)
    canonical_separators = lambda value: value.replace("\\", "/") if os.name == "nt" else value
    if not root.is_absolute() or canonical_separators(os.path.normpath(root_text)) != canonical_separators(root_text):
        raise FixtureError("--root must be an explicit absolute clean path.")
    if root.name != "Colosseum-dltest-" + tag or not stat.S_ISDIR(no_link(root).st_mode):
        raise FixtureError("Root must already be the tagged synthetic app-data directory.")
    for ancestor in root.parents:
        if not stat.S_ISDIR(no_link(ancestor).st_mode):
            raise FixtureError("Root ancestors must be existing ordinary directories.")
    marker = root / MARKER
    info = no_link(marker)
    if not stat.S_ISREG(info.st_mode) or info.st_size != len(MARKER_BYTES) or marker.read_bytes() != MARKER_BYTES:
        raise FixtureError("The existing synthetic-fixture marker is missing or invalid.")
    pending = [root]
    count = 0
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                count += 1
                if count > 10_000:
                    raise FixtureError("Fixture root exceeds the controller's 10,000-entry bound.")
                path = Path(entry.path)
                facts = no_link(path)
                if stat.S_ISDIR(facts.st_mode):
                    pending.append(path)
                elif not stat.S_ISREG(facts.st_mode):
                    raise FixtureError(f"Non-file fixture entry is not allowed: {path}")
    return root


def chunk(kind: bytes, body: bytes) -> bytes:
    return kind + struct.pack("<I", len(body)) + body + (b"\x00" if len(body) % 2 else b"")


def riff(kind: bytes, body: bytes) -> bytes:
    return chunk(b"RIFF", kind + body)


def info_list(title: str, description: str) -> bytes:
    return chunk(b"LIST", b"INFO" + chunk(b"INAM", title.encode("ascii") + b"\0")
                 + chunk(b"ICMT", description.encode("ascii") + b"\0")
                 + chunk(b"ISFT", b"Colosseum deterministic fixture generator v1\0"))


def silent_wav() -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(SAMPLE_RATE)
        stream.writeframes(b"\x00\x00" * (SAMPLE_RATE * SECONDS))
    encoded = output.getvalue()
    # Preserve Python's PCM fmt/data chunks and append valid RIFF INFO metadata.
    return riff(b"WAVE", encoded[12:] + info_list(
        "Synthetic Silence (Collections UI fixture)",
        "Two seconds of generated digital silence. No recorded music or artist performance."))


# A small built-in bitmap alphabet keeps the AVI visibly labelled with no font,
# image, codec, or third-party package dependency. Strings are 5x7 glyph rows.
GLYPHS = {
    " ": ("00000",) * 7,
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "N": ("10001", "11001", "11001", "10101", "10011", "10011", "10001"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
}


def video_frame(frame_number: int) -> bytes:
    stride = (WIDTH * 3 + 3) & ~3
    pixels = bytearray(stride * HEIGHT)

    def pixel(x: int, y: int, rgb: tuple[int, int, int]) -> None:
        # Positive BITMAPINFOHEADER height is bottom-up, with BGR24 pixels.
        offset = (HEIGHT - y - 1) * stride + x * 3
        pixels[offset:offset + 3] = bytes(reversed(rgb))

    for y in range(HEIGHT):
        for x in range(WIDTH):
            pixel(x, y, (48, 40, 64) if y < 76 else (32, 76, 76))
    for text, top in (("SYNTHETIC", 22), ("UI FIXTURE", 44)):
        left = (WIDTH - (len(text) * 6 - 1) * 2) // 2
        for letter_number, letter in enumerate(text):
            for y, row in enumerate(GLYPHS[letter]):
                for x, value in enumerate(row):
                    if value == "1":
                        for dy in range(2):
                            for dx in range(2):
                                pixel(left + letter_number * 12 + x * 2 + dx, top + y * 2 + dy, (240, 232, 248))
    # A deterministic moving patch makes every frame real and distinguishable.
    for y in range(82, 90):
        for x in range(4 + frame_number * 7, 12 + frame_number * 7):
            pixel(x, y, (232, 208, 128))
    return bytes(pixels)


def uncompressed_avi() -> bytes:
    frame_count = FPS * SECONDS
    frame_bytes = ((WIDTH * 3 + 3) & ~3) * HEIGHT
    main = struct.pack("<14I", 1_000_000 // FPS, (frame_bytes + 8) * FPS, 0, 0x10,
                       frame_count, 0, 1, frame_bytes, WIDTH, HEIGHT, 0, 0, 0, 0)
    stream = struct.pack("<4s4sIHH8I4h", b"vids", b"DIB ", 0, 0, 0,
                         0, 1, FPS, 0, frame_count, frame_bytes, 0xFFFFFFFF, 0,
                         0, 0, WIDTH, HEIGHT)
    bitmap = struct.pack("<IiiHHIIiiII", 40, WIDTH, HEIGHT, 1, 24, 0, frame_bytes, 0, 0, 0, 0)
    header = chunk(b"LIST", b"hdrl" + chunk(b"avih", main) + chunk(b"LIST", b"strl"
                   + chunk(b"strh", stream) + chunk(b"strf", bitmap)
                   + chunk(b"strn", b"Synthetic Collections UI color fixture\0")))
    frames = []
    index = []
    offset = 4  # AVI 1.0 idx1 offsets are relative to the movi list's type code.
    for number in range(frame_count):
        encoded = chunk(b"00db", video_frame(number))
        frames.append(encoded)
        index.append(struct.pack("<4sIII", b"00db", 0x10, offset, frame_bytes))
        offset += len(encoded)
    return riff(b"AVI ", header + chunk(b"LIST", b"movi" + b"".join(frames))
                + chunk(b"idx1", b"".join(index)) + info_list(
                    "Synthetic Collections UI color fixture",
                    "Computer-generated labelled color patches. This video is not the film Arrival or other real media."))


def catalogue() -> bytes:
    track = {"file": "synthetic-silence.wav", "title": "Two seconds of synthetic silence",
             "disc": 1, "number": 1, "seconds": SECONDS,
             "notes": "Generated silent PCM test audio; no recorded music."}
    manifest = {
        "version": 1,
        "artist": {"key": "synthetic-artist", "name": "Collections Synthetic Artist",
                   "biography": "A synthetic metadata label for UI verification, not a real performer."},
        "releases": [{"key": "synthetic-silence", "title": "Synthetic Silence (UI fixture)",
                      "kind": "album", "year": "2026", "genre": "Test signal",
                      "about": "Generated digital silence for Collections album, song, artist and edition checks.",
                      "editions": [
                          {"key": "standard", "title": "Silent standard fixture", "tracks": [track]},
                          {"key": "alternate", "title": "Alternate fixture edition", "tracks": [track]},
                      ]}],
    }
    return (json.dumps(manifest, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def generate(root_text: str) -> dict:
    root = require_fixture_root(root_text)
    target = root / OUTPUT_DIRECTORY
    if os.path.lexists(target):
        raise FixtureError(f"No overwrite: destination already exists: {target}")
    files = {
        "music/synthetic-silence.wav": silent_wav(),
        "music/synthetic.vinyl.json": catalogue(),
        "video/arrival-ui-fixture/synthetic-color.avi": uncompressed_avi(),
        "README.txt": (
            "SYNTHETIC COLLECTIONS UI FIXTURES\n\n"
            "The WAV is two seconds of mono 8000 Hz, 16-bit signed PCM digital silence.\n"
            "The AVI is two seconds of labelled 160x96 BGR24 frames at 10 fps, uncompressed.\n"
            "It is not Arrival or any other real film. Any adopted IMDb identifier belongs\n"
            "only to a separate, explicitly labelled route-verification fixture.\n"
            "The Vinyl manifest uses one generated track in two named test editions.\n"
            "Music IDs must be created by VinylLocalSource and VaultIdentity, never invented.\n"
            "Contents and file modification times are deterministic (2026-10-29 UTC).\n"
            "This generator never opens a player or changes app owner data.\n"
        ).encode("ascii"),
    }
    records = [{"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
               for name, data in files.items()]
    files["fixture-media-receipt.json"] = (json.dumps(
        {"fixture": "collections-synthetic-media-v1", "fixedMtimeUtc": "2026-10-29T00:00:00Z", "files": records},
        indent=2) + "\n").encode("utf-8")
    require_fixture_root(root_text)
    target.mkdir(mode=0o700)  # Exclusive creation also prevents concurrent generators.
    for folder in (target / "music", target / "video", target / "video" / "arrival-ui-fixture"):
        folder.mkdir(mode=0o700)
    for name, data in files.items():
        require_fixture_root(root_text)
        path = target / name
        with path.open("xb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        no_link(path)
        if os.utime in os.supports_follow_symlinks:
            os.utime(path, ns=(FIXED_MTIME_NS, FIXED_MTIME_NS), follow_symlinks=False)
        else:
            os.utime(path, ns=(FIXED_MTIME_NS, FIXED_MTIME_NS))
    require_fixture_root(root_text)
    return {"ok": True, "root": str(root), "directory": str(target),
            "manifest": str(target / "music" / "synthetic.vinyl.json"),
            "wav": str(target / "music" / "synthetic-silence.wav"),
            "video": str(target / "video" / "arrival-ui-fixture" / "synthetic-color.avi"),
            "files": records}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="Existing absolute marked/tagged synthetic app-data root; no overwrite.")
    args = parser.parse_args()
    try:
        result = generate(args.root)
    except (FixtureError, OSError, ValueError) as error:
        print(f"generate_fixture_media: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
