"""P02S-A runtime-surface accounting and profile reachability tests."""

from __future__ import annotations

import copy
import json
import os
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MATRIX_PATH = ROOT / "docs/server1/RUNTIME-SURFACE-MATRIX.json"
PROFILES_PATH = ROOT / "tools/server_lab/scenarios/reference_profiles.json"
CASE_PATH = ROOT / "tools/server_lab/cases/P02S-A.json"
ARTIFACT_ROOT = ROOT / "artifacts/server1/P02S/P02S-A"
RUNNER_PATH = ARTIFACT_ROOT / "run_reference_profiles.py"
PROFILE_RUNS_PATH = ARTIFACT_ROOT / "profile-runs.json"
QUALIFICATION_PATH = ARTIFACT_ROOT / "QUALIFICATION-SUMMARY.json"
QUALIFIED_NODE = Path(
    r"C:\Users\PublicUser\AppData\Local\Temp\p05-b-node-v22.16.0"
) / "node-v22.16.0-win-x64" / "node.exe"
CLASSIFICATIONS = {
    "always_initialized",
    "always_mounted",
    "conditional_mount",
    "profile_or_platform_specific",
    "dependency_only",
    "unobserved_requires_trace",
}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_matrix(matrix: dict) -> None:
    assert matrix["schema"] == "colosseum-server1-runtime-surface-matrix/v1"
    assert matrix["oracle"]["sha256"] == "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"
    assert matrix["source_authority"]["M564"]["sha256"] == "d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569"
    assert matrix["profiles"]
    assert matrix["surfaces"]
    assert matrix["required_profile_ids"] == [
        "desktop-default",
        "desktop-tv-env",
        "desktop-unity-env",
        "desktop-ios-env",
        "android-default",
    ]
    for row in matrix["surfaces"]:
        assert row["id"]
        assert row["kind"] in {"service", "route", "side_effect"}
        assert row["classification"] in CLASSIFICATIONS
        assert row["source_modules"]
        assert row["source_lines"]
        assert row["evidence_refs"]
        assert set(row["profile_states"]) == set(matrix["required_profile_ids"])


