from __future__ import annotations

import contextlib
import hashlib
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import colosseum_cli as cli


class WorkbenchR1Tests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "repo"
        (self.root / "native" / "build-msvc" / "tests").mkdir(parents=True)
        (self.root / "qml").mkdir()
        (self.root / "tests").mkdir(exist_ok=True)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "config", "user.email", "fixture@example.invalid"], check=True)
        subprocess.run(["git", "-C", str(self.root), "config", "user.name", "Fixture"], check=True)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_resolve_root_falls_back_to_enclosing_colosseum_checkout(self) -> None:
        expected = Path(cli.__file__).resolve().parents[2]
        self.assertTrue(cli.looks_like_colosseum(expected))

        with mock.patch.dict(cli.os.environ, {}, clear=True), contextlib.chdir(self.tmp.name):
            resolved = cli.resolve_root(None)

        self.assertEqual(resolved, expected)

    def commit_fixture(self) -> None:
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "commit", "-qm", "fixture"], check=True)
    def test_status_snapshot_preserves_tracked_and_untracked_records(self) -> None:
        tracked = self.root / "tracked.txt"
        tracked.write_text("one\n", encoding="utf-8")
        self.commit_fixture()
        tracked.write_text("two\n", encoding="utf-8")
        (self.root / "new.txt").write_text("new\n", encoding="utf-8")

        snapshot = cli.repo_snapshot(self.root)

        self.assertTrue(any(row.startswith(" M ") and row.endswith("tracked.txt") for row in snapshot["dirty"]))
        self.assertTrue(any(row.startswith("?? ") and row.endswith("new.txt") for row in snapshot["dirty"]))

    def test_status_reports_unknown_for_legacy_map_without_semantic_fingerprint(self) -> None:
        (self.root / "seed.txt").write_text("seed\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "legacy-map.json"
        map_path.write_text(json.dumps({
            "map_id": "fixture",
            "repo_basis": {
                "head": cli.repo_snapshot(self.root)["head"],
                "branch": "master",
            },
            "domains": [],
        }), encoding="utf-8")

        ns = type("Args", (), {
            "command": "status",
            "root": str(self.root),
            "map": str(map_path),
        })()
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["mapFreshness"]["state"], "UNKNOWN")

    def test_inspect_labels_legacy_map_non_authoritative(self) -> None:
        (self.root / "src").mkdir()
        (self.root / "src" / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "legacy-inspect-map.json"
        map_path.write_text(json.dumps({
            "map_id": "fixture",
            "repo_basis": {
                "head": cli.repo_snapshot(self.root)["head"],
                "branch": "master",
            },
            "domains": [{
                "id": "alpha",
                "aliases": [],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
            }],
        }), encoding="utf-8")

        ns = type("Args", (), {
            "command": "inspect",
            "root": str(self.root),
            "map": str(map_path),
            "target": "alpha",
        })()
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["freshness"]["state"], "UNKNOWN")
        self.assertTrue(any("non-authoritative" in warning for warning in payload["warnings"]))

    def test_ratings_alias_and_shared_path_ranking(self) -> None:
        doc = {
            "map_id": "fixture",
            "repo_basis": {"head": "a" * 40, "branch": "master"},
            "domains": [
                {
                    "id": "shell",
                    "aliases": [],
                    "source_roots": ["qml/Main.qml"],
                    "entry_points": ["qml/Main.qml"],
                    "owners": [{"path": "qml/Main.qml"}],
                },
                {
                    "id": "ratings-reviews",
                    "aliases": ["ratings", "reviews"],
                    "source_roots": ["native/account/SyncEngine.cpp"],
                    "entry_points": ["native/account/SyncEngine.cpp", "qml/Main.qml"],
                    "owners": [{"path": "native/account/SyncEngine.cpp"}],
                },
            ],
        }
        ratings = cli.inspect_map(doc, "ratings", current_head="a" * 40)
        self.assertEqual(ratings["domain"]["id"], "ratings-reviews")

        shared = cli.inspect_map(doc, "qml/Main.qml", current_head="a" * 40)
        self.assertEqual(shared["match"]["kind"], "shared-path")
        self.assertEqual([c["domain"]["id"] for c in shared["candidates"]], ["shell", "ratings-reviews"])
        self.assertTrue(all(c["evidence"] for c in shared["candidates"]))
    def test_unknown_inspect_query_has_nearby_suggestions(self) -> None:
        doc = {
            "map_id": "fixture",
            "repo_basis": {"head": "a" * 40, "branch": "master"},
            "domains": [{
                "id": "account-sync",
                "aliases": ["account"],
                "source_roots": ["native/account/ProfilePreferencesStore.cpp"],
                "entry_points": ["native/account/SyncEngine.cpp"],
                "owners": [{"path": "native/account/ProfilePreferencesStore.cpp"}],
            }],
        }

        with self.assertRaises(cli.HarnessError) as raised:
            cli.inspect_map(doc, "native/profile/ProfilePreferencesStore.cpp", current_head="a" * 40)

        self.assertEqual(raised.exception.code, "INSPECT_NOT_FOUND")
        suggestions = raised.exception.details["suggestions"]
        self.assertEqual(suggestions[0]["target"], "native/account/ProfilePreferencesStore.cpp")

    def _write_ctest_fixture(self, name: str) -> None:
        (self.root / "tests" / "CMakeLists.txt").write_text(
            f"add_test(NAME {name} COMMAND fixture)\n", encoding="utf-8"
        )
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "native" / "build-msvc" / "tests" / "CTestTestfile.cmake").write_text(
            f'add_test({name} "fixture.exe")\n', encoding="utf-8"
        )
    def test_ctest_resolution_requires_registration_and_no_tests_error(self) -> None:
        name = "colosseum.qttest.sync_engine"
        self._write_ctest_fixture(name)

        item = cli.resolve_test(self.root, name)

        self.assertEqual(item["selectedTests"], [name])
        self.assertTrue(item["registration"]["sourceCMake"])
        self.assertTrue(item["registration"]["generatedBuildRegistry"])
        self.assertIn("--no-tests=error", item["argv"])

    def test_future_ctest_is_not_reported_as_success(self) -> None:
        self._write_ctest_fixture("colosseum.qttest.sync_engine")

        with self.assertRaises(cli.HarnessError) as raised:
            cli.resolve_test(self.root, "colosseum.qttest.ratings_reviews_delivery")

        self.assertEqual(raised.exception.code, "TEST_NOT_FOUND")

    def test_verify_empty_plan_is_not_successful_looking(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "seed.txt").write_text("seed\n", encoding="utf-8")
        self.commit_fixture()
        (self.root / "unmapped.txt").write_text("dirty\n", encoding="utf-8")

        plan, warnings = cli.verify_plan(self.root, None)

        self.assertEqual(plan, [])
        self.assertTrue(any("No executable verification surface" in warning for warning in warnings))
    def test_verify_plan_reports_why_existing_check_was_selected(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('ok')\n", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        map_path = Path(self.tmp.name) / "map.json"
        self.commit_fixture()
        (src / "owner.cpp").write_text("// changed\n", encoding="utf-8")

        head = cli.repo_snapshot(self.root)["head"]
        map_path.write_text(json.dumps({
            "map_id": "fixture",
            "repo_basis": {"head": head, "branch": "master"},
            "domains": [{
                "id": "alpha",
                "aliases": [],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": [],
                "relations": [],
            }],
        }), encoding="utf-8")

        plan, warnings = cli.verify_plan(self.root, str(map_path))

        self.assertFalse(warnings)
        self.assertEqual(len(plan), 1)
        self.assertEqual(plan[0]["name"], "test_alpha")
        reasons = plan[0]["selectionReasons"]
        self.assertEqual(reasons[0]["path"], "src/owner.cpp")
        self.assertEqual(reasons[0]["domain"], "alpha")
        self.assertIn("declares this executable check", reasons[0]["because"])

    def test_verify_can_use_clear_exact_match_winner_but_not_ambiguous_peer(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "test_exact.py").write_text("print('ok')\n", encoding="utf-8")
        target_dir = self.root / "native" / "account"
        target_dir.mkdir(parents=True, exist_ok=True)
        target = target_dir / "RatingsReviewsStore.cpp"
        target.write_text("// owner\n", encoding="utf-8")
        map_path = Path(self.tmp.name) / "map-shared.json"
        self.commit_fixture()
        target.write_text("// changed\n", encoding="utf-8")

        head = cli.repo_snapshot(self.root)["head"]
        map_path.write_text(json.dumps({
            "map_id": "fixture",
            "repo_basis": {"head": head, "branch": "master"},
            "domains": [
                {
                    "id": "account-sync",
                    "aliases": [],
                    "source_roots": ["native/account"],
                    "entry_points": ["native/account/Other.cpp"],
                    "owners": [{"path": "native/account/Other.cpp"}],
                    "ctests": [],
                    "checks": [],
                    "lanista_scenarios": [],
                    "relations": [],
                },
                {
                    "id": "ratings-reviews",
                    "aliases": ["ratings"],
                    "source_roots": ["native/account/RatingsReviewsStore.cpp"],
                    "entry_points": ["native/account/RatingsReviewsStore.cpp"],
                    "owners": [{"path": "native/account/RatingsReviewsStore.cpp"}],
                    "ctests": [],
                    "checks": ["tests/test_exact.py"],
                    "lanista_scenarios": [],
                    "relations": [],
                },
            ],
        }), encoding="utf-8")

        plan, warnings = cli.verify_plan(self.root, str(map_path))

        self.assertEqual([item["name"] for item in plan], ["test_exact"])
        self.assertTrue(any("clear exact-match winner" in warning for warning in warnings))
        self.assertEqual(plan[0]["selectionReasons"][0]["domain"], "ratings-reviews")

    def test_verify_command_fails_closed_when_plan_is_empty(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "seed.txt").write_text("seed\n", encoding="utf-8")
        self.commit_fixture()
        (self.root / "unmapped.txt").write_text("dirty\n", encoding="utf-8")

        ns = type("Args", (), {
            "command": "verify",
            "root": str(self.root),
            "dry_run": True,
        })()
        payload = cli.dispatch(ns)

        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error"]["code"], "NO_VERIFICATION_SURFACE")
        self.assertEqual(payload["data"]["checks"], [])
        self.assertTrue(payload["error"]["details"]["nextActions"])

    def _write_semantic_map(
        self,
        map_path: Path,
        domains: list[dict[str, object]],
        watch_scopes: list[str],
    ) -> dict[str, object]:
        head = cli.repo_snapshot(self.root)["head"]
        doc: dict[str, object] = {
            "map_id": "fixture",
            "repo_basis": {
                "head": head,
                "branch": "master",
                "semantic_worktree": {
                    "algorithm": "sha256-git-semantic-v2",
                    "watch_scopes": watch_scopes,
                    "fingerprint": cli.semantic_worktree_fingerprint(
                        self.root, watch_scopes
                    ),
                },
            },
            "domains": domains,
        }
        map_path.write_text(json.dumps(doc), encoding="utf-8")
        return doc

    def test_semantic_freshness_detects_dirty_and_new_untracked_files(self) -> None:
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        doc = self._write_semantic_map(
            Path(self.tmp.name) / "fresh-map.json",
            [],
            ["src"],
        )

        self.assertEqual(cli.map_freshness(self.root, doc)["state"], "FRESH")

        owner.write_text("// changed\n", encoding="utf-8")
        self.assertEqual(
            cli.map_freshness(self.root, doc)["state"], "WORKTREE_DRIFT"
        )

        owner.write_text("// owner\n", encoding="utf-8")
        (src / "new-owner.cpp").write_text("// new\n", encoding="utf-8")
        self.assertEqual(
            cli.map_freshness(self.root, doc)["state"], "WORKTREE_DRIFT"
        )

    def test_semantic_v2_head_move_outside_watch_scope_stays_fresh(self) -> None:
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        doc = self._write_semantic_map(
            Path(self.tmp.name) / "head-move-map.json",
            [],
            ["src"],
        )
        basis_head = doc["repo_basis"]["head"]

        (self.root / "unrelated.txt").write_text(
            "outside watched architecture\n", encoding="utf-8"
        )
        self.commit_fixture()
        freshness = cli.map_freshness(self.root, doc)

        self.assertNotEqual(cli.repo_snapshot(self.root)["head"], basis_head)
        self.assertEqual(freshness["state"], "FRESH")
        self.assertTrue(freshness["authoritative"])
        self.assertTrue(freshness["headMoved"])

    def test_semantic_v2_detects_committed_change_inside_watch_scope(self) -> None:
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        doc = self._write_semantic_map(
            Path(self.tmp.name) / "committed-drift-map.json",
            [],
            ["src"],
        )

        owner.write_text("// changed and committed\n", encoding="utf-8")
        self.commit_fixture()
        freshness = cli.map_freshness(self.root, doc)

        self.assertEqual(freshness["state"], "WORKTREE_DRIFT")
        self.assertFalse(freshness["authoritative"])
        self.assertTrue(freshness["headMoved"])

    def test_semantic_v1_replays_basis_head_after_unrelated_head_move(self) -> None:
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        head = cli.repo_snapshot(self.root)["head"]
        doc = {
            "map_id": "legacy-semantic",
            "repo_basis": {
                "head": head,
                "branch": "master",
                "semantic_worktree": {
                    "algorithm": "sha256-git-semantic-v1",
                    "watch_scopes": ["src"],
                    "fingerprint": cli.semantic_worktree_fingerprint(
                        self.root, ["src"], "sha256-git-semantic-v1"
                    ),
                },
            },
            "domains": [],
        }

        (self.root / "unrelated.txt").write_text("new head\n", encoding="utf-8")
        self.commit_fixture()
        freshness = cli.map_freshness(self.root, doc)

        self.assertEqual(freshness["state"], "FRESH")
        self.assertTrue(freshness["authoritative"])
        self.assertTrue(freshness["headMoved"])

        owner.write_text("// watched drift\n", encoding="utf-8")
        self.assertEqual(
            cli.map_freshness(self.root, doc)["state"], "WORKTREE_DRIFT"
        )

    def test_semantic_freshness_detects_delete_and_rename(self) -> None:
        src = self.root / "src"
        src.mkdir()
        first = src / "first.cpp"
        second = src / "second.cpp"
        first.write_text("// first\n", encoding="utf-8")
        second.write_text("// second\n", encoding="utf-8")
        self.commit_fixture()
        doc = self._write_semantic_map(
            Path(self.tmp.name) / "rename-map.json",
            [],
            ["src"],
        )

        first.unlink()
        self.assertEqual(
            cli.map_freshness(self.root, doc)["state"], "WORKTREE_DRIFT"
        )

        subprocess.run(
            ["git", "-C", str(self.root), "checkout", "--", "src/first.cpp"],
            check=True,
        )
        second.rename(src / "renamed.cpp")
        self.assertEqual(
            cli.map_freshness(self.root, doc)["state"], "WORKTREE_DRIFT"
        )

    def test_task_scoped_verify_unions_shared_path_domains(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        shared = self.root / "src"
        shared.mkdir()
        target = shared / "shared.cpp"
        target.write_text("// shared\n", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('a')\n", encoding="utf-8")
        (self.root / "tests" / "test_beta.py").write_text("print('b')\n", encoding="utf-8")
        self.commit_fixture()
        target.write_text("// changed\n", encoding="utf-8")

        map_path = Path(self.tmp.name) / "shared-scope-map.json"
        self._write_semantic_map(
            map_path,
            [
                {
                    "id": "alpha",
                    "aliases": [],
                    "source_roots": ["src/shared.cpp"],
                    "entry_points": ["src/shared.cpp"],
                    "owners": [{"path": "src/shared.cpp"}],
                    "ctests": [],
                    "checks": ["tests/test_alpha.py"],
                    "lanista_scenarios": [],
                    "relations": [],
                },
                {
                    "id": "beta",
                    "aliases": [],
                    "source_roots": ["src/shared.cpp"],
                    "entry_points": ["src/shared.cpp"],
                    "owners": [{"path": "src/shared.cpp"}],
                    "ctests": [],
                    "checks": ["tests/test_beta.py"],
                    "lanista_scenarios": [],
                    "relations": [],
                },
            ],
            ["src", "tests"],
        )

        plan, warnings = cli.verify_plan(
            self.root, str(map_path), paths=["src/shared.cpp"]
        )

        self.assertEqual(
            sorted(item["name"] for item in plan),
            ["test_alpha", "test_beta"],
        )
        self.assertTrue(any("unioned" in warning for warning in warnings))

    def test_task_scoped_verify_uses_v2_map_after_unrelated_head_move(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        check = self.root / "tests" / "test_alpha.py"
        check.write_text("print('a')\n", encoding="utf-8")
        self.commit_fixture()

        map_path = Path(self.tmp.name) / "head-move-verify-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": [],
                "source_roots": ["src/owner.cpp"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": [],
                "relations": [],
            }],
            ["src", "tests/test_alpha.py"],
        )

        (self.root / "unrelated.txt").write_text("new head\n", encoding="utf-8")
        self.commit_fixture()

        plan, warnings = cli.verify_plan(
            self.root, str(map_path), paths=["src/owner.cpp"]
        )

        self.assertEqual([item["name"] for item in plan], ["test_alpha"])
        self.assertFalse(any("Map ignored" in warning for warning in warnings))

    def test_task_scoped_verify_ignores_unrelated_dirty_domain(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "alpha.cpp").write_text("// alpha\n", encoding="utf-8")
        (src / "beta.cpp").write_text("// beta\n", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('a')\n", encoding="utf-8")
        (self.root / "tests" / "test_beta.py").write_text("print('b')\n", encoding="utf-8")
        self.commit_fixture()

        (src / "alpha.cpp").write_text("// alpha changed\n", encoding="utf-8")
        (src / "beta.cpp").write_text("// beta changed\n", encoding="utf-8")
        map_path = Path(self.tmp.name) / "scope-map.json"
        self._write_semantic_map(
            map_path,
            [
                {
                    "id": "alpha",
                    "aliases": [],
                    "source_roots": ["src/alpha.cpp"],
                    "entry_points": ["src/alpha.cpp"],
                    "owners": [{"path": "src/alpha.cpp"}],
                    "ctests": [],
                    "checks": ["tests/test_alpha.py"],
                    "lanista_scenarios": [],
                    "relations": [],
                },
                {
                    "id": "beta",
                    "aliases": [],
                    "source_roots": ["src/beta.cpp"],
                    "entry_points": ["src/beta.cpp"],
                    "owners": [{"path": "src/beta.cpp"}],
                    "ctests": [],
                    "checks": ["tests/test_beta.py"],
                    "lanista_scenarios": [],
                    "relations": [],
                },
            ],
            ["src", "tests"],
        )

        plan, _warnings = cli.verify_plan(
            self.root, str(map_path), paths=["src/alpha.cpp"]
        )

        self.assertEqual([item["name"] for item in plan], ["test_alpha"])
        self.assertEqual(
            plan[0]["selectionReasons"][0]["path"], "src/alpha.cpp"
        )

    def test_broad_verify_never_reports_completion_ready(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('ok')\n", encoding="utf-8")
        self.commit_fixture()
        (self.root / "tests" / "test_alpha.py").write_text("print('changed')\n", encoding="utf-8")

        ns = type("Args", (), {
            "command": "verify",
            "root": str(self.root),
            "dry_run": False,
        })()
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["scope"]["kind"], "dirty-tree")
        self.assertFalse(payload["data"]["completionReady"])

    def test_scoped_verify_completion_ready_requires_real_execution(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('ok')\n", encoding="utf-8")
        self.commit_fixture()
        owner.write_text("// changed\n", encoding="utf-8")

        map_path = Path(self.tmp.name) / "completion-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": [],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": [],
                "relations": [],
            }],
            ["src", "tests"],
        )

        def args(dry_run: bool):
            return type("Args", (), {
                "command": "verify",
                "root": str(self.root),
                "map": str(map_path),
                "paths": ["src/owner.cpp"],
                "dry_run": dry_run,
            })()

        dry = cli.dispatch(args(True))
        run = cli.dispatch(args(False))

        self.assertTrue(dry["ok"])
        self.assertFalse(dry["data"]["completionReady"])
        self.assertEqual(dry["data"]["scope"]["paths"], ["src/owner.cpp"])
        self.assertTrue(run["ok"])
        self.assertTrue(run["data"]["completionReady"])
        self.assertEqual(run["data"]["checks"][0]["exitCode"], 0)

    def test_context_for_task_returns_bounded_fresh_domain_packet(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "context-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "ratings-reviews",
                "display_name": "Ratings & Reviews",
                "aliases": ["ratings", "reviews"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{
                    "name": "RatingsOwner",
                    "path": "src/owner.cpp",
                    "confidence": "observed",
                    "responsibility": "Canonical ratings owner.",
                    "provenance": [{"path": "src/owner.cpp"}],
                }],
                "ctests": [],
                "checks": [],
                "lanista_scenarios": [],
                "context": [{
                    "scope": "preflight",
                    "path": "arcs/49/CURRENT-STATUS.md",
                    "role": "arc-status",
                    "arc_id": "49",
                    "authority": "binding",
                }],
                "platform_constraints": ["Windows runtime proof required."],
                "relations": [],
            }],
            ["src"],
        )

        packet = cli.context_for_task(
            self.root,
            str(map_path),
            "Fix ratings opening for invalid title IDs",
        )

        self.assertEqual(packet["freshness"]["state"], "FRESH")
        self.assertEqual(packet["domains"][0]["id"], "ratings-reviews")
        self.assertEqual(packet["domains"][0]["owners"][0]["name"], "RatingsOwner")
        self.assertEqual(packet["activeArcs"][0]["id"], "49")
        self.assertIn("Windows runtime proof required.", packet["knownConstraints"])

    def test_context_for_task_record_run_writes_reloadable_receipt(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        check = self.root / "tests" / "test_alpha.py"
        check.write_text("print('ok')\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "run-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "display_name": "Alpha",
                "aliases": ["alpha"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": [],
                "context": [],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src", "tests"],
        )

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "--map", str(map_path),
            "context-for-task",
            "Fix alpha",
            "--path", "src/owner.cpp",
            "--record-run",
        ])
        payload = cli.dispatch(ns)

        run_id = payload["data"]["runId"]
        receipt_path = Path(payload["data"]["receiptPath"])
        self.assertRegex(run_id, r"^run_[0-9a-f]{32}$")
        self.assertEqual(
            receipt_path,
            self.root / "artifacts" / "harness-runs" / run_id / "run.json",
        )
        receipt = cli.load_run_receipt(receipt_path)
        self.assertEqual(receipt["schema"], "colosseum.harness.run.v1")
        self.assertEqual(receipt["runId"], run_id)
        self.assertEqual(receipt["task"], "Fix alpha")
        self.assertEqual(receipt["repo"]["root"], str(self.root.resolve()))
        self.assertEqual(receipt["repo"]["head"], cli.repo_snapshot(self.root)["head"])
        self.assertEqual(receipt["paths"], ["src/owner.cpp"])
        self.assertEqual(
            receipt["source"][0],
            {
                "path": "src/owner.cpp",
                "exists": True,
                "sha256": hashlib.sha256(owner.read_bytes()).hexdigest(),
                "sizeBytes": owner.stat().st_size,
            },
        )
        self.assertEqual(
            [item["selector"] for item in receipt["verification"]["selectedChecks"]],
            ["tests/test_alpha.py"],
        )
        self.assertIsNone(receipt["build"])
        self.assertIsNone(receipt["runtime"])
        self.assertEqual(receipt["desktopEvidence"], [])
        self.assertIsNone(receipt["result"])
        self.assertFalse(receipt["completionReady"])

    def test_context_for_task_record_run_requires_explicit_paths(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "run-scope-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha", "aliases": ["alpha"], "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"], "owners": [{"path": "src/owner.cpp"}],
                "ctests": [], "checks": [], "lanista_scenarios": [], "context": [],
                "platform_constraints": [], "relations": [],
            }],
            ["src"],
        )

        plain = cli.context_for_task(self.root, str(map_path), "alpha")
        self.assertEqual(plain["domains"][0]["id"], "alpha")

        ns = cli.build_parser().parse_args([
            "--root", str(self.root), "--map", str(map_path),
            "context-for-task", "alpha", "--record-run",
        ])
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)
        self.assertEqual(raised.exception.code, "RUN_SCOPE_REQUIRED")

    def test_context_for_task_record_run_refuses_directory_scope(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "run-directory-map.json"
        self._write_semantic_map(map_path, [], ["src"])

        with self.assertRaises(cli.HarnessError) as raised:
            cli.create_run_receipt(self.root, "alpha", ["src"], str(map_path), [], [])
        self.assertEqual(raised.exception.code, "RUN_SCOPE_NOT_FILE")

    def _make_run_and_session(self) -> tuple[dict[str, object], Path, Path]:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        owner = src / "owner.cpp"
        owner.write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()

        map_path = Path(self.tmp.name) / "bind-session-map.json"
        map_path.write_text("{}\n", encoding="utf-8")
        receipt, receipt_path = cli.create_run_receipt(
            self.root, "alpha", ["src/owner.cpp"], str(map_path), [], []
        )

        session_dir = self.root / "artifacts" / "lanista-sessions" / "fixture-session"
        session_dir.mkdir(parents=True)
        session_path = session_dir / "session.json"
        manifest = {
            "schema": "colosseum.session.v1",
            "sessionId": "20260924-015700-deadbeef",
            "tag": "fixture-session",
            "pipe": "ColosseumLanista-20260924-015700-deadbeef",
            "exe": "C:/fixture/colosseum.exe",
            "exeSha256": "a" * 64,
            "pid": 4242,
            "appDataRoot": "C:/fixture/Colosseum-dltest-fixture-session",
            "cacheRoot": "C:/cache/Colosseum-dltest-fixture-session",
        }
        session_path.write_text(json.dumps(manifest), encoding="utf-8")
        return receipt, receipt_path, session_path

    def test_bind_session_copies_lanista_identity_into_run_receipt(self) -> None:
        receipt, receipt_path, session_path = self._make_run_and_session()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "bind-session",
            "--run-id", str(receipt["runId"]),
            "--session", str(session_path),
        ])
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["runId"], receipt["runId"])
        runtime = cli.load_run_receipt(receipt_path)["runtime"]
        self.assertEqual(runtime["source"], "lanista-session-manifest")
        self.assertEqual(runtime["manifestPath"], str(session_path.resolve()))
        self.assertNotIn("manifestSha256", runtime)
        self.assertEqual(runtime["sessionId"], "20260924-015700-deadbeef")
        self.assertEqual(runtime["tag"], "fixture-session")
        self.assertEqual(runtime["exe"], "C:/fixture/colosseum.exe")
        self.assertEqual(runtime["exeSha256"], "a" * 64)
        self.assertEqual(runtime["pid"], 4242)
        self.assertEqual(runtime["pipe"], "ColosseumLanista-20260924-015700-deadbeef")
        self.assertEqual(
            runtime["appDataRoot"],
            "C:/fixture/Colosseum-dltest-fixture-session",
        )
        self.assertEqual(
            runtime["cacheRoot"],
            "C:/cache/Colosseum-dltest-fixture-session",
        )
        self.assertFalse(cli.load_run_receipt(receipt_path)["completionReady"])

    def test_bind_session_rejects_invalid_lanista_manifest(self) -> None:
        receipt, _receipt_path, session_path = self._make_run_and_session()
        broken = json.loads(session_path.read_text(encoding="utf-8"))
        broken["schema"] = "not-lanista"
        session_path.write_text(json.dumps(broken), encoding="utf-8")

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "bind-session",
            "--run-id", str(receipt["runId"]),
            "--session", str(session_path),
        ])
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "LANISTA_SESSION_INVALID")

    def test_bind_session_refuses_overwriting_existing_runtime_identity(self) -> None:
        receipt, receipt_path, session_path = self._make_run_and_session()
        args = [
            "--root", str(self.root),
            "bind-session",
            "--run-id", str(receipt["runId"]),
            "--session", str(session_path),
        ]

        cli.dispatch(cli.build_parser().parse_args(args))
        first_runtime = cli.load_run_receipt(receipt_path)["runtime"]
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(cli.build_parser().parse_args(args))

        self.assertEqual(raised.exception.code, "RUN_RUNTIME_ALREADY_BOUND")
        self.assertEqual(cli.load_run_receipt(receipt_path)["runtime"], first_runtime)

    def _make_run_for_verify(self) -> tuple[dict[str, object], Path]:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        alpha = self.root / "tests" / "test_alpha.py"
        alpha.write_text("print('alpha-frozen')\n", encoding="utf-8")
        beta = self.root / "tests" / "test_beta.py"
        beta.write_text("raise SystemExit(17)\n", encoding="utf-8")
        self.commit_fixture()

        map_path = Path(self.tmp.name) / "verify-run-map.json"
        map_path.write_text("{}\n", encoding="utf-8")
        selected = [{
            "selector": "tests/test_alpha.py",
            "kind": "script",
            "name": "test_alpha",
            "selectedTests": ["tests/test_alpha.py"],
        }]
        receipt, receipt_path = cli.create_run_receipt(
            self.root,
            "Verify alpha",
            ["src/owner.cpp"],
            str(map_path),
            selected,
            ["frozen warning"],
        )
        # This would be selected by a fresh dirty-tree verify, but must not
        # affect a run whose verification selection was already frozen.
        beta.write_text("raise SystemExit(23)\n", encoding="utf-8")
        return receipt, receipt_path

    def test_run_bound_verify_executes_only_frozen_checks_and_records_result(self) -> None:
        receipt, receipt_path = self._make_run_for_verify()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "verify",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertFalse(payload["data"]["completionReady"])
        self.assertEqual(
            [item["selector"] for item in payload["data"]["selectedChecks"]],
            ["tests/test_alpha.py"],
        )
        self.assertEqual(len(payload["data"]["checks"]), 1)
        self.assertEqual(payload["data"]["checks"][0]["exitCode"], 0)
        self.assertIn("alpha-frozen", payload["data"]["checks"][0]["stdout"])

        updated = cli.load_run_receipt(receipt_path)
        self.assertFalse(updated["completionReady"])
        self.assertIsInstance(updated["result"], dict)
        recorded = updated["result"]["verification"]
        self.assertTrue(recorded["ok"])
        self.assertEqual(recorded["scope"], {
            "kind": "run-receipt",
            "paths": ["src/owner.cpp"],
        })
        self.assertEqual(
            [item["selector"] for item in recorded["selectedChecks"]],
            ["tests/test_alpha.py"],
        )
        self.assertEqual(recorded["checks"][0]["exitCode"], 0)
        self.assertEqual(recorded["warnings"], ["frozen warning"])

    def test_run_bound_verify_dry_run_does_not_record_result(self) -> None:
        receipt, receipt_path = self._make_run_for_verify()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "verify",
            "--run-id", str(receipt["runId"]),
            "--dry-run",
        ])
        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertTrue(payload["data"]["dryRun"])
        self.assertFalse(payload["data"]["completionReady"])
        self.assertNotIn("exitCode", payload["data"]["checks"][0])
        self.assertIsNone(cli.load_run_receipt(receipt_path)["result"])

    def test_run_bound_verify_rejects_new_explicit_paths(self) -> None:
        receipt, _receipt_path = self._make_run_for_verify()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "verify",
            "--run-id", str(receipt["runId"]),
            "--path", "tests/test_beta.py",
            "--run",
        ])
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "RUN_VERIFY_SCOPE_FIXED")

    def test_run_bound_verify_persists_bounded_hashed_output(self) -> None:
        receipt, receipt_path = self._make_run_for_verify()
        (self.root / "tests" / "test_alpha.py").write_text(
            "import sys\n"
            "print('x' * 20000)\n"
            "print('y' * 10000, file=sys.stderr)\n",
            encoding="utf-8",
        )

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "verify",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])
        cli.dispatch(ns)

        recorded = cli.load_run_receipt(receipt_path)["result"]["verification"]["checks"][0]
        self.assertNotIn("stdout", recorded)
        self.assertNotIn("stderr", recorded)
        self.assertGreater(recorded["stdoutBytes"], 20000)
        self.assertGreater(recorded["stderrBytes"], 10000)
        self.assertRegex(recorded["stdoutSha256"], r"^[0-9a-f]{64}$")
        self.assertRegex(recorded["stderrSha256"], r"^[0-9a-f]{64}$")
        self.assertLessEqual(len(recorded["stdoutTail"]), 4000)
        self.assertLessEqual(len(recorded["stderrTail"]), 4000)
        self.assertTrue(recorded["stdoutTail"].rstrip().endswith("x" * 100))
        self.assertTrue(recorded["stderrTail"].rstrip().endswith("y" * 100))

    def _make_run_for_journey(self) -> tuple[dict[str, object], Path]:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()

        scenario_dir = self.root / "tests" / "lanista_scenarios"
        scenario_dir.mkdir(parents=True)
        scenario_path = scenario_dir / "alpha_journey.json"
        scenario_path.write_text(
            json.dumps({"name": "alpha_journey", "steps": [{"cmd": "ping"}]}),
            encoding="utf-8",
        )
        map_path = Path(self.tmp.name) / "journey-run-map.json"
        map_path.write_text("{}\n", encoding="utf-8")
        selected_journeys = [{
            "selector": "alpha_journey",
            "name": "alpha_journey",
            "path": "tests/lanista_scenarios/alpha_journey.json",
        }]
        receipt, receipt_path = cli.create_run_receipt(
            self.root,
            "Journey alpha",
            ["src/owner.cpp"],
            str(map_path),
            [],
            [],
            selected_journeys=selected_journeys,
        )
        receipt["runtime"] = {
            "source": "lanista-session-manifest",
            "sessionId": "20260924-020000-feedface",
            "pipe": "ColosseumLanista-20260924-020000-feedface",
        }
        receipt["result"] = {"verification": {"ok": True}}
        cli._write_run_receipt(receipt_path, receipt)
        return receipt, receipt_path

    def test_context_for_task_record_run_freezes_mapped_journey(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('ok')\n", encoding="utf-8")
        scenario_dir = self.root / "tests" / "lanista_scenarios"
        scenario_dir.mkdir(parents=True)
        (scenario_dir / "alpha_journey.json").write_text(
            json.dumps({"name": "alpha_journey", "steps": [{"cmd": "ping"}]}),
            encoding="utf-8",
        )
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "journey-context-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": ["alpha"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": ["alpha_journey"],
                "context": [],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src", "tests"],
        )

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "--map", str(map_path),
            "context-for-task", "alpha",
            "--path", "src/owner.cpp",
            "--record-run",
        ])
        payload = cli.dispatch(ns)
        receipt = cli.load_run_receipt(Path(payload["data"]["receiptPath"]))

        self.assertEqual(receipt["verification"]["selectedJourneys"], [{
            "selector": "alpha_journey",
            "name": "alpha_journey",
            "path": "tests/lanista_scenarios/alpha_journey.json",
        }])

    def _journey_choice_fixture(self) -> Path:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        (self.root / "tests" / "test_alpha.py").write_text("print('ok')\n", encoding="utf-8")
        scenario_dir = self.root / "tests" / "lanista_scenarios"
        scenario_dir.mkdir(parents=True)
        for name in ("alpha_journey", "beta_journey"):
            (scenario_dir / f"{name}.json").write_text(
                json.dumps({"name": name, "steps": [{"cmd": "ping"}]}),
                encoding="utf-8",
            )
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "journey-choice-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": ["alpha"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": ["tests/test_alpha.py"],
                "lanista_scenarios": ["alpha_journey", "beta_journey"],
                "context": [],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src", "tests"],
        )
        return map_path

    def test_record_run_multiple_journey_candidates_require_explicit_choice(self) -> None:
        map_path = self._journey_choice_fixture()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "--map", str(map_path),
            "context-for-task", "alpha",
            "--path", "src/owner.cpp",
            "--record-run",
        ])
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "JOURNEY_SELECTION_REQUIRED")
        candidates = raised.exception.details["candidates"]
        self.assertEqual(
            sorted(item["name"] for item in candidates),
            ["alpha_journey", "beta_journey"],
        )
        # The failure happens before any receipt is written.
        runs_root = self.root / "artifacts" / "harness-runs"
        self.assertFalse(runs_root.exists() or any(runs_root.glob("*/run.json")))

    def test_record_run_journey_choice_freezes_only_the_selected_journey(self) -> None:
        map_path = self._journey_choice_fixture()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "--map", str(map_path),
            "context-for-task", "alpha",
            "--path", "src/owner.cpp",
            "--record-run",
            "--journey", "beta_journey",
        ])
        payload = cli.dispatch(ns)
        receipt = cli.load_run_receipt(Path(payload["data"]["receiptPath"]))

        self.assertEqual(receipt["verification"]["selectedJourneys"], [{
            "selector": "beta_journey",
            "name": "beta_journey",
            "path": "tests/lanista_scenarios/beta_journey.json",
        }])
        self.assertEqual(
            payload["data"]["frozenJourneys"],
            receipt["verification"]["selectedJourneys"],
        )

    def test_record_run_journey_choice_must_match_a_candidate(self) -> None:
        map_path = self._journey_choice_fixture()

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "--map", str(map_path),
            "context-for-task", "alpha",
            "--path", "src/owner.cpp",
            "--record-run",
            "--journey", "gamma_journey",
        ])
        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "JOURNEY_SELECTION_INVALID")
        candidates = raised.exception.details["candidates"]
        self.assertEqual(
            sorted(item["name"] for item in candidates),
            ["alpha_journey", "beta_journey"],
        )
        runs_root = self.root / "artifacts" / "harness-runs"
        self.assertFalse(runs_root.exists() or any(runs_root.glob("*/run.json")))

    def test_run_bound_journey_uses_bound_pipe_and_records_bounded_result(self) -> None:
        receipt, receipt_path = self._make_run_for_journey()
        seen: dict[str, object] = {}

        def fake_execute(root, item, dry_run):
            seen["item"] = item
            seen["dryRun"] = dry_run
            return {
                **item,
                "dryRun": False,
                "exitCode": 0,
                "stdout": "journey-ok\n",
                "stderr": "",
            }

        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "journey",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])
        with mock.patch.object(cli, "execute", side_effect=fake_execute):
            payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertFalse(payload["data"]["completionReady"])
        item = seen["item"]
        self.assertEqual(item["mode"], "attached")
        self.assertEqual(
            item["argv"][1:3],
            ["--pipe", "ColosseumLanista-20260924-020000-feedface"],
        )
        self.assertEqual(item["argv"][-2:], [
            "run", "tests/lanista_scenarios/alpha_journey.json",
        ])
        updated = cli.load_run_receipt(receipt_path)
        self.assertTrue(updated["result"]["verification"]["ok"])
        recorded = updated["result"]["journey"]
        self.assertTrue(recorded["ok"])
        self.assertEqual(recorded["sessionId"], "20260924-020000-feedface")
        self.assertEqual(recorded["pipe"], "ColosseumLanista-20260924-020000-feedface")
        self.assertEqual(recorded["execution"]["exitCode"], 0)
        self.assertEqual(recorded["execution"]["stdoutTail"], "journey-ok\n")
        self.assertNotIn("stdout", recorded["execution"])
        self.assertFalse(updated["completionReady"])

    def test_run_bound_journey_dry_run_does_not_record_result(self) -> None:
        receipt, receipt_path = self._make_run_for_journey()
        before = cli.load_run_receipt(receipt_path)["result"]
        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "journey",
            "--run-id", str(receipt["runId"]),
            "--dry-run",
        ])
        payload = cli.dispatch(ns)
        self.assertTrue(payload["ok"])
        self.assertTrue(payload["data"]["dryRun"])
        self.assertEqual(cli.load_run_receipt(receipt_path)["result"], before)

    def test_run_bound_journey_rejects_selector_override(self) -> None:
        receipt, _receipt_path = self._make_run_for_journey()
        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "journey", "alpha_journey",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])

        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "RUN_JOURNEY_SCOPE_FIXED")

    def test_run_bound_journey_requires_bound_runtime(self) -> None:
        receipt, receipt_path = self._make_run_for_journey()
        receipt["runtime"] = None
        cli._write_run_receipt(receipt_path, receipt)
        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "journey",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])

        with self.assertRaises(cli.HarnessError) as raised:
            cli.dispatch(ns)

        self.assertEqual(raised.exception.code, "RUN_RUNTIME_NOT_BOUND")

    def _make_completion_gate_run(self) -> tuple[dict[str, object], Path]:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        test_path = self.root / "tests" / "test_alpha.py"
        test_path.write_text("print('alpha-ready')\n", encoding="utf-8")
        scenario_dir = self.root / "tests" / "lanista_scenarios"
        scenario_dir.mkdir(parents=True)
        scenario_path = scenario_dir / "alpha_journey.json"
        scenario_path.write_text(
            json.dumps({"name": "alpha_journey", "steps": [{"cmd": "ping"}]}),
            encoding="utf-8",
        )
        self.commit_fixture()

        map_path = Path(self.tmp.name) / "completion-run-map.json"
        map_path.write_text("{}\n", encoding="utf-8")
        selected_checks = [{
            "selector": "tests/test_alpha.py",
            "kind": "script",
            "name": "test_alpha",
            "selectedTests": ["tests/test_alpha.py"],
        }]
        selected_journeys = [{
            "selector": "alpha_journey",
            "name": "alpha_journey",
            "path": "tests/lanista_scenarios/alpha_journey.json",
        }]
        receipt, receipt_path = cli.create_run_receipt(
            self.root,
            "Complete alpha",
            ["src/owner.cpp"],
            str(map_path),
            selected_checks,
            [],
            selected_journeys=selected_journeys,
        )
        session_id = "20260924-040000-c0ffee42"
        receipt["runtime"] = {
            "source": "lanista-session-manifest",
            "manifestPath": str((Path(self.tmp.name) / "session.json").resolve()),
            "sessionId": session_id,
            "tag": "completion-fixture",
            "exe": "C:/fixture/colosseum.exe",
            "exeSha256": "b" * 64,
            "pid": 4242,
            "pipe": f"ColosseumLanista-{session_id}",
            "appDataRoot": "C:/fixture/Colosseum-dltest-completion-fixture",
            "cacheRoot": "C:/cache/Colosseum-dltest-completion-fixture",
        }
        screenshot = self.root / "artifacts" / "desktop" / "final.png"
        screenshot.parent.mkdir(parents=True)
        screenshot.write_bytes(b"png")
        receipt["desktopEvidence"] = [str(screenshot.resolve())]
        receipt["result"] = {
            "journey": {
                "ok": True,
                "sessionId": session_id,
                "pipe": f"ColosseumLanista-{session_id}",
                "selector": "alpha_journey",
                "path": "tests/lanista_scenarios/alpha_journey.json",
                "execution": {"exitCode": 0},
            },
        }
        cli._write_run_receipt(receipt_path, receipt)
        return receipt, receipt_path

    def test_run_bound_verify_sets_completion_ready_from_existing_receipt_evidence(self) -> None:
        receipt, receipt_path = self._make_completion_gate_run()
        ns = cli.build_parser().parse_args([
            "--root", str(self.root),
            "verify",
            "--run-id", str(receipt["runId"]),
            "--run",
        ])

        payload = cli.dispatch(ns)

        self.assertTrue(payload["ok"])
        self.assertTrue(payload["data"]["completionReady"])
        self.assertEqual(payload["data"]["completionBlockers"], [])
        updated = cli.load_run_receipt(receipt_path)
        self.assertTrue(updated["completionReady"])
        self.assertEqual(updated["completionBlockers"], [])

    def test_run_completion_gate_reports_exact_missing_evidence_classes(self) -> None:
        receipt, receipt_path = self._make_completion_gate_run()
        receipt["task"] = ""
        receipt["source"] = []
        receipt["runtime"] = None
        receipt["desktopEvidence"] = []
        receipt["result"] = None
        cli._write_run_receipt(receipt_path, receipt)

        blockers = cli.run_completion_blockers(receipt)
        codes = [item["code"] for item in blockers]

        self.assertIn("RUN_RECEIPT_INCOMPLETE", codes)
        self.assertIn("RUN_RUNTIME_NOT_BOUND", codes)
        self.assertIn("RUN_DESKTOP_EVIDENCE_MISSING", codes)
        self.assertIn("RUN_VERIFICATION_INCOMPLETE", codes)
        self.assertIn("RUN_JOURNEY_INCOMPLETE", codes)

    def test_run_completion_gate_blocks_unconfirmed_desktop_actions(self) -> None:
        receipt, _receipt_path = self._make_completion_gate_run()
        states = {
            "PENDING_VISUAL_REVIEW": "RUN_DESKTOP_ACTION_PENDING",
            "VISUAL_OUTCOME_UNCERTAIN": "RUN_DESKTOP_ACTION_UNCERTAIN",
            "VISUALLY_REJECTED": "RUN_DESKTOP_ACTION_REJECTED",
        }
        for state, expected_code in states.items():
            with self.subTest(state=state):
                action = self.root / "artifacts" / "desktop" / f"{state}.json"
                action.write_text(json.dumps({
                    "verificationState": state,
                    "visualVerdict": {
                        "PENDING_VISUAL_REVIEW": None,
                        "VISUAL_OUTCOME_UNCERTAIN": "uncertain",
                        "VISUALLY_REJECTED": "fail",
                    }[state],
                }), encoding="utf-8")
                candidate = dict(receipt)
                candidate["desktopEvidence"] = [str(action.resolve())]

                codes = [item["code"] for item in cli.run_completion_blockers(candidate)]

                self.assertIn(expected_code, codes)

    def test_run_completion_gate_blocks_failed_or_missing_confirmed_action_evidence(self) -> None:
        receipt, _receipt_path = self._make_completion_gate_run()
        screenshot = Path(receipt["desktopEvidence"][0])
        screenshot_sha = hashlib.sha256(screenshot.read_bytes()).hexdigest().upper()
        action = self.root / "artifacts" / "desktop" / "confirmed-action.json"

        def write_action(*, tool_error, after_path: Path) -> None:
            action.write_text(json.dumps({
                "verificationState": "VISUALLY_CONFIRMED",
                "visualVerdict": "pass",
                "toolError": tool_error,
                "before": {
                    "screenshotPath": str(screenshot.resolve()),
                    "screenshotSha256": screenshot_sha,
                    "screenshotBytes": screenshot.stat().st_size,
                },
                "after": {
                    "screenshotPath": str(after_path.resolve()),
                    "screenshotSha256": screenshot_sha,
                    "screenshotBytes": screenshot.stat().st_size,
                },
            }), encoding="utf-8")

        write_action(tool_error={"code": "CLICK_FAILED"}, after_path=screenshot)
        candidate = dict(receipt)
        candidate["desktopEvidence"] = [str(action.resolve())]
        codes = [item["code"] for item in cli.run_completion_blockers(candidate)]
        self.assertIn("RUN_DESKTOP_ACTION_FAILED", codes)

        missing = self.root / "artifacts" / "desktop" / "missing.png"
        write_action(tool_error=None, after_path=missing)
        codes = [item["code"] for item in cli.run_completion_blockers(candidate)]
        self.assertIn("RUN_DESKTOP_EVIDENCE_INVALID", codes)

        write_action(tool_error=None, after_path=screenshot)
        codes = [item["code"] for item in cli.run_completion_blockers(candidate)]
        self.assertFalse(any(code.startswith("RUN_DESKTOP_") for code in codes))

    def test_context_for_task_reads_bounded_mapped_preflight_authority(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()

        preflight = Path(self.tmp.name) / "preflight"
        mapped = preflight / "arcs" / "49" / "CURRENT-STATUS.md"
        mapped.parent.mkdir(parents=True)
        mapped.write_text(
            "# Arc 49\n" + "".join(f"evidence-{i:04d}\n" for i in range(1200)),
            encoding="utf-8",
        )
        (preflight / "secret.md").write_text("DO_NOT_LEAK\n", encoding="utf-8")

        map_path = Path(self.tmp.name) / "authority-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "ratings-reviews",
                "display_name": "Ratings & Reviews",
                "aliases": ["ratings"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": [],
                "lanista_scenarios": [],
                "context": [{
                    "scope": "preflight",
                    "path": "arcs/49/CURRENT-STATUS.md",
                    "role": "arc-status",
                    "arc_id": "49",
                    "authority": "binding",
                }],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src"],
        )

        packet = cli.context_for_task(
            self.root,
            str(map_path),
            "ratings",
            preflight_root=preflight,
        )

        authority = packet["domains"][0]["context"][0]["evidence"]
        self.assertTrue(authority["available"])
        self.assertTrue(authority["truncated"])
        self.assertLessEqual(
            len(authority["excerpt"].encode("utf-8")),
            cli.AUTHORITY_EXCERPT_MAX_BYTES,
        )
        self.assertLessEqual(
            len(authority["excerpt"].splitlines()),
            cli.AUTHORITY_EXCERPT_MAX_LINES,
        )
        self.assertIn("# Arc 49", authority["excerpt"])
        self.assertNotIn("DO_NOT_LEAK", json.dumps(packet))

    def test_context_for_task_preflight_unavailable_degrades_authority_only(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "unavailable-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": ["alpha"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": [],
                "lanista_scenarios": [],
                "context": [{
                    "scope": "preflight",
                    "path": "arcs/alpha/CONTRACT.md",
                    "role": "contract",
                    "authority": "binding",
                }],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src"],
        )

        packet = cli.context_for_task(self.root, str(map_path), "alpha")

        evidence = packet["domains"][0]["context"][0]["evidence"]
        self.assertFalse(evidence["available"])
        self.assertEqual(evidence["code"], "PREFLIGHT_UNAVAILABLE")

    def test_context_for_task_preflight_path_escape_is_map_invalid(self) -> None:
        (self.root / "native" / "CMakeLists.txt").write_text("", encoding="utf-8")
        (self.root / "tests" / "CMakeLists.txt").write_text("", encoding="utf-8")
        src = self.root / "src"
        src.mkdir()
        (src / "owner.cpp").write_text("// owner\n", encoding="utf-8")
        self.commit_fixture()
        preflight = Path(self.tmp.name) / "preflight"
        preflight.mkdir()
        map_path = Path(self.tmp.name) / "escape-map.json"
        self._write_semantic_map(
            map_path,
            [{
                "id": "alpha",
                "aliases": ["alpha"],
                "source_roots": ["src"],
                "entry_points": ["src/owner.cpp"],
                "owners": [{"path": "src/owner.cpp"}],
                "ctests": [],
                "checks": [],
                "lanista_scenarios": [],
                "context": [{
                    "scope": "preflight",
                    "path": "../escape.md",
                    "role": "contract",
                    "authority": "binding",
                }],
                "platform_constraints": [],
                "relations": [],
            }],
            ["src"],
        )

        with self.assertRaises(cli.HarnessError) as raised:
            cli.context_for_task(
                self.root,
                str(map_path),
                "alpha",
                preflight_root=preflight,
            )

        self.assertEqual(raised.exception.code, "MAP_INVALID")

    def test_context_for_task_refuses_map_without_semantic_freshness(self) -> None:
        (self.root / "seed.txt").write_text("seed\n", encoding="utf-8")
        self.commit_fixture()
        map_path = Path(self.tmp.name) / "old-map.json"
        map_path.write_text(json.dumps({
            "map_id": "fixture",
            "repo_basis": {
                "head": cli.repo_snapshot(self.root)["head"],
                "branch": "master",
            },
            "domains": [],
        }), encoding="utf-8")

        with self.assertRaises(cli.HarnessError) as raised:
            cli.context_for_task(self.root, str(map_path), "anything")

        self.assertEqual(raised.exception.code, "MAP_STALE")
        self.assertEqual(raised.exception.details["freshness"]["state"], "UNKNOWN")

    def test_json_emit_is_ascii_safe(self) -> None:
        payload = {
            "ok": True,
            "command": "status",
            "repo": {},
            "data": {"word": "façade"},
            "evidence": [],
            "warnings": [],
        }
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            cli.emit(payload, True)

        rendered = stream.getvalue()
        self.assertNotIn("\ufffd", rendered)
        self.assertIn("fa\\u00e7ade", rendered)
        self.assertEqual(json.loads(rendered)["data"]["word"], "façade")

    def test_emit_test_run_plain_output_shows_execution_result(self) -> None:
        payload = {
            "ok": True,
            "command": "test",
            "repo": {},
            "data": {
                "selectedTests": ["colosseum.qttest.example"],
                "kind": "ctest",
                "dryRun": False,
                "argv": ["ctest", "--test-dir", "build"],
                "exitCode": 0,
                "stdout": "Test #1: tst_example\n100% tests passed, 0 tests failed out of 19\n",
                "stderr": "",
            },
            "evidence": [],
            "warnings": [],
        }
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            cli.emit(payload, False)

        rendered = stream.getvalue()
        self.assertIn("result: exit=0 (passed)", rendered)
        self.assertIn("summary: 100% tests passed, 0 tests failed out of 19", rendered)

    def test_emit_test_run_plain_output_marks_failure(self) -> None:
        payload = {
            "ok": True,
            "command": "test",
            "repo": {},
            "data": {
                "selectedTests": ["colosseum.qttest.example"],
                "kind": "ctest",
                "dryRun": False,
                "argv": ["ctest", "--test-dir", "build"],
                "exitCode": 8,
                "stdout": "50% tests passed, 1 tests failed out of 2\n",
                "stderr": "",
            },
            "evidence": [],
            "warnings": [],
        }
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            cli.emit(payload, False)

        rendered = stream.getvalue()
        self.assertIn("result: exit=8 (failed)", rendered)
        self.assertIn("summary: 50% tests passed, 1 tests failed out of 2", rendered)

    def test_emit_test_dry_run_plain_output_has_no_result_line(self) -> None:
        payload = {
            "ok": True,
            "command": "test",
            "repo": {},
            "data": {
                "selectedTests": ["colosseum.qttest.example"],
                "kind": "ctest",
                "dryRun": True,
                "argv": ["ctest", "--test-dir", "build"],
            },
            "evidence": [],
            "warnings": [],
        }
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            cli.emit(payload, False)

        self.assertNotIn("result:", stream.getvalue())

    def test_emit_journeys_lists_discoverable_scenario_names(self) -> None:
        payload = {
            "ok": True,
            "command": "journeys",
            "repo": {},
            "data": {
                "count": 3,
                "validCount": 2,
                "invalidCount": 1,
                "journeys": [
                    {"name": "ratings-reviews-frieren-production", "valid": True},
                    {"name": "app-home", "valid": True},
                    {
                        "name": "broken-scenario",
                        "valid": False,
                        "error": {"code": "SCENARIO_INVALID"},
                    },
                ],
            },
            "evidence": [],
            "warnings": [],
        }
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            cli.emit(payload, False)

        rendered = stream.getvalue()
        self.assertIn("ratings-reviews-frieren-production", rendered)
        self.assertIn("app-home", rendered)
        self.assertIn("broken-scenario [invalid: SCENARIO_INVALID]", rendered)

    def test_runner_summary_line_prefers_last_ctest_summary(self) -> None:
        stdout = (
            "Test #1: first\n25% tests passed, 3 tests failed out of 4\n"
            "Test #2: second\n100% tests passed, 0 tests failed out of 4\n"
        )
        self.assertEqual(
            cli.runner_summary_line(stdout),
            "100% tests passed, 0 tests failed out of 4",
        )
        self.assertEqual(cli.runner_summary_line("no summary here"), "")

    def test_resolve_journey_accepts_repo_relative_scenario_path(self) -> None:
        scenario_dir = self.root / "tests" / "lanista_scenarios"
        scenario_dir.mkdir(parents=True, exist_ok=True)
        (scenario_dir / "alpha-journey.json").write_text(
            json.dumps({"name": "alpha-journey", "steps": []}), encoding="utf-8"
        )

        resolved = cli.resolve_journey(
            self.root, "tests/lanista_scenarios/alpha-journey.json"
        )

        self.assertEqual(resolved["name"], "alpha-journey")
        self.assertEqual(resolved["path"], "tests/lanista_scenarios/alpha-journey.json")


if __name__ == "__main__":
    unittest.main()
