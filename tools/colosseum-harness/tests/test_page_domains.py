from __future__ import annotations

import json
import copy
import subprocess
import sys
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parents[1]
ROOT = HARNESS.parents[1]
sys.path.insert(0, str(HARNESS))
sys.path.insert(0, str(HARNESS / "intelligence"))

from harness.context import context_for_task
from validate_map import validate_map

MAP = HARNESS / "intelligence/colosseum-map.json"


class PageDomainsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.doc = json.loads(MAP.read_text(encoding="utf-8"))
        cls.domains = {item["id"]: item for item in cls.doc["domains"]}

    def test_page_inventory_and_live_references(self) -> None:
        pages = [item for item in self.doc["domains"] if item.get("kind") == "page"]
        self.assertEqual(len(pages), 59)  # 57 inventory rows plus five native-by-design groups, minus Feria (2) and the retired Ratings page (2026-09-28).
        for domain in pages:
            with self.subTest(domain=domain["id"]):
                self.assertTrue(domain["qml_files"])
                self.assertEqual(domain["host"]["path"], "qml/Main.qml")
                self.assertIn(domain["host"]["anchor"], (ROOT / "qml/Main.qml").read_text(encoding="utf-8"))
                for path in domain["qml_files"]:
                    self.assertTrue((ROOT / path).is_file(), path)
                self.assertIn(domain["owners"][0]["path"], domain["qml_files"])

        required = {
            "page-top-bar": "qml/TopBar.qml",
            "page-home": "qml/WorldPage.qml",
            "page-search": "qml/SearchSurface.qml",
            "page-library-page": "qml/LibraryPage.qml",
            "page-tankoyomi-configuration-page": "qml/TankoyomiConfigurationPage.qml",
            "page-biblio-book": "qml/BiblioBook.qml",
            "page-d-c-a-u-universe-page": "qml/DCAUUniversePage.qml",
            "page-tracker-sync-center-page": "qml/TrackerSyncCenterPage.qml",
            "page-account-data-privacy-page": "qml/account/AccountProfilePage.qml",
            "page-book-reader": "qml/reader2/ReaderShell.qml",
            "page-player-2": "qml/player2/controls/ShortcutsSheet.qml",
        }
        for domain_id, path in required.items():
            self.assertIn(path, self.domains[domain_id]["qml_files"])

        extensions = self.domains["page-extensions-page"]
        for path in (
            "qml/ExtensionsHouseHero.qml",
            "qml/ExtensionsSeeAllPage.qml",
            "qml/ExtensionsSetupSheet.qml",
            "qml/ExtensionsStoreCard.qml",
            "qml/ExtensionsStoreRail.qml",
        ):
            self.assertIn(path, extensions["qml_files"])
        self.assertIn("qml/ExtensionsStoreApi.js", extensions["source_roots"])

        connections = self.domains["page-tracker-sync-center-page"]
        self.assertIn(
            "tests/lanista_scenarios/trakt_via_stremio_connections.json",
            connections["lanista_scenarios"],
        )

    def test_map_uses_registered_verification_and_is_fresh(self) -> None:
        result = validate_map(self.doc, ROOT)
        self.assertEqual(result["errors"], [])
        self.assertTrue(result["ok"])
        from harness.intelligence import map_freshness
        self.assertEqual(map_freshness(ROOT, self.doc)["state"], "FRESH")

    def test_context_for_page_returns_files_host_owner_and_verification(self) -> None:
        examples = {
            "Theatre Library": ("page-library-page", "qml/LibraryPage.qml"),
            "Downloads": ("page-downloads-page", "qml/DownloadsPage.qml"),
            "Account Center (frame)": ("page-account-center", "qml/account/AccountCenter.qml"),
            "Player 2": ("page-player-2", "qml/player2host/Player2Page.qml"),
        }
        for task, (domain_id, path) in examples.items():
            with self.subTest(task=task):
                result = context_for_task(ROOT, str(MAP), task)
                self.assertEqual([domain["id"] for domain in result["domains"]], [domain_id])
                domain = result["domains"][0]
                self.assertIn(path, domain["qmlFiles"])
                self.assertEqual(domain["host"]["path"], "qml/Main.qml")
                self.assertTrue(domain["owners"])
                self.assertIn("verification", domain)
        downloads = context_for_task(ROOT, str(MAP), "Downloads")["domains"][0]
        self.assertIn({"kind": "ctest", "selector": "colosseum.qttest.local_downloads_projection"}, downloads["verification"])
        self.assertIn({"kind": "journey", "selector": "tests/lanista_scenarios/keyboard_universe_utilities_completion.json"}, downloads["verification"])

    def test_current_v117_architecture_and_probe_domains(self) -> None:
        self.assertNotIn("webui", self.domains)
        scopes = self.doc["repo_basis"]["semantic_worktree"]["watch_scopes"]
        self.assertEqual(
            scopes,
            ["qml", "native", "server", "extensions", "tests", "data/title-identities-v1.json"],
        )

        account_sync = self.domains["account-sync"]
        self.assertIn(
            "colosseum.qttest.stremio_sync",
            {item["name"] for item in account_sync["ctests"]},
        )

        smoothness = self.domains["smoothness-probes"]
        self.assertEqual(smoothness["entry_points"], ["native/main.cpp"])
        self.assertEqual(
            [item["name"] for item in smoothness["ctests"]],
            ["colosseum.qttest.qml_smoothness_probes"],
        )
        self.assertIn("native/FrameTimingProbe.h", smoothness["source_roots"])
        self.assertIn("native/net/PosterTimingProbe.cpp", smoothness["source_roots"])

    def test_page_host_and_planned_paths_are_validated(self) -> None:
        bad_host = copy.deepcopy(self.doc)
        bad_host_domain = next(item for item in bad_host["domains"] if item["id"] == "page-audiobook-session")
        bad_host_domain["host"]["anchor"] = "missing Main.qml route"
        self.assertTrue(any("host.anchor" in error for error in validate_map(bad_host, ROOT)["errors"]))

        bad_path = copy.deepcopy(self.doc)
        bad_path["domains"][0]["planned_paths"] = ["../elsewhere"]
        self.assertTrue(any("planned_paths" in error for error in validate_map(bad_path, ROOT)["errors"]))

    def test_run_py_contract_routes_a_page(self) -> None:
        result = subprocess.run(
            [sys.executable, str(HARNESS / "run.py"), "--root", str(ROOT), "--map", str(MAP),
             "context-for-task", "Tankoyomi configuration", "--json"],
            capture_output=True, text=True, check=True,
        )
        payload = json.loads(result.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["domains"][0]["id"], "page-tankoyomi-configuration-page")


if __name__ == "__main__":
    unittest.main()
