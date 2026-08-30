from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import uuid
from dataclasses import dataclass
from typing import Any

from function0008_loopback_fixture import (
    EXPECTED_REQUEST_HEADERS,
    NEGATIVE_HEADER,
    Function0008Fixture,
    VIDEO_ID,
    _urlopen,
)

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
SCENARIOS = HERE / "lanista_scenarios"
ALLOWED_COMMANDS = {"ping", "get-state", "qml-get", "ui-click", "ui-wait-for", "grab"}
SESSION_RE = re.compile(r"^SESSION\s+(\S+)\s+pipe=(\S+)", re.MULTILINE)


class ProofError(RuntimeError):
    pass


@dataclass(frozen=True)
class Paths:
    tag: str
    appdata_root: pathlib.Path
    fixture_root: pathlib.Path
    output_path: pathlib.Path
    negative_log: pathlib.Path
    runtime_log: pathlib.Path
    driver_manifest: pathlib.Path
    lanista_stdout: pathlib.Path
    lanista_stderr: pathlib.Path


def expected_appdata_root(tag: str) -> pathlib.Path:
    roaming = os.environ.get("APPDATA")
    if not roaming:
        raise ProofError("APPDATA is unavailable; cannot derive the tagged Windows AppData seed root")
    return pathlib.Path(roaming) / "Brotherhood" / f"Colosseum-dltest-{tag}"


def make_paths(journey: str) -> Paths:
    stem = "download" if journey == "direct-download" else "arriving"
    tag = f"function0008-{stem}-{uuid.uuid4().hex[:8]}"
    root = expected_appdata_root(tag)
    fixture_root = root / "function0008-fixture"
    return Paths(
        tag, root, fixture_root, fixture_root / "video.avi",
        fixture_root / "negative-control.jsonl", fixture_root / "requests.jsonl",
        fixture_root / "journey-manifest.json", fixture_root / "lanista-driver.stdout.txt",
        fixture_root / "lanista-driver.stderr.txt",
    )


