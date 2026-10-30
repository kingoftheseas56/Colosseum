#!/usr/bin/env python3
"""Seed a labelled persisted-index comic fixture from explicit repository CBZs.

This is the completed-index fixture technique in
tests/comic_downloader_pack_demux_harness.cpp::runPackVolumesReadApiScenario.
It is NOT an ingest/download test. Actual ComicDownloader.loadIndex, packVolumes
and UI acceptance must be checked in the application after this command.

Python standard library plus the sibling generate_fixture_media.py root guard.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import zipfile
import zlib

# Importing the shared guard must not create a cache beside the source script.
sys.dont_write_bytecode = True
from generate_fixture_media import FIXED_MTIME_NS, FixtureError, no_link, require_fixture_root


SERIES_ID = "comic-packs:collections-synthetic-fixture"
SERIES_TITLE = "Synthetic comic pack (repository fixtures)"
MAX_ARCHIVE_BYTES = 4 * 1024 * 1024
MAX_PAGE_BYTES = 4 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 16 * 1024 * 1024


def png_facts(data: bytes) -> dict:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise FixtureError("A declared PNG page has no PNG signature.")
    position, header, compressed = 8, None, bytearray()
    ended, idat_closed, seen_idat, palette = False, False, False, False
    while position < len(data):
        if position + 12 > len(data):
            raise FixtureError("Truncated PNG chunk.")
        size = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        if not re.fullmatch(b"[A-Za-z]{4}", kind) or kind[2] & 0x20:
            raise FixtureError("PNG chunk type is invalid.")
        end = position + size + 12
        if end > len(data) or size > MAX_PAGE_BYTES:
            raise FixtureError("PNG chunk exceeds its bounded page.")
        body = data[position + 8:position + 8 + size]
        crc = struct.unpack_from(">I", data, position + 8 + size)[0]
        if binascii.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise FixtureError("PNG chunk CRC mismatch.")
        if header is None:
            if kind != b"IHDR" or size != 13:
                raise FixtureError("PNG must start with a complete IHDR.")
            header = struct.unpack(">IIBBBBB", body)
            width, height, depth, color, compression, filtering, interlace = header
            depths = {0: {1, 2, 4, 8, 16}, 2: {8, 16}, 3: {1, 2, 4, 8}, 4: {8, 16}, 6: {8, 16}}
            if (not 1 <= width <= 2048 or not 1 <= height <= 2048 or width * height > 4_194_304
                    or color not in depths or depth not in depths[color]
                    or compression != 0 or filtering != 0 or interlace != 0):
                raise FixtureError("Fixture PNG must be bounded, legal, non-interlaced PNG.")
        elif kind == b"IHDR":
            raise FixtureError("PNG contains duplicate IHDR.")
        elif kind == b"PLTE":
            if palette or seen_idat or size == 0 or size % 3 or size > 768:
                raise FixtureError("PNG palette is invalid.")
            if header[3] in (0, 4) or (header[3] == 3 and size // 3 > (1 << header[2])):
                raise FixtureError("PNG palette conflicts with the color format.")
            palette = True
        elif kind == b"IDAT":
            if idat_closed:
                raise FixtureError("PNG IDAT chunks must be consecutive.")
            seen_idat = True
            compressed.extend(body)
        elif kind == b"IEND":
            if size or end != len(data) or not seen_idat:
                raise FixtureError("PNG IEND or image-data boundary is invalid.")
            ended = True
        elif not kind[0] & 0x20:
            raise FixtureError("Fixture PNG has an unsupported critical chunk.")
        if seen_idat and kind != b"IDAT":
            idat_closed = True
        position = end
    if not ended or header is None or (header[3] == 3 and not palette):
        raise FixtureError("PNG is missing image data, its palette, or IEND.")
    width, height, depth, color, *_ = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    stride = (width * channels * depth + 7) // 8 + 1
    expected = stride * height
    if expected > MAX_UNCOMPRESSED_BYTES:
        raise FixtureError("PNG decoded rows exceed the fixture bound.")
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(bytes(compressed), expected + 1)
    if len(decoded) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise FixtureError("PNG image data does not match its declared row size.")
    if any(decoded[row * stride] > 4 for row in range(height)):
        raise FixtureError("PNG contains an invalid row filter.")
    return {"width": width, "height": height, "bitDepth": depth, "colorType": color,
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def source_archive(text: str) -> tuple[Path, bytes, list[dict]]:
    path = Path(text)
    separators = lambda value: value.replace("\\", "/") if os.name == "nt" else value
    if (not path.is_absolute() or path.suffix.lower() != ".cbz"
            or separators(os.path.normpath(text)) != separators(text)):
        raise FixtureError("Each --cbz must be an explicit absolute clean .cbz path.")
    facts = no_link(path)
    if not stat.S_ISREG(facts.st_mode) or not 1 <= facts.st_size <= MAX_ARCHIVE_BYTES:
        raise FixtureError("Repository fixture CBZ must be a regular file of at most 4 MiB.")
    fixture_directory = None
    for parent in path.parents:
        if not stat.S_ISDIR(no_link(parent).st_mode):
            raise FixtureError("Source ancestors must be ordinary directories.")
        if parent.name == "comics-pack" and parent.parent.name == "fixtures" and parent.parent.parent.name == "tests":
            fixture_directory = parent
    if fixture_directory is None:
        raise FixtureError("Sources must come from an explicit tests/fixtures/comics-pack repository path.")
    path.resolve(strict=True).relative_to(fixture_directory.resolve(strict=True))
    data = path.read_bytes()
    if len(data) != facts.st_size or len(data) > MAX_ARCHIVE_BYTES:
        raise FixtureError("Source archive changed during reading.")
    pages, names, total = [], set(), 0
    with zipfile.ZipFile(io.BytesIO(data), "r") as archive:
        entries = archive.infolist()
        if not 1 <= len(entries) <= 64:
            raise FixtureError("Fixture ZIP must contain 1-64 entries.")
        for entry in entries:
            name = entry.filename
            parts = PurePosixPath(name).parts
            mode = entry.external_attr >> 16
            if (not name or name in names or "\\" in name or "\x00" in name or ":" in name
                    or name.startswith("/") or ".." in parts or stat.S_ISLNK(mode)
                    or entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)):
                raise FixtureError("ZIP contains a duplicate, unsafe, encrypted, linked or unsupported entry.")
            names.add(name)
            if entry.is_dir():
                continue
            total += entry.file_size
            if not 0 <= entry.file_size <= MAX_PAGE_BYTES or total > MAX_UNCOMPRESSED_BYTES:
                raise FixtureError("ZIP uncompressed bytes exceed the fixture bounds.")
            with archive.open(entry, "r") as stream:
                payload = stream.read(MAX_PAGE_BYTES + 1)
            if len(payload) != entry.file_size:
                raise FixtureError("ZIP entry length mismatch.")
            # archive.open/read checks the real ZIP CRC; png_facts also checks
            # every PNG chunk and the complete bounded image-data stream.
            if PurePosixPath(name).suffix.lower() == ".png":
                pages.append({"name": name, **png_facts(payload)})
            elif PurePosixPath(name).suffix.lower() in (".jpg", ".jpeg", ".webp", ".bmp", ".gif"):
                raise FixtureError("This narrow fixture copier accepts PNG-page CBZs only.")
        if not 1 <= len(pages) <= 16:
            raise FixtureError("Each fixture CBZ must contain 1-16 validated PNG pages.")
    # Repository fixtures use page-number names. loadIndex accepts a real
    # archive path plus a nonempty files list; the reader opens actual CBZ data.
    def natural_key(page: dict) -> tuple:
        return tuple((1, int(part), len(part)) if part.isdigit() else (0, part.casefold())
                     for part in re.split(r"([0-9]+)", page["name"]))
    pages.sort(key=natural_key)
    return path, data, pages


def hash10(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:10]


def safe_segment(text: str) -> str:
    # Same ASCII subset used by the harness's safeSegMirror; fixture IDs and
    # labels below are deliberately ASCII.
    result = "".join(char if char.isalnum() or char in "._- " else "_" for char in text).strip().rstrip(".")
    return (result or "item")[:80]


def write_new(root_text: str, path: Path, data: bytes) -> None:
    require_fixture_root(root_text)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    no_link(path)
    if os.utime in os.supports_follow_symlinks:
        os.utime(path, ns=(FIXED_MTIME_NS, FIXED_MTIME_NS), follow_symlinks=False)
    else:
        os.utime(path, ns=(FIXED_MTIME_NS, FIXED_MTIME_NS))


def seed(root_text: str, cbz_paths: list[str]) -> dict:
    root = require_fixture_root(root_text)
    if not 1 <= len(cbz_paths) <= 2:
        raise FixtureError("Provide exactly one or two explicit --cbz source paths.")
    comics = root / "comics"
    if os.path.lexists(comics):
        if not stat.S_ISDIR(no_link(comics).st_mode) or any(comics.iterdir()):
            raise FixtureError("No overwrite: comics must be absent or an existing completely empty directory.")
    sources = [source_archive(path) for path in cbz_paths]
    if len({path.name.casefold() for path, _, _ in sources}) != len(sources):
        raise FixtureError("The two repository fixtures must have different filenames.")
    index, copied = {}, []
    destination = comics / safe_segment(SERIES_ID)
    for order, (source, data, pages) in enumerate(sources, 1):
        issue_id = SERIES_ID + ":vol:" + hash10(source.name)
        label = f"Vol. {order} - Synthetic fixture"
        target = destination / (safe_segment(label) + "-" + hash10(issue_id) + ".cbz")
        index[issue_id] = {"seriesId": SERIES_ID, "seriesTitle": SERIES_TITLE, "label": label,
                           "packRole": "main", "packOrder": order, "archive": target.as_posix(),
                           "files": [page["name"] for page in pages], "bytes": len(data),
                           "addedAt": FIXED_MTIME_NS // 1_000_000}
        copied.append({"source": str(source), "destination": target.as_posix(), "issueId": issue_id,
                       "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "pages": pages})
    require_fixture_root(root_text)
    if not comics.exists():
        comics.mkdir(mode=0o700)
    elif any(comics.iterdir()):
        raise FixtureError("Comics destination changed before writing; nothing was overwritten.")
    destination.mkdir(mode=0o700)
    for (_, data, _), record in zip(sources, copied):
        write_new(root_text, Path(record["destination"]), data)
    index_bytes = (json.dumps(index, indent=2, ensure_ascii=True) + "\n").encode("utf-8")
    write_new(root_text, comics / "index.json", index_bytes)
    receipt = {"fixture": "collections-comic-persisted-index-v1", "seriesId": SERIES_ID,
               "seriesTitle": SERIES_TITLE, "method": "Explicitly seeded completed-index fixture; no ingest or download performed.",
               "nativeOwnerAcceptance": "Pending actual ComicDownloader.loadIndex/packVolumes and UI verification.",
               "indexSha256": hashlib.sha256(index_bytes).hexdigest(), "archives": copied}
    write_new(root_text, comics / "collections-fixture-receipt.json",
              (json.dumps(receipt, indent=2, ensure_ascii=True) + "\n").encode("utf-8"))
    write_new(root_text, comics / "COLLECTIONS-SYNTHETIC-FIXTURE.txt", (
        "SYNTHETIC COMIC PACK - REPOSITORY TEST FIXTURES\n"
        "Copied byte-for-byte from explicit tests/fixtures/comics-pack CBZs.\n"
        "A completed index was seeded using the existing native harness format.\n"
        "No download, ingest, external catalogue identity, or real comic title is claimed.\n"
        "Native loadIndex, packVolumes and UI acceptance still require actual app verification.\n"
    ).encode("ascii"))
    for (_, data, _), record in zip(sources, copied):
        if Path(record["destination"]).read_bytes() != data:
            raise FixtureError("Copied CBZ bytes changed; fixture verification failed.")
    require_fixture_root(root_text)
    print(f"seed_comic_pack: seeded {len(copied)} synthetic persisted main-volume rows; "
          "ZIP/PNG and copied-byte checks passed. Native owner/UI acceptance is pending. "
          f"Receipt: {comics / 'collections-fixture-receipt.json'}", file=sys.stderr)
    reference = {"world": "tankoban", "kind": "comic", "source": "comic-packs", "sourceId": SERIES_ID,
                 "editionId": "", "title": SERIES_TITLE, "cover": "", "creator": "", "year": "",
                 "route": "comic-pack-detail", "payload": {}}
    return {"collections": [{"name": "Comic pack fixture", "references": [reference]}]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="Existing marked/tagged absolute fixture app-data root.")
    parser.add_argument("--cbz", action="append", required=True,
                        help="Explicit absolute repository tests/fixtures/comics-pack CBZ; repeat for a second volume.")
    args = parser.parse_args()
    try:
        result = seed(args.root, args.cbz)
    except (FixtureError, OSError, ValueError, zipfile.BadZipFile, zlib.error, struct.error) as error:
        print(f"seed_comic_pack: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
