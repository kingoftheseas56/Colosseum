"""Build the committed P07-C BaselineReport from real WSL samples."""

from __future__ import annotations

import hashlib
import json
import math
import statistics
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
PACKET_ROOT = ROOT / "artifacts" / "server1" / "P07" / "P07-C"
MEASUREMENT_PATH = PACKET_ROOT / "measurement-run.json"
REPORT_PATH = ROOT / "docs" / "server1" / "BASELINE-REPORT.json"
ORACLE = Path(r"C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js")
WORK_ITEMS = Path(
    r"C:\Users\PublicUser\Desktop\Preflight-Architect\arcs\44-native-stream-server\plans\server1-v2.1-parallel\PARALLEL-WORK-ITEMS.json"
)
ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
PLAN_SHA256 = "777319421c8c8d50348492ace51d7c51eb8c6b14e8e225d5d1e8962bdb1c3dc7"
T_CRITICAL_95_N10 = 2.2621571627


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def summary(values: list[float]) -> dict[str, Any]:
    mean = statistics.mean(values)
    sample_stddev = statistics.stdev(values)
    ci95_half_width = T_CRITICAL_95_N10 * sample_stddev / math.sqrt(len(values))
    return {
        "n": len(values),
        "mean": mean,
        "sample_stddev": sample_stddev,
        "ci95_confidence": 0.95,
        "ci95_t_critical": T_CRITICAL_95_N10,
        "ci95_half_width": ci95_half_width,
        "ci95_low": mean - ci95_half_width,
        "ci95_high": mean + ci95_half_width,
        "min": min(values),
        "max": max(values),
    }


def file_identity(path: Path) -> dict[str, Any]:
    return {"path": str(path.relative_to(ROOT)).replace("\\", "/"), "sha256": sha256(path)}


