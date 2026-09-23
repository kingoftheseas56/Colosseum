from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from validate_map import load_json, validate_map


class ValidateMapTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.repo = base / "repo"
        self.preflight = base / "preflight"
        (self.repo / "native").mkdir(parents=True)
        (self.repo / "tests" / "lanista_scenarios").mkdir(parents=True)
        (self.repo / "src").mkdir(parents=True)
        self.preflight.mkdir(parents=True)

        (self.repo / "AGENTS.md").write_text("authority\n", encoding="utf-8")
        (self.repo / "tool.py").write_text("print('index')\n", encoding="utf-8")
        (self.repo / "src" / "owner.h").write_text("// OWNER anchor\n", encoding="utf-8")
        (self.repo / "tests" / "check.ps1").write_text("exit 0\n", encoding="utf-8")
        (self.repo / "tests" / "lanista_scenarios" / "smoke.json").write_text("{}\n", encoding="utf-8")
        (self.preflight / "arc.md").write_text("# context\n", encoding="utf-8")

        cmake = (
            'add_test(NAME colosseum.alpha COMMAND alpha)\n'
            'set_tests_properties(colosseum.alpha PROPERTIES LABELS "unit;alpha")\n'
        )
        (self.repo / "native" / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
        (self.repo / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")

        self.data = {
            "schema_version": 1,
            "map_id": "fixture",
            "repo_basis": {
                "head": "0" * 40,
                "branch": "master",
                "path_semantics": "repo-relative",
                "source_of_truth": ["AGENTS.md"],
            },
            "donors": {"source_index": "tool.py"},
            "known_gaps": [],
            "domains": [
                {
                    "id": "alpha",
                    "display_name": "Alpha",
                    "aliases": ["a"],
                    "source_roots": ["src"],
                    "entry_points": ["src/owner.h"],
                    "owners": [
                        {
                            "name": "Owner",
                            "path": "src/owner.h",
                            "confidence": "observed",
                            "responsibility": "Owns alpha.",
                            "provenance": [{"path": "src/owner.h", "contains": "OWNER anchor"}],
                        }
                    ],
                    "ctests": [{"name": "colosseum.alpha", "labels": ["unit"]}],
                    "checks": ["tests/check.ps1"],
                    "lanista_scenarios": ["tests/lanista_scenarios/smoke.json"],
                    "context": [{"scope": "preflight", "path": "arc.md"}],
                    "platform_constraints": [],
                    "relations": [],
                }
            ],
        }

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def result(self, data=None):
        return validate_map(
            self.data if data is None else data,
            self.repo,
            self.preflight,
            check_head=False,
        )

    def test_valid_fixture_passes(self) -> None:
        result = self.result()
        self.assertTrue(result["ok"], result["errors"])
        self.assertEqual(result["stats"]["domains"], 1)
        self.assertEqual(result["stats"]["ctests"], 1)
        self.assertEqual(result["stats"]["lanista_scenarios"], 1)

    def test_candidate_json_files_parse(self) -> None:
        here = Path(__file__).resolve().parent
        live_map = load_json(here / "colosseum-map.json")
        schema = json.loads((here / "colosseum-map.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(live_map["schema_version"], 1)
        self.assertEqual(schema["properties"]["schema_version"]["const"], 1)

    def test_missing_check_fails(self) -> None:
        (self.repo / "tests" / "check.ps1").unlink()
        result = self.result()
        self.assertFalse(result["ok"])
        self.assertTrue(any("missing repo path" in e for e in result["errors"]))

    def test_missing_related_file_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["related_files"] = ["src/missing.txt"]
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any("related_files[0]: missing repo path" in e for e in result["errors"])
        )

    def test_unknown_ctest_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["ctests"][0]["name"] = "colosseum.missing"
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("CTest name not registered" in e for e in result["errors"]))

    def test_missing_ctest_label_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["ctests"][0]["labels"] = ["unit", "windows"]
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("missing expected labels" in e for e in result["errors"]))

    def test_missing_lanista_scenario_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["lanista_scenarios"][0] = "tests/lanista_scenarios/nope.json"
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("missing repo path" in e for e in result["errors"]))

    def test_malformed_lanista_scenario_fails(self) -> None:
        scenario = self.repo / "tests" / "lanista_scenarios" / "smoke.json"
        scenario.write_text(r'{"path":"C:\qbad"}', encoding="utf-8")
        result = self.result()
        self.assertFalse(result["ok"])
        self.assertTrue(any("invalid scenario JSON" in e for e in result["errors"]))

    def test_stale_provenance_anchor_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["owners"][0]["provenance"][0]["contains"] = "GONE"
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("provenance anchor not found" in e for e in result["errors"]))

    def test_inferred_owner_requires_note(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["owners"][0]["confidence"] = "inferred"
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("inferred owner requires" in e for e in result["errors"]))

    def test_selector_collision_fails(self) -> None:
        data = copy.deepcopy(self.data)
        beta = copy.deepcopy(data["domains"][0])
        beta["id"] = "beta"
        beta["aliases"] = ["a"]
        data["domains"].append(beta)
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("selector collision" in e for e in result["errors"]))

    def test_unknown_relation_fails(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["relations"] = ["missing"]
        result = self.result(data)
        self.assertFalse(result["ok"])
        self.assertTrue(any("unknown domain id" in e for e in result["errors"]))

    def test_semantic_worktree_contract_accepts_valid_shape(self) -> None:
        data = copy.deepcopy(self.data)
        data["repo_basis"]["semantic_worktree"] = {
            "algorithm": "sha256-git-semantic-v1",
            "watch_scopes": ["src", "tests"],
            "fingerprint": "a" * 64,
        }

        result = self.result(data)

        self.assertTrue(result["ok"], result["errors"])

    def test_semantic_worktree_contract_rejects_invalid_shape(self) -> None:
        data = copy.deepcopy(self.data)
        data["repo_basis"]["semantic_worktree"] = {
            "algorithm": "wrong",
            "watch_scopes": ["../outside"],
            "fingerprint": "nope",
        }

        result = self.result(data)

        self.assertFalse(result["ok"])
        self.assertTrue(any("semantic_worktree.algorithm" in e for e in result["errors"]))
        self.assertTrue(any("semantic_worktree.watch_scopes" in e for e in result["errors"]))
        self.assertTrue(any("semantic_worktree.fingerprint" in e for e in result["errors"]))

    def test_preflight_context_path_escape_fails(self) -> None:
        outside = self.preflight.parent / "escape.md"
        outside.write_text("escape\n", encoding="utf-8")
        data = copy.deepcopy(self.data)
        data["domains"][0]["context"][0]["path"] = "../escape.md"

        result = self.result(data)

        self.assertFalse(result["ok"])
        self.assertTrue(any("escapes preflight root" in e for e in result["errors"]))

    def test_context_metadata_and_known_constraints_accept_valid_shape(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["known_constraints"] = [
            "Canonical owner must remain unique."
        ]
        data["domains"][0]["context"][0].update({
            "role": "arc-status",
            "arc_id": "49",
            "authority": "binding",
        })

        result = self.result(data)

        self.assertTrue(result["ok"], result["errors"])

    def test_context_metadata_and_known_constraints_reject_invalid_shape(self) -> None:
        data = copy.deepcopy(self.data)
        data["domains"][0]["known_constraints"] = ["valid", 7]
        data["domains"][0]["context"][0].update({
            "role": "mystery",
            "arc_id": "",
            "authority": "maybe",
        })

        result = self.result(data)

        self.assertFalse(result["ok"])
        self.assertTrue(any("known_constraints" in e for e in result["errors"]))
        self.assertTrue(any(".role:" in e for e in result["errors"]))
        self.assertTrue(any(".arc_id:" in e for e in result["errors"]))
        self.assertTrue(any(".authority:" in e for e in result["errors"]))

    def test_missing_preflight_context_fails(self) -> None:
        (self.preflight / "arc.md").unlink()
        result = self.result()
        self.assertFalse(result["ok"])
        self.assertTrue(any("missing preflight path" in e for e in result["errors"]))


if __name__ == "__main__":
    unittest.main()
