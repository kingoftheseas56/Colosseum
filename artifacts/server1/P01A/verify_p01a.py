"""Fail-closed validator for the Server 1.0 P01A native substrate lock."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


class ValidationError(ValueError):
    pass


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"invalid JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValidationError(f"JSON root is not an object: {path}")
    return value


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def validate_hashes(root: Path, records: list[dict]) -> None:
    for record in records:
        path = root / Path(record["path"])
        _require(path.is_file(), f"missing locked file: {record['path']}")
        data = path.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        _require(
            len(data) == record["bytes"] and actual == record["sha256"],
            f"locked file mismatch: {record['path']} bytes={len(data)}/{record['bytes']} sha={actual}/{record['sha256']}",
        )


def validate_libtorrent(lock: dict) -> None:
    identity = lock["libtorrent"]
    header = identity["header_declared"]
    archive = identity["archive_identity"]
    linked = identity["linked_runtime"]
    patch_provenance = identity["local_patch_provenance"]
    abi = identity["abi"]
    _require(header["major"] == 2, "libtorrent major version is not 2")
    _require(header["minor"] == 0, "libtorrent minor version is not 0")
    _require(not header["version"].startswith("2.1"), "libtorrent 2.1 is forbidden")
    _require(linked["version"] == header["version"], "header/library version mismatch")
    _require("revision" not in linked, "linked-runtime revision is unsupported by the probe")
    _require(archive["sha256"] == identity["archive_sha256"], "archive identity mismatch")
    _require(patch_provenance["state"] == "unknown", "local patch provenance must remain unknown without a receipt")
    _require(abi["architecture"] == "x64", "ABI architecture mismatch")
    _require(abi["crt"] == "/MD", "ABI CRT mismatch")
    _require(abi["language_standard"] == "C++17", "ABI language-standard mismatch")
    _require(linked["probe_exit"] == 0, "linked runtime probe failed")
    _require("IDENTITY_MISMATCH" not in linked["probe_output"], "runtime identity probe reported mismatch")


def validate_consumer(root: Path, lock: dict) -> None:
    consumer = lock["consumer_probe"]
    duplicate = root / "artifacts/server1/P01A/consumer/CMakeLists.txt"
    _require(not duplicate.exists(), "duplicate colosseum_libtorrent definition remains")
    _require(consumer["source"] == "native/CMakeLists.txt", "consumer did not configure native authority")
    _require(consumer["target"] == "torrent_engine_link_harness", "wrong native consumer target")
    _require(consumer["dependency_target"] == "colosseum_libtorrent", "native consumer did not link shared target")
    _require(consumer["configure_valid"]["exit"] == 0, "valid consumer configure failed")
    _require(consumer["build"]["exit"] == 0, "consumer build failed")
    _require(consumer["run"]["exit"] == 0, "consumer runtime failed")
    _require(consumer["configure_without_libtorrent"]["exit"] != 0, "missing libtorrent did not fail closed")
    _require("libtorrent" in consumer["configure_without_libtorrent"]["output"].lower(), "missing dependency failure was not visible")


def validate(root: Path) -> tuple[dict, dict]:
    lock = _read_json(root / "docs/server1/DEPENDENCY-LOCK.json")
    substrate = _read_json(root / "docs/server1/NATIVE-SUBSTRATE.json")
    _require(lock["schema"] == "colosseum-server1-dependency-lock/v1", "wrong dependency lock schema")
    _require(substrate["schema"] == "colosseum-server1-native-substrate/v1", "wrong substrate schema")
    validate_hashes(root, lock["locked_files"])
    validate_libtorrent(lock)
    validate_consumer(root, lock)
    compiler = substrate["compiler"]
    _require(compiler["language_standard"] == "C++17", "wrong language standard")
    _require(compiler["crt"] == "/MD", "wrong CRT mode")
    _require(substrate["linux_qualification"]["state"] == "planned", "Linux execution was falsely claimed")
    return lock, substrate


if __name__ == "__main__":
    repository = Path(__file__).resolve().parents[3]
    try:
        dependency_lock, native_substrate = validate(repository)
    except (KeyError, ValidationError) as error:
        print(f"REJECTED: {error}")
        raise SystemExit(1)
    print(
        "ACCEPTED: "
        f"libtorrent={dependency_lock['libtorrent']['header_declared']['version']} "
        f"target={dependency_lock['native_authority']['target']} "
        f"crt={native_substrate['compiler']['crt']}"
    )