def main() -> int:
    measurement = json.loads(MEASUREMENT_PATH.read_text(encoding="utf-8"))
    if measurement["status"] != "PASS":
        raise SystemExit("refusing to publish a baseline from a failed measurement run")
    if measurement["oracle"]["sha256"] != ORACLE_SHA256:
        raise SystemExit("measurement oracle hash mismatch")
    if sha256(ORACLE) != ORACLE_SHA256:
        raise SystemExit("live oracle hash mismatch")
    if sha256(WORK_ITEMS) != PLAN_SHA256:
        raise SystemExit("parallel work-items hash mismatch")

    contract = {
        "declared_before_measurement": True,
        "warmup_count": measurement["contract"]["warmup_count"],
        "measured_count": measurement["contract"]["measured_count"],
        "timeout_seconds": measurement["contract"]["timeout_seconds"],
        "uncertainty_method": "student_t_95_ci",
        "confidence": 0.95,
        "thresholds": {
            "startup_deadline_seconds": 6.0,
            "heartbeat_status": 200,
            "settings_status": 200,
            "settings_server_version": "4.21.0",
            "oracle_identity_match": True,
            "teardown_alive_after": False,
        },
        "observation_definitions": {
            "startup_seconds": "Monotonic elapsed time from fresh Node process spawn to the first 200 /heartbeat response.",
            "heartbeat_request_seconds": "Monotonic duration of a post-readiness GET /heartbeat request.",
            "settings_request_seconds": "Monotonic duration of a post-readiness GET /settings request.",
            "teardown_seconds": "Monotonic elapsed time from termination request until the reference process exits.",
            "oracle_identity": "SHA-256 is checked before and after every trial; a changed oracle is a failed trial.",
            "uncertainty": "Sample standard deviation with a two-sided Student-t 95% confidence interval over every measured sample; no best-run selection.",
        },
    }
    contract["thresholds_sha256"] = canonical_sha256(contract["thresholds"])

    samples = measurement["samples"]
    metric_names = ["startup_seconds", "heartbeat_request_seconds", "settings_request_seconds", "teardown_seconds"]
    metric_summaries = {
        metric: summary([float(sample[metric]) for sample in samples]) for metric in metric_names
    }
    profile = {
        "id": measurement["profile"],
        "description": "Fresh disposable Stremio reference process serving the controlled heartbeat/settings route corpus.",
        "runtime_lane": "WSL2 Ubuntu qualified reference lane",
        "routes": measurement["contract"]["routes"],
        "warmup_count": contract["warmup_count"],
        "measured_count": contract["measured_count"],
        "metric_names": metric_names,
        "primary_metric": "startup_seconds",
        "samples": samples,
        "summary": metric_summaries["startup_seconds"],
        "metric_summaries": metric_summaries,
        "result": "PASS",
    }

    probe_files = [
        ROOT / "tools" / "server_lab" / "probes" / "http_bytes.py",
        ROOT / "tools" / "server_lab" / "probes" / "resources.py",
        ROOT / "tools" / "server_lab" / "probes" / "player.py",
        ROOT / "tools" / "server_lab" / "tests" / "test_probes.py",
    ]
    report = {
        "schema": "colosseum-server1-baseline-report/v1",
        "worker_id": "P07-C",
        "parent_packet": "P07",
        "case": "P07-03",
        "status": "BASELINE_PUBLISHED",
        "evaluation_target": "stremio_baseline_only",
        "server1_evaluation": "NOT_RUN",
        "parity_verdict": "NOT_RUN",
        "performance_verdict": "BASELINE_ONLY",
        "source_authority": {
            "oracle": {
                "path": str(ORACLE),
                "bytes": ORACLE.stat().st_size,
                "sha256": ORACLE_SHA256,
            },
            "parallel_work_items": {
                "path": str(WORK_ITEMS),
                "sha256": PLAN_SHA256,
            },
            "modules": [
                {
                    "module": "M172",
                    "lines": "18110-18500",
                    "sha256": "bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486",
                },
                {
                    "module": "M846",
                    "lines": "74730-74772",
                    "sha256": "fdff76e6c1b5a55a544a9de1d52f7882a63aef8a8d64a3425cd9f0218391c727",
                },
            ],
            "source_trace": "artifacts/server1/P07/P07-C/SOURCE-TRACE.json",
        },
        "interfaces_consumed": {
            "LabRunner": {"state": "PRESENT", "path": "tools/server_lab/lab.py"},
            "EvidenceSchema": {"state": "PRESENT", "path": "tools/server_lab/evidence.py"},
            "QualifiedFixtureCorpus": {"state": "PRESENT_NOT_CONSUMED_BY_STREMIO_ADAPTER", "path": "tools/server_lab/cases/P06-C.json"},
            "QualifiedReferenceRuntime": {"state": "QUALIFIED_WSL", "path": "artifacts/server1/P02/WSL-QUALIFICATION.json"},
            "ByteProbe": {"state": "PRESENT", "path": "tools/server_lab/probes/http_bytes.py"},
            "ResourceProbe": {"state": "PRESENT", "path": "tools/server_lab/probes/resources.py"},
            "PlayerProbe": {"state": "PRESENT_NOT_RUN_FOR_BASELINE", "path": "tools/server_lab/probes/player.py"},
        },
        "candidate_hashes": [file_identity(path) for path in probe_files],
        "evidence_planes": {
            "js_module_policy_traces": {
                "state": "SOURCE_TRACED",
                "source_trace": "artifacts/server1/P07/P07-C/SOURCE-TRACE.json",
                "candidate_evaluation": "NOT_RUN",
            },
            "raw_protocol": {
                "state": "MEASURED",
                "raw_samples": "artifacts/server1/P07/P07-C/raw-samples/",
                "protocol_is_preserved": True,
            },
            "controlled_runtime": {
                "state": "MEASURED",
                "lane": "WSL2 Ubuntu",
                "oracle_processes": contract["measured_count"] + contract["warmup_count"],
                "server1_candidate": "NOT_RUN",
            },
            "performance": {
                "state": "MEASURED",
                "profiles": [profile["id"]],
                "parity_scoreboard": "SEPARATE_NOT_RUN",
            },
        },
        "controlled_corpus": {
            "id": "P02-reference-control-routes-v1",
            "inputs": {
                "routes": ["/heartbeat", "/settings"],
                "settings_file": "{}\\n",
                "https": "disabled",
                "network_interfaces": "disabled",
                "caching": "disabled",
            },
            "goldens": {
                "/heartbeat": {"status": 200, "json": {"success": True}},
                "/settings": {"status": 200, "server_version": "4.21.0"},
            },
            "p06_torrent_fixture_consumption": {
                "state": "UNSUPPORTED",
                "reason": "The existing Stremio adapter exposes no controlled P06 torrent-fixture injection interface; no fabricated torrent parity result is recorded.",
            },
        },
        "measurement_contract": {
            **contract,
            "profiles": [profile],
        },
        "runtime_identity": {
            "lane": "WSL2 Ubuntu",
            "node": measurement["runtime"],
            "ffmpeg": {"path": "/usr/bin/ffmpeg", "state": "PRESENT", "provenance": "WSL qualification"},
            "ffprobe": {"path": "/usr/bin/ffprobe", "state": "PRESENT", "provenance": "WSL qualification"},
            "intended_stremio_runtime": {"state": "MISSING", "repair": "none"},
        },
        "windows_limitation": {
            "state": "BLOCKED_ENV",
            "excluded_tcp_range": [11464, 11563],
            "required_oracle_ports": [11470, 11471, 11472, 11473, 11474],
            "ffmpeg": "MISSING",
            "ffprobe": "MISSING",
            "evidence": "artifacts/server1/P02/WINDOWS-BLOCKED.json",
        },
        "server_0_1_comparison": {
            "state": "BASELINE_UNAVAILABLE",
            "blocking": False,
            "source": "artifacts/server1/P01B/CASE-RESULTS.json",
            "reason": "P01B runtime_mpv_playback_test exited 8 because mpv did not load the native torrent URL; optional comparison evidence is not promoted.",
        },
        "player_lane": {
            "state": "NOT_RUN",
            "telemetry": "NOT_RUN",
            "telemetry_samples": [],
            "reason": "P07-C publishes the Stremio baseline; no fake player telemetry is accepted and P07-B packet-local libmpv evidence is not runtime playback qualification.",
            "libmpv": {
                "path": r"C:\tools\mpvqt-feasibility\libmpv-prefix\bin\libmpv-2.dll",
                "sha256": "f709c7ca8b183bec76b8158bf0c45c53018c63366750729352612f228ff7bdea",
                "client_api": "2.5",
                "state": "QUALIFIED_INPUT_NOT_USED_FOR_STREMIO_BASELINE",
            },
        },
        "candidate": {
            "server1": {"state": "NOT_RUN", "source_sha256": None, "reason": "P07-C freezes the baseline before Server 1.0 evaluation."},
            "server_0_1": {"state": "BASELINE_UNAVAILABLE", "source_sha256": None},
        },
        "differential_qualification": {
            "state": "NOT_RUN",
            "parity_and_performance_are_separate": True,
            "reason": "No Server 1.0 candidate is evaluated in this packet.",
        },
        "evidence": {
            "measurement_run": "artifacts/server1/P07/P07-C/measurement-run.json",
            "warmup_samples": [sample["raw_output"] for sample in measurement["warmups"]],
            "measured_samples": [sample["raw_output"] for sample in samples],
            "source_trace": "artifacts/server1/P07/P07-C/SOURCE-TRACE.json",
        },
        "limitations": [
            "Windows live oracle launch is BLOCKED_ENV by the excluded TCP range; accepted WSL qualification is the measured lane.",
            "Windows does not have qualified ffmpeg/ffprobe; WSL companions are recorded with provenance and are not called Windows capability.",
            "The P06 controlled torrent fixture corpus is not consumable through the existing Stremio adapter and remains UNSUPPORTED for this baseline report.",
            "This report freezes thresholds and observations; it does not evaluate or approve Server 1.0.",
        ],
        "evidence_states": [
            "source-traced",
            "test-authored",
            "implementation-authored",
            "compiled",
            "test-executed",
            "measured",
            "not-integrated",
        ],
    }
    REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(REPORT_PATH), "sha256": sha256(REPORT_PATH), "samples": len(samples)}))


if __name__ == "__main__":
    main()
