"""Run the archived Colosseum Server 0.1 comparison target in isolation."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
from typing import Iterable, Sequence


SOURCE_REVISION = "a3fcaa96ec2650014e1dd94f603d76b2b1e48387"
INTEGRATION_BASE = "1c08a0734acbe6f46b67630f3a735e24df190db1"
PLAYBACK_TEST = "runtime_mpv_playback_test"
EXPLICIT_INPUTS = (
    "native/torrent/engine/TorrentEngine.cpp",
    "native/torrent/engine/TorrentEngine.h",
    "native/torrent/engine/DebugLogBuffer.cpp",
    "native/torrent/engine/DebugLogBuffer.h",
    "native/player/mpvitem.cpp",
    "native/player/mpvitem.h",
    "native/player/mpvproperties.h",
    "native/player/http_header_fields.h",
    "native/player/streamserver.cpp",
    "native/player/streamserver.h",
    "native/third_party/miniz/miniz.c",
    "native/third_party/miniz/miniz.h",
    "tests/fixtures/vault/media/tiny.mp4",
)


def source_receipt(source_revision: str, files: Iterable[tuple[str, str, int, str]]) -> dict:
    """Keep source revision, Git object IDs, byte counts, and SHA-256 separate."""

    return {
        "source_revision": source_revision,
        "files": [
            {"path": path, "git_object": git_object, "bytes": size, "sha256": sha256}
            for path, git_object, size, sha256 in files
        ],
    }


def check_required_inputs(root: Path, paths: Iterable[str]) -> dict:
    """Reject a reduced source snapshot and return deterministic path evidence."""

    normalized = sorted({path.replace("\\", "/") for path in paths})
    missing = [path for path in normalized if not (root / Path(path)).is_file()]
    return {
        "complete": not missing,
        "required_count": len(normalized),
        "missing": missing,
        "present_count": len(normalized) - len(missing),
    }


def classify_ctest_listing(output: str) -> dict:
    configured = re.search(rf"(?m)\b{re.escape(PLAYBACK_TEST)}\b", output) is not None
    return {
        "runtime_mpv_playback_test_configured": configured,
        "playback_qualified": False,
        "status": "CONFIGURED" if configured else "BASELINE_UNAVAILABLE",
    }


def _run(command: Sequence[str], cwd: Path, output_path: Path) -> int:
    command_line = "$ " + subprocess.list2cmdline(list(command)) + "\n"
    try:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError as error:
        output_path.write_text(
            command_line + f"TOOL_NOT_FOUND: {error}\nEXIT=127\n",
            encoding="utf-8",
        )
        return 127
    output_path.write_text(
        command_line + completed.stdout + completed.stderr + f"EXIT={completed.returncode}\n",
        encoding="utf-8",
    )
    return completed.returncode


def _git(repo_root: Path, args: Sequence[str]) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "git command failed")
    return completed.stdout.strip()


def _tracked_paths(repo_root: Path, revision: str, prefix: str) -> list[str]:
    output = _git(repo_root, ["ls-tree", "-r", "--name-only", revision, "--", prefix])
    return [line for line in output.splitlines() if line]


def closure_paths(repo_root: Path, revision: str) -> list[str]:
    paths = set(_tracked_paths(repo_root, revision, "native/colosseum_server"))
    paths.update(EXPLICIT_INPUTS)
    return sorted(paths)


def _blob_id(repo_root: Path, revision: str, path: str) -> str:
    return _git(repo_root, ["rev-parse", f"{revision}:{path}"])


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _extract(repo_root: Path, revision: str, paths: Sequence[str], destination: Path) -> None:
    archive = destination.parent / "source.tar"
    completed = subprocess.run(
        ["git", "-C", str(repo_root), "archive", "--format=tar", "--output", str(archive), revision, *paths],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "git archive failed")
    with tarfile.open(archive) as stream:
        stream.extractall(destination)


def _write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_evidence(artifact_dir: Path, result: dict) -> None:
    result["artifacts"].update(
        {
            "case_results": "CASE-RESULTS.json",
            "packet_receipt": "PACKET-RECEIPT.json",
            "wiring_request": "WIRING-REQUEST.json",
        }
    )
    _write_json(artifact_dir / "RESULT.json", result)
    _write_json(
        artifact_dir / "CASE-RESULTS.json",
        {
            "schema": "colosseum-server1-case-results/v1",
            "worker_id": result["worker_id"],
            "parent_packet": result["parent_packet"],
            "status": result["status"],
            "cases": result["cases"],
            "ctest": result.get("ctest"),
            "build": result.get("build"),
            "commands": result["commands"],
            "errors": result["errors"],
        },
    )
    _write_json(
        artifact_dir / "PACKET-RECEIPT.json",
        {
            "schema": "colosseum-server1-packet-receipt/v1",
            "worker_id": result["worker_id"],
            "parent_packet": result["parent_packet"],
            "integration_base": INTEGRATION_BASE,
            "source_revision": result["source_revision"],
            "source_tree": result.get("source_tree"),
            "status": result["status"],
            "cases": result["cases"],
            "commands": result["commands"],
            "artifacts": result["artifacts"],
            "interface_produced": "OptionalServer01Baseline",
            "interface_changes": "none",
            "push_status": "not pushed; Sol review required",
        },
    )
    _write_json(
        artifact_dir / "WIRING-REQUEST.json",
        {
            "schema": "colosseum-server1-wiring-request/v1",
            "worker_id": result["worker_id"],
            "parent_packet": result["parent_packet"],
            "wiring_needed": False,
            "shared_files_requested": [],
            "interfaces_changed": [],
            "reason": "P01B emits packet-local OptionalServer01Baseline evidence; no Server 1.0 source, build, test, runtime, or CI wiring is requested.",
        },
    )


def run(repo_root: Path, artifact_dir: Path, revision: str = SOURCE_REVISION) -> int:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "schema": "colosseum-server01-baseline-result/v1",
        "worker_id": "P01B-A",
        "parent_packet": "P01B",
        "source_revision": revision,
        "status": "BASELINE_UNAVAILABLE",
        "commands": [],
        "cases": {},
        "artifacts": {},
        "errors": [],
    }

    try:
        resolved = _git(repo_root, ["rev-parse", "--verify", f"{revision}^{{commit}}"])
        if resolved != revision:
            raise RuntimeError(f"revision resolved to {resolved}, expected {revision}")
        result["source_tree"] = _git(repo_root, ["rev-parse", f"{revision}^{{tree}}"])
        paths = closure_paths(repo_root, revision)
        _write_json(artifact_dir / "CLOSURE-PATHS.json", {"paths": paths, "count": len(paths)})
        result["artifacts"]["closure_paths"] = "CLOSURE-PATHS.json"
    except (RuntimeError, OSError) as error:
        result["errors"].append(str(error))
        result["cases"]["P01B-01"] = "BASELINE_UNAVAILABLE"
        result["cases"]["P01B-02"] = "BASELINE_UNAVAILABLE"
        result["cases"]["P01B-03"] = "BASELINE_UNAVAILABLE"
        _write_evidence(artifact_dir, result)
        return 2

    with tempfile.TemporaryDirectory(prefix="colosseum-server01-") as temporary:
        staging = Path(temporary) / "source"
        staging.mkdir()
        try:
            _extract(repo_root, revision, paths, staging)
            required = check_required_inputs(staging, paths)
            _write_json(artifact_dir / "REQUIRED-INPUTS.json", required)
            result["artifacts"]["required_inputs"] = "REQUIRED-INPUTS.json"
            if not required["complete"]:
                result["errors"].append("missing archived inputs: " + ", ".join(required["missing"]))
                result["cases"]["P01B-01"] = "BASELINE_UNAVAILABLE"
                result["cases"]["P01B-02"] = "BASELINE_UNAVAILABLE"
                result["cases"]["P01B-03"] = "BASELINE_UNAVAILABLE"
                _write_evidence(artifact_dir, result)
                return 2

            recovered = []
            for path in paths:
                recovered_path = staging / Path(path)
                recovered.append((path, _blob_id(repo_root, revision, path), recovered_path.stat().st_size, _sha256(recovered_path)))
            receipt = source_receipt(revision, recovered)
            _write_json(artifact_dir / "SOURCE-RECEIPT.json", receipt)
            result["artifacts"]["source_receipt"] = "SOURCE-RECEIPT.json"
            result["cases"]["P01B-01"] = "PASS"
            result["cases"]["P01B-02"] = "PASS"

            build = Path(temporary) / "build"
            source = staging / "native" / "colosseum_server" / "tests"
            configure_command = ["cmake", "-S", str(source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"]
            if shutil.which("ninja"):
                configure_command[1:1] = ["-G", "Ninja"]
            result["commands"].append({"name": "configure", "argv": configure_command})
            configure_exit = _run(configure_command, repo_root, artifact_dir / "CONFIGURE.txt")
            result["commands"][-1]["exit"] = configure_exit
            if configure_exit == 127:
                result["errors"].append("cmake executable was not found on PATH; see CONFIGURE.txt")
            elif configure_exit != 0:
                result["errors"].append(f"archived baseline configure failed with exit {configure_exit}; see CONFIGURE.txt")

            ctest_listing = artifact_dir / "CTEST-N.txt"
            ctest_exit = None
            listing = {"runtime_mpv_playback_test_configured": False, "playback_qualified": False, "status": "BASELINE_UNAVAILABLE"}
            build_exit = None
            runtime_exit = None
            if configure_exit == 0:
                build_command = ["cmake", "--build", str(build), "--config", "Release", "--parallel"]
                result["commands"].append({"name": "build", "argv": build_command})
                build_exit = _run(build_command, repo_root, artifact_dir / "BUILD.txt")
                result["commands"][-1]["exit"] = build_exit

                ctest_command = ["ctest", "--test-dir", str(build), "-N", "-C", "Release"]
                result["commands"].append({"name": "ctest -N", "argv": ctest_command})
                ctest_exit = _run(ctest_command, repo_root, ctest_listing)
                result["commands"][-1]["exit"] = ctest_exit
                listing = classify_ctest_listing(ctest_listing.read_text(encoding="utf-8"))

                if listing["runtime_mpv_playback_test_configured"]:
                    runtime_command = [
                        "ctest", "--test-dir", str(build), "-C", "Release", "-R", f"^{PLAYBACK_TEST}$", "--output-on-failure"
                    ]
                    result["commands"].append({"name": PLAYBACK_TEST, "argv": runtime_command})
                    runtime_exit = _run(runtime_command, repo_root, artifact_dir / "RUNTIME-MPV.txt")
                    result["commands"][-1]["exit"] = runtime_exit
                    listing["playback_qualified"] = runtime_exit == 0
                    listing["status"] = "PASS" if runtime_exit == 0 else "BASELINE_UNAVAILABLE"
                else:
                    (artifact_dir / "RUNTIME-MPV.txt").write_text(
                        "NOT_RUN: runtime_mpv_playback_test was not configured; omission is not a pass.\n",
                        encoding="utf-8",
                    )
            else:
                (artifact_dir / "BUILD.txt").write_text("NOT_RUN: configure failed.\n", encoding="utf-8")
                (artifact_dir / "CTEST-N.txt").write_text("NOT_RUN: configure failed.\n", encoding="utf-8")
                (artifact_dir / "RUNTIME-MPV.txt").write_text("NOT_RUN: configure failed.\n", encoding="utf-8")

            result["ctest"] = listing
            result["cases"]["P01B-03"] = "PASS" if listing["playback_qualified"] else "BASELINE_UNAVAILABLE"
            result["status"] = "AVAILABLE" if all(value == "PASS" for value in result["cases"].values()) else "BASELINE_UNAVAILABLE"
            result["build"] = {"configure_exit": configure_exit, "build_exit": build_exit, "ctest_n_exit": ctest_exit, "runtime_exit": runtime_exit}
        except (RuntimeError, OSError, tarfile.TarError) as error:
            result["errors"].append(str(error))
            result["cases"]["P01B-03"] = "BASELINE_UNAVAILABLE"

    _write_evidence(artifact_dir, result)
    return 0 if result["status"] == "AVAILABLE" else 2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--source-revision", default=SOURCE_REVISION)
    arguments = parser.parse_args()
    return run(arguments.repo_root.resolve(), arguments.artifact_dir.resolve(), arguments.source_revision)


if __name__ == "__main__":
    raise SystemExit(main())