def write_jsonl(path: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        for row in rows:
            handle.write(json.dumps(row, sort_keys=True) + "\n")


def validate_scenario(path: pathlib.Path) -> None:
    doc = json.loads(path.read_text(encoding="utf-8"))
    steps = doc.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ProofError(f"scenario has no steps: {path}")
    for index, step in enumerate(steps, 1):
        cmd = step.get("cmd")
        if cmd not in ALLOWED_COMMANDS:
            raise ProofError(f"{path.name} step {index} uses non-ledger command {cmd!r}")


def negative_control(fixture: Function0008Fixture, log_path: pathlib.Path) -> None:
    fixture.hold_download = False
    fixture.set_negative_control(True)
    status, _ = _urlopen(fixture.video_url, dict(EXPECTED_REQUEST_HEADERS))
    if status != 403:
        raise ProofError(f"negative-control expected 403, got {status}")
    fixture.set_negative_control(False)
    restored = dict(EXPECTED_REQUEST_HEADERS)
    restored["Range"] = "bytes=0-31"
    status, body = _urlopen(fixture.video_url, restored)
    if status != 206 or not body.startswith(b"RIFF"):
        raise ProofError(f"negative-control restore failed: status={status}, RIFF={body[:4]!r}")
    write_jsonl(log_path, fixture.records())
    fixture.reset_runtime_records()


def build_seed(seed_root: pathlib.Path, fixture: Function0008Fixture, paths: Paths) -> None:
    extensions_dir = seed_root / "extensions"
    videos_dir = seed_root / "videos"
    extensions_dir.mkdir(parents=True, exist_ok=True)
    videos_dir.mkdir(parents=True, exist_ok=True)
    installed = {
        "v": 1,
        "defaultsVersion": 10,
        "extensions": [{
            "id": "org.preflight.function0008.fixture",
            "transportUrl": fixture.base_url + "/manifest.json",
            "installedAt": 1,
            "enabled": True,
            "core": False,
            "manifest": fixture.manifest(),
        }],
    }
    queue = [{
        "request": {
            "id": VIDEO_ID,
            "kind": "movie",
            "title": "Function 0008 Direct Runtime",
            "art": "",
        },
        "state": "queued",
        "error": "",
        "outputPath": str(paths.output_path),
    }]
    (extensions_dir / "installed.json").write_text(
        json.dumps(installed, indent=2), encoding="utf-8"
    )
    (videos_dir / "queue.json").write_text(
        json.dumps(queue, indent=2), encoding="utf-8"
    )


def expected_headers_present(row: dict[str, Any]) -> bool:
    headers = row.get("headers", {})
    return all(headers.get(name) == value for name, value in EXPECTED_REQUEST_HEADERS.items()) \
        and not headers.get(NEGATIVE_HEADER)


def runtime_wire_verdict(rows: list[dict[str, Any]], *, arriving: bool) -> tuple[bool, list[str]]:
    failures: list[str] = []
    media = [row for row in rows if row.get("path") == "/video.mp4"]
    if any(row.get("status") == 403 for row in media):
        failures.append("runtime media log contains 403")
    accepted = [row for row in media if row.get("accepted") and row.get("status") in (200, 206)]
    if not accepted:
        failures.append("no accepted 2xx media request")
    if accepted and not all(expected_headers_present(row) for row in accepted):
        failures.append("accepted media request is missing required headers or carries the sentinel")
    if arriving and not any(row.get("role") == "playback" for row in accepted):
        failures.append("no second header-valid Player media request")
    return not failures, failures


def session_command(lanista: pathlib.Path, exe: pathlib.Path, qml: str,
                    scenario: pathlib.Path, seed: pathlib.Path, tag: str,
                    ready_ms: int) -> list[str]:
    return [
        str(lanista), "session", "run", str(scenario),
        "--exe", str(exe),
        "--qml", qml,
        "--tag", tag,
        "--drive",
        "--seed", str(seed),
        "--ready-ms", str(ready_ms),
        "--keep-going",
        "--timings",
    ]


def parse_session(stdout: str) -> tuple[str, str]:
    match = SESSION_RE.search(stdout)
    if not match:
        return "", ""
    return match.group(1), match.group(2)


def session_artifacts(session_id: str) -> tuple[pathlib.Path | None, list[str], dict[str, Any]]:
    if not session_id:
        return None, [], {}
    root = REPO / "artifacts" / "lanista-sessions" / session_id
    if not root.is_dir():
        return root, [], {}
    names = sorted(path.name for path in root.iterdir() if path.is_file())
    manifest_path = root / "session.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    return root, names, manifest


def run_process(command: list[str], *, arriving: bool,
                fixture: Function0008Fixture) -> tuple[int, str, str, bool]:
    if not arriving:
        completed = subprocess.run(
            command, cwd=REPO, text=True, capture_output=True,
            timeout=150, check=False,
        )
        return completed.returncode, completed.stdout, completed.stderr, True

    process = subprocess.Popen(
        command, cwd=REPO, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    player_request_before_release = fixture.playback_request_seen.wait(timeout=45)
    fixture.release_download.set()
    try:
        stdout, stderr = process.communicate(timeout=150)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate(timeout=10)
        return 124, stdout, stderr + "\nDRIVER: session timed out after cleanup release\n", player_request_before_release
    return process.returncode, stdout, stderr, player_request_before_release


def known_arriving_blocker() -> str:
    return (
        "dd576634 native/engine/LocalDownloads.cpp projects DownloadStore jobs to DownloadsPage "
        "without headers or partPath; routeArrivingPlay(job) therefore cannot receive the "
        "DownloadStore provenance/disk-first fields. Arc 33 does not repair this by design."
    )


def run_journey(journey: str, lanista: pathlib.Path, exe: pathlib.Path,
                qml: str, ready_ms: int) -> bool:
    arriving = journey == "arriving-play"
    scenario = SCENARIOS / (
        "function0008_arriving_play.json" if arriving else "function0008_direct_download.json"
    )
    validate_scenario(scenario)
    paths = make_paths(journey)
    if paths.appdata_root.exists():
        raise ProofError(f"unique tagged root already exists: {paths.appdata_root}")
    paths.fixture_root.mkdir(parents=True, exist_ok=False)

    failures: list[str] = []
    session_id = ""
    pipe = ""
    session_root: pathlib.Path | None = None
    artifact_names: list[str] = []
    session_manifest: dict[str, Any] = {}

    with Function0008Fixture() as fixture:
        negative_control(fixture, paths.negative_log)
        fixture.hold_download = arriving
        with tempfile.TemporaryDirectory(prefix=f"f0008-{journey}-seed-") as seed_name:
            seed = pathlib.Path(seed_name)
            build_seed(seed, fixture, paths)
            command = session_command(lanista, exe, qml, scenario, seed, paths.tag, ready_ms)
            code, stdout, stderr, player_before_release = run_process(
                command, arriving=arriving, fixture=fixture
            )
        paths.lanista_stdout.write_text(stdout, encoding="utf-8")
        paths.lanista_stderr.write_text(stderr, encoding="utf-8")
        rows = fixture.records()
        write_jsonl(paths.runtime_log, rows)
        session_id, pipe = parse_session(stdout)
        session_root, artifact_names, session_manifest = session_artifacts(session_id)

        wire_ok, wire_failures = runtime_wire_verdict(rows, arriving=arriving)
        failures.extend(wire_failures)
        stream_path = f"/stream/movie/{VIDEO_ID}.json"
        if not any(row.get("path") == stream_path and row.get("status") == 200 for row in rows):
            failures.append("local Stremio stream endpoint was not observed")
        if code != 0:
            failures.append(f"Lanista scenario exited {code}")
        if arriving and not player_before_release:
            failures.append("no header-valid Player request was observed before cleanup released the held download")
        if not wire_ok:
            pass

        manifest_root = str(session_manifest.get("appDataRoot", ""))
        manifest_cache = str(session_manifest.get("cacheRoot", ""))
        if paths.tag not in manifest_root or paths.tag not in manifest_cache:
            failures.append("Lanista session manifest does not prove tagged AppData/cache isolation")
        if session_manifest and session_manifest.get("drive") is not True:
            failures.append("Lanista session manifest does not record drive=true")
        if pipe == "ColosseumLanista":
            failures.append("session used the forbidden daily-app pipe")
        if not paths.output_path.is_file():
            failures.append(f"download output did not land inside tagged root: {paths.output_path}")

    object_reads = {
        "downloadsPage": ["visible", "liveJobCount", "attentionCount", "totalsMap"],
    }
    if arriving:
        object_reads[f"downloadsPlayArriving_{VIDEO_ID}"] = ["visible", "diskFirstReady"]
        object_reads["player"] = [
            "playerReady", "sourceIdentity", "mediaTransport", "currentPlaybackUrl",
        ]

    result = {
        "schema": "preflight.function0008.lanista-runtime.v1",
        "journey": journey,
        "verdict": "PASS" if not failures else "FAIL",
        "tag": paths.tag,
        "pipe": pipe,
        "expectedAppDataRoot": str(paths.appdata_root),
        "fixtureOutputPath": str(paths.output_path),
        "negativeControlLog": str(paths.negative_log),
        "runtimeRequestLog": str(paths.runtime_log),
        "lanistaDriverStdout": str(paths.lanista_stdout),
        "lanistaDriverStderr": str(paths.lanista_stderr),
        "lanistaSessionId": session_id,
        "lanistaSessionDir": str(session_root) if session_root else "",
        "lanistaArtifacts": artifact_names,
        "objectReads": object_reads,
        "failures": failures,
        "integratedRuntimeStillOwnedByClaude": True,
    }
    if arriving:
        result["currentDd576634Prerequisite"] = known_arriving_blocker()
    paths.driver_manifest.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({
        "journey": journey,
        "verdict": result["verdict"],
        "tag": paths.tag,
        "manifest": str(paths.driver_manifest),
        "failures": failures,
    }, indent=2))
    return not failures


def check_only() -> int:
    for name in ("function0008_direct_download.json", "function0008_arriving_play.json"):
        validate_scenario(SCENARIOS / name)
    with Function0008Fixture() as fixture:
        temp = pathlib.Path(tempfile.mkdtemp(prefix="f0008-check-"))
        try:
            negative_control(fixture, temp / "negative-control.jsonl")
        finally:
            shutil.rmtree(temp, ignore_errors=True)
    print("FUNCTION0008_RUNTIME_DRIVER_CHECK_OK")
    return 0


def require_file(path: pathlib.Path, label: str) -> None:
    if not path.is_file():
        raise ProofError(f"{label} not found: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Function 0008 assembled-app Lanista runtime proof")
    parser.add_argument("--journey", choices=["direct-download", "arriving-play", "all"], default="all")
    parser.add_argument("--lanista", default="native/build-msvc/lanista.exe")
    parser.add_argument("--exe", default="native/build-msvc/colosseum.exe")
    parser.add_argument("--qml", default="qml/Main.qml")
    parser.add_argument("--ready-ms", type=int, default=60000)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()

    if args.check_only:
        return check_only()

    lanista = pathlib.Path(args.lanista)
    if not lanista.is_absolute():
        lanista = REPO / lanista
    exe = pathlib.Path(args.exe)
    if not exe.is_absolute():
        exe = REPO / exe
    qml_path = pathlib.Path(args.qml)
    require_file(lanista, "Lanista executable (build with: cmake --build native/build-msvc --target lanista)")
    require_file(exe, "Colosseum executable")
    require_file(qml_path if qml_path.is_absolute() else REPO / qml_path, "Main QML")

    journeys = [args.journey] if args.journey != "all" else ["direct-download", "arriving-play"]
    ok = True
    for journey in journeys:
        ok = run_journey(journey, lanista, exe, args.qml, args.ready_ms) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProofError as error:
        print(f"FUNCTION0008_PROOF_ERROR: {error}")
        raise SystemExit(2)
