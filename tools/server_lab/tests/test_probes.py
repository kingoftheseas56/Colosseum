"""Self-tests for the repeatable Stremio P07-03 baseline report.

The report is an evidence artifact, not a Server 1.0 evaluation.  These tests
keep the four evidence planes separate and make it impossible to accept a
partial, best-only, threshold-drifted, under-sampled, or fake-player report.
"""

from __future__ import annotations

import copy
import hashlib
import json
import math
import os
import statistics
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
REPORT_PATH = ROOT / "docs" / "server1" / "BASELINE-REPORT.json"
WORK_ITEMS_PATH = Path(
    os.environ.get(
        "PARALLEL_WORK_ITEMS_JSON",
        r"C:\Users\PublicUser\Desktop\Preflight-Architect\arcs\44-native-stream-server\plans\server1-v2.1-parallel\PARALLEL-WORK-ITEMS.json",
    )
)
ORACLE_PATH = Path(
    r"C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js"
)
EXPECTED_ORACLE_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
EXPECTED_PLAN_SHA256 = "777319421c8c8d50348492ace51d7c51eb8c6b14e8e225d5d1e8962bdb1c3dc7"
EXPECTED_MODULES = {
    "M172": {
        "lines": "18110-18500",
        "sha256": "bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486",
    },
    "M846": {
        "lines": "74730-74772",
        "sha256": "fdff76e6c1b5a55a544a9de1d52f7882a63aef8a8d64a3425cd9f0218391c727",
    },
}
EXPECTED_LIBMPV_SHA256 = "f709c7ca8b183bec76b8158bf0c45c53018c63366750729352612f228ff7bdea"
EXPECTED_LIBMPV_PATH = r"C:\tools\mpvqt-feasibility\libmpv-prefix\bin\libmpv-2.dll"
T_CRITICAL_95_N10 = 2.2621571627


class BaselineReportError(ValueError):
    """Raised when a baseline artifact cannot support a trustworthy result."""