class SurfaceMatrixTests(unittest.TestCase):
    def test_p02s_01_profiles_change_only_source_composition(self) -> None:
        matrix = load_json(MATRIX_PATH)
        profiles = load_json(PROFILES_PATH)
        validate_matrix(matrix)
        self.assertEqual(
            profiles["profiles"]["desktop-tv-env"]["source_composition_overrides"],
            {"casting": "disabled", "caching": "disabled", "cors": "disabled", "hls_v2": "disabled"},
        )
        self.assertEqual(
            profiles["profiles"]["desktop-unity-env"]["source_composition_overrides"],
            {"casting": "disabled", "caching": "disabled", "cors": "disabled", "hls_v2": "disabled", "network_interfaces": "disabled"},
        )
        self.assertEqual(
            profiles["profiles"]["desktop-ios-env"]["source_composition_overrides"],
            {"casting": "disabled", "hls_v2": "disabled", "https": "disabled"},
        )
        rows = {row["id"]: row for row in matrix["surfaces"]}
        self.assertEqual(rows["route.hls-v2"]["classification"], "conditional_mount")
        self.assertEqual(rows["route.casting"]["classification"], "conditional_mount")
        self.assertEqual(rows["route.network-info"]["classification"], "always_mounted")
        self.assertNotEqual(rows["route.casting"]["profile_states"]["desktop-default"], "absent")
        self.assertNotEqual(rows["route.hls-v2"]["profile_states"]["desktop-default"], "absent")

    def test_p02s_02_dependency_only_is_not_a_mount(self) -> None:
        matrix = load_json(MATRIX_PATH)
        rows = {row["id"]: row for row in matrix["surfaces"]}
        self.assertEqual(rows["dependency.hls-converter"]["classification"], "dependency_only")
        self.assertEqual(rows["service.enginefs"]["classification"], "always_initialized")
        self.assertEqual(rows["route.local-addon"]["classification"], "always_mounted")
        self.assertEqual(rows["side-effect.local-addon-indexing"]["classification"], "conditional_mount")
        self.assertNotEqual(rows["dependency.hls-converter"]["profile_states"]["desktop-default"], "mounted")

    def test_p02s_03_completeness_rejects_deleted_observed_surface(self) -> None:
        matrix = load_json(MATRIX_PATH)
        validate_matrix(matrix)
        self.assertEqual(
            {row["id"] for row in matrix["surfaces"]},
            set(matrix["source_accounting"]["required_surface_ids"]),
        )
        mutated = copy.deepcopy(matrix)
        mutated["surfaces"].pop()
        with self.assertRaises(AssertionError):
            self.assertEqual(
                {row["id"] for row in mutated["surfaces"]},
                set(mutated["source_accounting"]["required_surface_ids"]),
            )
        rows = {row["id"]: row for row in matrix["surfaces"]}
        self.assertEqual(rows["route.heartbeat"]["required_for_completeness"], True)
        self.assertEqual(rows["route.network-info"]["required_for_completeness"], True)
        self.assertEqual(rows["route.hls-v2"]["required_for_completeness"], True)

    def test_case_packet_and_evidence_contract(self) -> None:
        case = load_json(CASE_PATH)
        self.assertEqual(case["worker_id"], "P02S-A")
        self.assertEqual(case["parent_packet"], "P02S")
        self.assertEqual([item["id"] for item in case["cases"]], ["P02S-01", "P02S-02", "P02S-03"])
        self.assertTrue(ARTIFACT_ROOT.is_dir())

    def test_profile_contract_does_not_inject_invalid_companion_paths(self) -> None:
        profiles = load_json(PROFILES_PATH)
        qualification = profiles["qualification"]
        self.assertEqual(qualification["runtime_env"], "P05_QUALIFIED_NODE")
        self.assertEqual(qualification["required_version"], "v22.16.0")
        self.assertEqual(qualification["companion_policy"], "record-missing-without-override")
        for profile in profiles["profiles"].values():
            self.assertNotIn("FFMPEG_BIN", profile.get("env", {}))
            self.assertNotIn("FFPROBE_BIN", profile.get("env", {}))

    def test_runner_uses_bounded_isolated_observation_and_exact_runtime_selection(self) -> None:
        source = RUNNER_PATH.read_text(encoding="utf-8")
        self.assertIn("P05_QUALIFIED_NODE", source)
        self.assertNotIn('["node"', source)
        self.assertNotIn(".readline(", source)
        self.assertIn("CREATE_NEW_PROCESS_GROUP", source)
        self.assertIn("taskkill", source)
        self.assertIn("server-settings.json", source)
        self.assertIn("PORT_START", source)
        self.assertIn("SO_EXCLUSIVEADDRUSE", source)
        self.assertNotIn("/usr/bin/ffmpeg", source)
        self.assertNotIn("/usr/bin/ffprobe", source)

    def test_profile_receipts_separate_failed_runs_from_observation(self) -> None:
        runs = load_json(PROFILE_RUNS_PATH)
        summary = load_json(QUALIFICATION_PATH)
        self.assertEqual(runs["runtime"]["path"], str(QUALIFIED_NODE))
        self.assertEqual(runs["runtime"]["version"], "v22.16.0")
        self.assertEqual(summary["qualified_runtime"]["path"], str(QUALIFIED_NODE))
        self.assertEqual(summary["qualified_runtime"]["qualification_case"], "P05-02")
        self.assertEqual(summary["qualified_runtime"]["qualification_state"], "PASS")
        for result in runs["profiles"]:
            self.assertIn(result["status"], {"PASS", "UNSUPPORTED", "NOT_RUN", "ERROR"})
            if result["status"] != "PASS":
                self.assertEqual(result["observed_surfaces"], [])
                self.assertEqual(result["observations"], {})
            else:
                self.assertTrue(result["ready"])
                self.assertTrue(result["observations"])

    def test_matrix_separates_source_accounting_from_runtime_observation(self) -> None:
        matrix = load_json(MATRIX_PATH)
        runtime = matrix["runtime_observation"]
        self.assertTrue(runtime["failed_runs_supply_no_observation"])
        self.assertEqual(
            set(runtime["profiles"]),
            set(matrix["required_profile_ids"]),
        )
        for result in runtime["profiles"].values():
            if result["status"] != "PASS":
                self.assertEqual(result["observed_surfaces"], [])
        planned = matrix["planned_packet_scope"]
        accounted = set(matrix["source_accounting"]["required_surface_ids"])
        scoped = set().union(*(set(ids) for ids in planned.values()))
        self.assertTrue({
            "media",
            "network",
            "archive",
            "local",
            "casting",
            "downloader",
        }.issubset(planned))
        self.assertEqual(scoped, accounted)


if __name__ == "__main__":
    unittest.main()
