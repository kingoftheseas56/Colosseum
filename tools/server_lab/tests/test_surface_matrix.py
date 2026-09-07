"""P02S-A runtime-surface accounting and profile reachability tests."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MATRIX_PATH = ROOT / "docs/server1/RUNTIME-SURFACE-MATRIX.json"
PROFILES_PATH = ROOT / "tools/server_lab/scenarios/reference_profiles.json"
CASE_PATH = ROOT / "tools/server_lab/cases/P02S-A.json"
ARTIFACT_ROOT = ROOT / "artifacts/server1/P02S/P02S-A"
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


if __name__ == "__main__":
    unittest.main()
