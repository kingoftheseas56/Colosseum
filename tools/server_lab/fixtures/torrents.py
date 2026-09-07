"""Deterministic, dependency-free torrent fixtures for the Server 1 lab."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Any

PIECE_LENGTH = 16 * 1024


def encode_bencode(value: Any) -> bytes:
    if isinstance(value, bytes):
        return str(len(value)).encode("ascii") + b":" + value
    if isinstance(value, str):
        return encode_bencode(value.encode("utf-8"))
    if isinstance(value, int):
        return b"i" + str(value).encode("ascii") + b"e"
    if isinstance(value, (list, tuple)):
        return b"l" + b"".join(encode_bencode(item) for item in value) + b"e"
    if isinstance(value, dict):
        items = sorted(((_as_bytes(key), item) for key, item in value.items()), key=lambda pair: pair[0])
        return b"d" + b"".join(encode_bencode(key) + encode_bencode(item) for key, item in items) + b"e"
    raise TypeError(f"unsupported bencode value: {type(value).__name__}")


def decode_bencode(data: bytes) -> Any:
    def parse(index: int) -> tuple[Any, int]:
        marker = data[index:index + 1]
        if marker == b"i":
            end = data.index(b"e", index)
            return int(data[index + 1:end]), end + 1
        if marker == b"l":
            index += 1
            result = []
            while data[index:index + 1] != b"e":
                value, index = parse(index)
                result.append(value)
            return result, index + 1
        if marker == b"d":
            index += 1
            result = {}
            while data[index:index + 1] != b"e":
                key, index = parse(index)
                value, index = parse(index)
                result[key] = value
            return result, index + 1
        colon = data.index(b":", index)
        length = int(data[index:colon])
        start = colon + 1
        return data[start:start + length], start + length

    result, end = parse(0)
    if end != len(data):
        raise ValueError("trailing bytes after bencode value")
    return result


def _as_bytes(value: Any) -> bytes:
    return value if isinstance(value, bytes) else str(value).encode("utf-8")


@dataclass(frozen=True)
class TorrentFile:
    path: tuple[str, ...]
    length: int
    offset: int


@dataclass(frozen=True)
class TorrentFixture:
    name: str
    payloads: dict[str, bytes]
    metainfo: bytes
    info_bytes: bytes
    infohash: str
    files: tuple[TorrentFile, ...]
    pieces: tuple[bytes, ...]

    @property
    def total_length(self) -> int:
        return sum(file.length for file in self.files)

    @property
    def bitfield_bytes(self) -> bytes:
        return bytes([0xFF] * ((len(self.pieces) + 7) // 8))

    def metainfo_again(self) -> bytes:
        return self.metainfo

    def block_size(self, piece: int, begin: int) -> int:
        if piece < 0 or piece >= len(self.pieces) or begin < 0 or begin >= len(self.pieces[piece]):
            raise ValueError("block outside piece")
        return min(PIECE_LENGTH, len(self.pieces[piece]) - begin)


def _make_fixture(name: str, payloads: dict[str, bytes]) -> TorrentFixture:
    files = []
    offset = 0
    for path, payload in payloads.items():
        files.append(TorrentFile(tuple(path.split("/")), len(payload), offset))
        offset += len(payload)
    joined = b"".join(payloads.values())
    pieces = tuple(joined[start:start + PIECE_LENGTH] for start in range(0, len(joined), PIECE_LENGTH))
    info: dict[str, Any] = {"name": name, "piece length": PIECE_LENGTH, "pieces": b"".join(hashlib.sha1(piece).digest() for piece in pieces)}
    if len(files) == 1:
        info["length"] = files[0].length
    else:
        info["files"] = [{"length": file.length, "path": list(file.path)} for file in files]
    info_bytes = encode_bencode(info)
    metainfo = encode_bencode({"info": info})
    return TorrentFixture(name, payloads, metainfo, info_bytes, hashlib.sha1(info_bytes).hexdigest(), tuple(files), pieces)


def build_single_file_torrent() -> TorrentFixture:
    return _make_fixture("p06-single", {"single.bin": b"P06-single\n" * 2048})


def build_multi_file_torrent() -> TorrentFixture:
    return _make_fixture("p06-multi", {
        "alpha.bin": b"A" * 7000,
        "nested/beta.bin": b"B" * 14000,
        "gamma.bin": b"C" * 12000,
    })


def validate_torrent_fixture(fixture: TorrentFixture) -> bool:
    if hashlib.sha1(fixture.info_bytes).hexdigest() != fixture.infohash:
        return False
    payload = b"".join(fixture.payloads[file.path[-1] if len(file.path) == 1 else "/".join(file.path)] for file in fixture.files)
    expected = tuple(payload[offset:offset + PIECE_LENGTH] for offset in range(0, len(payload), PIECE_LENGTH))
    return expected == fixture.pieces and tuple(hashlib.sha1(piece).digest() for piece in expected) == tuple(
        decode_bencode(fixture.metainfo)[b"info"][b"pieces"][offset:offset + 20] for offset in range(0, len(expected) * 20, 20)
    )
