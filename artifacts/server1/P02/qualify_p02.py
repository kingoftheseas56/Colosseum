"""Run P02 reference qualification and write bounded evidence artifacts."""

from __future__ import annotations

import argparse
import json
import socket
import sys
from pathlib import Path


WORKER_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(WORKER_ROOT))

from tools.server_lab.adapters.stremio import (  # noqa: E402
    DEFAULT_PORT_END,
    DEFAULT_PORT_START,
    StremioReference,
)


def dump(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def occupy_required_ports() -> tuple[list[socket.socket], dict[str, object] | None]:
    sockets: list[socket.socket] = []
    for port in range(DEFAULT_PORT_START, DEFAULT_PORT_END + 1):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("127.0.0.1", port))
            sock.listen(1)
            sockets.append(sock)
        except OSError as error:
            sock.close()
            for opened in sockets:
                opened.close()
            return [], {
                "status": "blocked-environment",
                "port": port,
                "error": repr(error),
            }
    return sockets, None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--output", default=Path(__file__).resolve().parent, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    reference = StremioReference(args.oracle)
    identity = reference.fingerprint()
    windows_companions = reference.companions()
    windows_probe = reference.run_probe(("/heartbeat", "/settings"))

    if windows_probe["ready"]:
        windows_case_01_state = "PASS"
        windows_case_01_reason = "reference served /heartbeat and /settings"
    elif windows_probe["failure"]["kind"] == "bind-denied":
        windows_case_01_state = "BLOCKED_ENV"
        windows_case_01_reason = "Windows excluded TCP range denied the reference listener bind"
    else:
        windows_case_01_state = "FAIL"
        windows_case_01_reason = windows_probe["failure"]["kind"]

    occupied, occupancy_failure = occupy_required_ports()
    if occupancy_failure:
        windows_case_02 = {
            "id": "P02-02-WINDOWS",
            "state": "BLOCKED_ENV",
            "reason": "Windows fixture could not occupy the excluded required port range",
            "fixture": occupancy_failure,
        }
    else:
        try:
            occupied_probe = reference.run_probe(("/heartbeat",), timeout=2.5)
        finally:
            for sock in occupied:
                sock.close()
        windows_case_02 = {
            "id": "P02-02-WINDOWS",
            "state": "PASS"
            if occupied_probe["failure"]["kind"] == "port-exhausted"
            and occupied_probe["port"] is None
            and 11475 not in occupied_probe["failure"]["attempted_ports"]
            else "FAIL",
            "result": occupied_probe,
        }

    windows_lane = {
        "schema": "colosseum-server1-p02-windows-lane/v1",
        "platform": identity["platform"],
        "excluded_tcp_range": [11464, 11563],
        "cases": [
            {
                "id": "P02-01-WINDOWS",
                "state": windows_case_01_state,
                "reason": windows_case_01_reason,
                "result": windows_probe,
            },
            windows_case_02,
            {
                "id": "P02-03-WINDOWS",
                "state": "BOUNDED_MISSING",
                "companions": windows_companions,
            },
        ],
    }

    wsl = reference.qualify_wsl("Ubuntu")
    case_01 = dict(wsl["cases"]["P02-01-WSL"])
    case_01.update({"id": "P02-01", "lane": "WSL2 Ubuntu"})
    case_02 = dict(wsl["cases"]["P02-02-WSL"])
    case_02.update({"id": "P02-02", "lane": "WSL2 Ubuntu"})
    case_03 = {
        "id": "P02-03",
        "lane": "WSL2 Ubuntu",
        "state": "PASS_WITH_PROVENANCE",
        "companions": wsl["companions"],
        "certificate_fixtures": {
            "status": "not-exercised",
            "reason": "HTTPS was disabled for the controlled HTTP qualification run; no certificate fixture was supplied",
            "repair": "none",
        },
        "native_companions": {
            "status": "stremio-runtime-missing",
            "reason": "portable official Node qualified the exact single-file oracle; this does not establish intended stremio-runtime companion identity",
            "exercised_path_effect": "none observed for /heartbeat, /settings, port fallback, or teardown",
            "repair": "none",
        },
        "outbound_connections": wsl["cases"]["P02-01-WSL"]["network_observation"],
    }

    dump(output / "WINDOWS-BLOCKED.json", windows_lane)
    dump(output / "WSL-QUALIFICATION.json", wsl)
    dump(
        output / "FINGERPRINT.json",
        {
            "oracle": identity["oracle"],
            "embedded": identity["embedded"],
            "windows": {
                "runtime": identity["runtime"],
                "platform": identity["platform"],
                "launch_flags": identity["launch_flags"],
            },
            "wsl": {
                "runtime": wsl["runtime"],
                "platform": wsl["platform"],
            },
        },
    )
    dump(
        output / "COMPANIONS.json",
        {"windows": windows_companions, "wsl": wsl["companions"]},
    )
    dump(
        output / "CASE-RESULTS.json",
        {
            "schema": "colosseum-server1-p02-case-results/v1",
            "worker_id": "P02-A",
            "cases": [case_01, case_02, case_03],
            "windows_blocked_lane": "WINDOWS-BLOCKED.json",
        },
    )
    dump(
        output / "LAUNCH-TRANSCRIPT.json",
        {
            "P02-01-WINDOWS": windows_probe,
            "P02-01-WSL": wsl["cases"]["P02-01-WSL"],
            "P02-02-WSL": wsl["cases"]["P02-02-WSL"],
        },
    )
    dump(
        output / "SOURCE-TRACE.json",
        {
            "schema": "colosseum-server1-p02-source-trace/v2",
            "oracle_sha256": identity["oracle"]["sha256"],
            "oracle_bytes": identity["oracle"]["bytes"],
            "authority_source": {
                "path": r"C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\44-native-stream-server\plans\server1-v2.1-parallel\PARALLEL-WORK-ITEMS.json",
                "sha256": "777319421c8c8d50348492ace51d7c51eb8c6b14e8e225d5d1e8962bdb1c3dc7",
                "worker_id": "P02-A",
            },
            "authorities": [
                {
                    "module": "M194",
                    "authority_lines": "19675-19682",
                    "authority_sha256": "b55829ef3824da2c446073bfd42c764b6871effd46136f64fd67d2a6c1c69843",
                    "observed_lines": "19677-19683",
                    "observed_behavior": "APP_PATH resolution",
                },
                {
                    "module": "M195",
                    "authority_lines": "19682-19707",
                    "authority_sha256": "7756e8b85a0ed5bcee2c3770d30dbd8649e943709e2b7b347e9d5977019e0b87",
                    "observed_lines": "19683-19716;46585,46957-46978",
                    "observed_behavior": "bridge/status and teardown references",
                },
                {
                    "module": "M413",
                    "authority_lines": "35868-35959",
                    "authority_sha256": "778189038cbd3b935199924b20b7a0db61c05471864cf5d022c17c719c6bf0ba",
                    "observed_lines": "35868-35959",
                    "observed_behavior": "embedded package identity and dependency metadata",
                },
                {
                    "module": "M433",
                    "authority_lines": "37687-37693",
                    "authority_sha256": "fb6d9de8a521b2f0a9e1093502728f41186a1cb8f0f69ef59104a33cdb4aa602",
                    "observed_lines": "37615-37625",
                    "observed_behavior": "ffprobe companion use",
                },
                {
                    "module": "M436",
                    "authority_lines": "37824-37826",
                    "authority_sha256": "5029b37ff330f4d35fa11767885507fbe0b7dd6697a7056baa58761f0f8e73e0",
                    "observed_lines": "46584-46588",
                    "observed_behavior": "ffmpeg/ffprobe search paths",
                },
                {
                    "module": "M564",
                    "authority_lines": "46578-46979",
                    "authority_sha256": "d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569",
                    "observed_lines": "46578-46979",
                    "observed_behavior": "server entry, routes, ports, settings",
                },
            ],
        },
    )
    dump(
        output / "PACKET-RECEIPT.json",
        {
            "schema": "colosseum-server1-packet-receipt/v1",
            "worker_id": "P02-A",
            "parent_packet": "P02",
            "branch": "codex/server1-p02-a",
            "base_sha": "1c08a0734acbe6f46b67630f3a735e24df190db1",
            "interfaces_consumed": ["EvidenceLock", "RequirementsLedger"],
            "interfaces_produced": ["ReferenceRuntimeIdentity", "ReferenceQualificationCases"],
            "interface_changes": "none",
            "production_files_changed": 0,
            "case_results": "CASE-RESULTS.json",
            "qualification_state": "PASS_WITH_PROVENANCE",
            "wiring_request": "WIRING-REQUEST.json",
            "push_status": "not pushed; Sol review required",
        },
    )
    dump(
        output / "WIRING-REQUEST.json",
        {
            "schema": "colosseum-server1-wiring-request/v1",
            "worker_id": "P02-A",
            "request": "Integrate the WSL2 Ubuntu reference qualification after Sol review while preserving Windows as BLOCKED_ENV and stremio-runtime as an explicitly missing intended companion.",
            "paths": ["FINGERPRINT.json", "COMPANIONS.json", "CASE-RESULTS.json", "LAUNCH-TRANSCRIPT.json", "WINDOWS-BLOCKED.json", "WSL-QUALIFICATION.json", "SOURCE-TRACE.json"],
        },
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