def _canonical_sha256(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise BaselineReportError(message)


def load_report() -> dict[str, Any]:
    return json.loads(REPORT_PATH.read_text(encoding="utf-8"))


def validate_report(report: dict[str, Any]) -> None:
    """Validate the packet-local BaselineReport interface and its evidence lanes."""

    _require(report.get("schema") == "colosseum-server1-baseline-report/v1", "schema mismatch")
    _require(report.get("worker_id") == "P07-C", "worker identity mismatch")
    _require(report.get("parent_packet") == "P07", "parent packet mismatch")
    _require(report.get("case") == "P07-03", "case mismatch")
    _require(report.get("evaluation_target") == "stremio_baseline_only", "baseline scope is not explicit")
    _require(report.get("server1_evaluation") == "NOT_RUN", "Server 1.0 evaluation was promoted from baseline")
    _require(report.get("parity_verdict") == "NOT_RUN", "parity verdict was promoted from baseline")
    _require(report.get("performance_verdict") == "BASELINE_ONLY", "performance scoreboard is not baseline-only")

    source = report.get("source_authority")
    _require(isinstance(source, dict), "source authority is missing")
    oracle = source.get("oracle")
    _require(isinstance(oracle, dict), "oracle identity is missing")
    _require(oracle.get("sha256") == EXPECTED_ORACLE_SHA256, "oracle hash mismatch")
    _require(oracle.get("bytes") == 6_676_503, "oracle byte count mismatch")
    plan = source.get("parallel_work_items")
    _require(isinstance(plan, dict), "parallel work-items identity is missing")
    _require(plan.get("sha256") == EXPECTED_PLAN_SHA256, "parallel work-items hash mismatch")
    modules = {item.get("module"): item for item in source.get("modules", []) if isinstance(item, dict)}
    _require(set(modules) == set(EXPECTED_MODULES), "P07 module authority set mismatch")
    for module, expected in EXPECTED_MODULES.items():
        _require(modules[module].get("lines") == expected["lines"], f"{module} authority range mismatch")
        _require(modules[module].get("sha256") == expected["sha256"], f"{module} authority hash mismatch")

    planes = report.get("evidence_planes")
    _require(isinstance(planes, dict), "four evidence planes are missing")
    _require(
        set(planes) == {"js_module_policy_traces", "raw_protocol", "controlled_runtime", "performance"},
        "evidence planes are incomplete or merged",
    )
    _require(planes["js_module_policy_traces"].get("state") == "SOURCE_TRACED", "JS/module plane is not source-traced")
    _require(planes["raw_protocol"].get("state") == "MEASURED", "raw protocol plane is not measured")
    _require(planes["controlled_runtime"].get("state") == "MEASURED", "controlled runtime plane is not measured")
    _require(planes["performance"].get("state") == "MEASURED", "performance plane is not measured")

    contract = report.get("measurement_contract")
    _require(isinstance(contract, dict), "measurement contract is missing")
    _require(contract.get("declared_before_measurement") is True, "measurement contract was not frozen before measurement")
    _require(contract.get("uncertainty_method") == "student_t_95_ci", "uncertainty method is not frozen")
    _require(contract.get("thresholds_sha256") == _canonical_sha256(contract.get("thresholds")), "thresholds changed after measurement")
    _require(isinstance(contract.get("timeout_seconds"), (int, float)) and contract["timeout_seconds"] > 0, "timeout is invalid")
    _require(isinstance(contract.get("warmup_count"), int) and contract["warmup_count"] >= 0, "warmup count is invalid")
    _require(isinstance(contract.get("measured_count"), int) and contract["measured_count"] >= 10, "measured count is under ten")
    _require(isinstance(contract.get("observation_definitions"), dict) and contract["observation_definitions"], "observations are not defined")
    _require(isinstance(contract.get("thresholds"), dict) and contract["thresholds"], "thresholds are not defined")

    profiles = contract.get("profiles")
    _require(isinstance(profiles, list) and profiles, "selected performance profiles are missing")
    for profile in profiles:
        _require(isinstance(profile, dict), "performance profile is not an object")
        profile_id = profile.get("id", "unknown")
        _require(profile.get("warmup_count") == contract["warmup_count"], f"{profile_id}: warmup count drifted")
        _require(profile.get("measured_count") == contract["measured_count"], f"{profile_id}: measured count drifted")
        samples = profile.get("samples")
        _require(isinstance(samples, list), f"{profile_id}: samples are missing")
        _require(len(samples) == profile["measured_count"], f"{profile_id}: best-only or partial samples were reported")
        _require([sample.get("trial") for sample in samples] == list(range(1, profile["measured_count"] + 1)), f"{profile_id}: samples are not complete and ordered")
        _require(all(sample.get("phase") == "measured" for sample in samples), f"{profile_id}: warmup sample promoted to measurement")
        for sample in samples:
            _require(sample.get("oracle_sha256") == EXPECTED_ORACLE_SHA256, f"{profile_id}: sample oracle identity mismatch")
            _require(isinstance(sample.get("raw_output"), str) and sample["raw_output"], f"{profile_id}: raw output path missing")
            for metric in profile["metric_names"]:
                _require(isinstance(sample.get(metric), (int, float)) and sample[metric] >= 0, f"{profile_id}: invalid {metric}")

        summary = profile.get("summary")
        _require(isinstance(summary, dict), f"{profile_id}: uncertainty summary missing")
        metric = profile["primary_metric"]
        values = [float(sample[metric]) for sample in samples]
        expected_mean = statistics.mean(values)
        expected_stddev = statistics.stdev(values)
        expected_ci = T_CRITICAL_95_N10 * expected_stddev / math.sqrt(len(values))
        _require(summary.get("n") == len(values), f"{profile_id}: uncertainty n does not cover every sample")
        _require(math.isclose(summary.get("mean"), expected_mean, rel_tol=0, abs_tol=1e-9), f"{profile_id}: mean is not from all samples")
        _require(math.isclose(summary.get("sample_stddev"), expected_stddev, rel_tol=0, abs_tol=1e-9), f"{profile_id}: standard deviation is not from all samples")
        _require(math.isclose(summary.get("ci95_half_width"), expected_ci, rel_tol=0, abs_tol=1e-9), f"{profile_id}: uncertainty is not Student-t 95% CI")
        _require(summary.get("ci95_low") <= summary.get("mean") <= summary.get("ci95_high"), f"{profile_id}: CI does not contain mean")
        _require(summary.get("min") == min(values), f"{profile_id}: minimum sample is not reported")
        _require(summary.get("max") == max(values), f"{profile_id}: maximum sample is not reported")
        _require("best" not in profile and "best_run" not in profile, f"{profile_id}: best-only field is present")

    comparison = report.get("server_0_1_comparison")
    _require(isinstance(comparison, dict), "Server 0.1 comparison lane is missing")
    _require(comparison.get("state") == "BASELINE_UNAVAILABLE", "unavailable Server 0.1 was promoted")
    _require(comparison.get("blocking") is False, "Server 0.1 unavailable state blocks the baseline")

    player = report.get("player_lane")
    _require(isinstance(player, dict), "player lane is missing")
    _require(player.get("state") in {"NOT_RUN", "UNAVAILABLE"}, "player telemetry was promoted")
    _require(player.get("telemetry") in {"NOT_RUN", "UNAVAILABLE"}, "player telemetry state is not honest")
    if player.get("telemetry_samples"):
        _require(player.get("source", {}).get("evidence_class") == "native_libmpv_adapter", "fake player telemetry is not accepted")
        _require(player.get("qualification_scope") == "runtime", "packet-local player telemetry is not runtime evidence")
    libmpv = player.get("libmpv")
    _require(isinstance(libmpv, dict), "pinned libmpv identity is missing")
    _require(libmpv.get("path") == EXPECTED_LIBMPV_PATH, "pinned libmpv path mismatch")
    _require(libmpv.get("sha256") == EXPECTED_LIBMPV_SHA256, "pinned libmpv hash mismatch")
    _require(libmpv.get("client_api") == "2.5", "pinned libmpv API mismatch")


class BaselineReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.report = load_report()

    def test_report_is_complete_and_baseline_only(self) -> None:
        validate_report(self.report)

    def test_p07_03_reports_every_sample_and_uncertainty(self) -> None:
        validate_report(self.report)
        for profile in self.report["measurement_contract"]["profiles"]:
            self.assertGreaterEqual(len(profile["samples"]), 10)
            self.assertEqual(profile["summary"]["n"], len(profile["samples"]))
            self.assertIn("ci95_half_width", profile["summary"])

    def test_changed_after_run_thresholds_are_rejected(self) -> None:
        mutated = copy.deepcopy(self.report)
        mutated["measurement_contract"]["thresholds"]["startup_deadline_seconds"] += 1
        with self.assertRaisesRegex(BaselineReportError, "thresholds changed"):
            validate_report(mutated)

    def test_missing_profile_dataset_is_rejected(self) -> None:
        mutated = copy.deepcopy(self.report)
        mutated["measurement_contract"]["profiles"][0].pop("samples")
        with self.assertRaisesRegex(BaselineReportError, "samples are missing"):
            validate_report(mutated)

    def test_partial_or_best_only_dataset_is_rejected(self) -> None:
        mutated = copy.deepcopy(self.report)
        profile = mutated["measurement_contract"]["profiles"][0]
        profile["samples"] = profile["samples"][:1]
        with self.assertRaisesRegex(BaselineReportError, "best-only or partial"):
            validate_report(mutated)

    def test_under_ten_dataset_is_rejected(self) -> None:
        mutated = copy.deepcopy(self.report)
        mutated["measurement_contract"]["measured_count"] = 9
        with self.assertRaisesRegex(BaselineReportError, "under ten"):
            validate_report(mutated)

    def test_fake_player_telemetry_is_rejected(self) -> None:
        mutated = copy.deepcopy(self.report)
        mutated["player_lane"]["state"] = "PASS"
        mutated["player_lane"]["telemetry"] = "PASS"
        mutated["player_lane"]["telemetry_samples"] = [{"first_presented_frame": 0.01, "source": "fake-player"}]
        with self.assertRaisesRegex(BaselineReportError, "player telemetry was promoted"):
            validate_report(mutated)

    def test_server_0_1_unavailability_is_non_blocking_and_not_a_parity_verdict(self) -> None:
        validate_report(self.report)
        self.assertEqual(self.report["server_0_1_comparison"]["state"], "BASELINE_UNAVAILABLE")
        self.assertFalse(self.report["server_0_1_comparison"]["blocking"])
        self.assertEqual(self.report["parity_verdict"], "NOT_RUN")

    def test_all_raw_sample_paths_are_preserved(self) -> None:
        validate_report(self.report)
        for profile in self.report["measurement_contract"]["profiles"]:
            for sample in profile["samples"]:
                path = ROOT / sample["raw_output"]
                self.assertTrue(path.is_file(), path)


class SourceAndEnvironmentTests(unittest.TestCase):
    def test_source_authority_files_are_still_authenticated(self) -> None:
        self.assertEqual(hashlib.sha256(WORK_ITEMS_PATH.read_bytes()).hexdigest(), EXPECTED_PLAN_SHA256)
        self.assertEqual(hashlib.sha256(ORACLE_PATH.read_bytes()).hexdigest(), EXPECTED_ORACLE_SHA256)


if __name__ == "__main__":
    unittest.main()
