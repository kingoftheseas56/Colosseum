"""Run P02 reference qualification and write bounded evidence artifacts."""

from __future__ import annotations

import argparse
import json
import os
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
    companions = reference.companions()
    probe = reference.run_probe(("/heartbeat", "/settings"))

    if probe["ready"]:
        case_01_state = "PASS"
        case_01_reason = "reference served /heartbeat and /settings"
    elif probe["failure"]["kind"] == "bind-denied":
        case_01_state = "BLOCKED_ENV"
        case_01_reason = "host denied the reference listener bind"
    else:
        case_01_state = "FAIL"
        case_01_reason = probe["failure"]["kind"]

    occupied, occupancy_failure = occupy_required_ports()
    if occupancy_failure:
        case_02 = {
            "id": "P02-02",
            "state": "BLOCKED_ENV",
            "reason": "fixture could not occupy the required port range",
            "fixture": occupancy_failure,
        }
    else:
        try:
            occupied_probe = reference.run_probe(("/heartbeat",), timeout=2.5)
        finally:
            for sock in occupied:
                sock.close()
        case_02 = {
            "id": "P02-02",
            "state": "PASS"
            if occupied_probe["failure"]["kind"] == "port-exhausted"
            and occupied_probe["port"] is None
            and 11475 not in occupied_probe["failure"]["attempted_ports"]
            else "FAIL",
            "result": occupied_probe,
        }

    case_03 = {
        "id": "P02-03",
        "state": "PASS" if companions["ffmpeg"]["status"] == "present" and companions["ffprobe"]["status"] == "present" else "BOUNDED_MISSING",
        "companions": companions,
        "certificate_fixtures": {
            "status": "not-exercised",
            "reason": "HTTPS was disabled for the controlled HTTP qualification run; no certificate fixture was supplied",
            "repair": "none",
        },
        "native_companions": {
            "status": "not-required-for-exercised-path",
            "reason": "heartbeat/settings startup path uses the single-file oracle under the resolved JavaScript runtime",
            "repair": "none",
        },
        "outbound_connections": {
            "status": "not-observed",
            "reason": "only loopback HTTP probes were issued; no external request route was exercised",
        },
    }

    dump(output / "FINGERPRINT.json", identity)
    dump(output / "COMPANIONS.json", companions)
    dump(
        output / "CASE-RESULTS.json",
        {
            "schema": "colosseum-server1-p02-case-results/v1",
            "worker_id": "P02-A",
            "cases": [
                {
                    "id": "P02-01",
                    "state": case_01_state,
                    "reason": case_01_reason,
                    "result": probe,
                },
                case_02,
                case_03,
            ],
        },
    )
    dump(output / "LAUNCH-TRANSCRIPT.json", {"P02-01": probe})
    dump(
        output / "SOURCE-TRACE.json",
        {
            "oracle_sha256": identity["oracle"]["sha256"],
            "oracle_bytes": identity["oracle"]["bytes"],
            "authorities": [
                {"module": "M194", "lines": "19677-19683", "behavior": "APP_PATH resolution"},
                {"module": "M195", "lines": "19683-19716;46585,46957-46978", "behavior": "bridge/status and teardown references"},
                {"module": "M413", "lines": "35868-35959", "behavior": "embedded package identity and dependency metadata"},
                {"module": "M433", "lines": "37615-37625", "behavior": "ffprobe companion use"},
                {"module": "M436", "lines": "46584-46588", "behavior": "ffmpeg/ffprobe search paths"},
                {"module": "M564", "lines": "46578-46979", "behavior": "server entry, routes, ports, settings"},
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
            "wiring_request": "WIRING-REQUEST.json",
            "push_status": "not pushed; Sol review required",
        },
    )
    dump(
        output / "WIRING-REQUEST.json",
        {
            "schema": "colosseum-server1-wiring-request/v1",
            "worker_id": "P02-A",
            "request": "Integrate P02 evidence into the G-REFERENCE ledger after Sol review; do not wire a runtime success from BLOCKED_ENV or BOUNDED_MISSING cases.",
            "paths": ["FINGERPRINT.json", "COMPANIONS.json", "CASE-RESULTS.json", "LAUNCH-TRANSCRIPT.json", "SOURCE-TRACE.json"],
        },
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
