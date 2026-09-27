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
        self.assertEqual(len(pages), 62)  # 57 inventory rows plus five native-by-design groups.
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
            "page-ratings-reviews-host": "qml/ratingsreviews/RatingsReviewsHost.qml",
            "page-account-data-privacy-page": "qml/account/AccountProfilePage.qml",
            "page-portico-account-history-slice-view": "qml/feria/PorticoHostView.qml",
            "page-book-reader": "qml/reader2/ReaderShell.qml",
            "page-player-2": "qml/player2/controls/ShortcutsSheet.qml",
        }
        for domain_id, path in required.items():
            self.assertIn(path, self.domains[domain_id]["qml_files"])

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
            "Ratings & Reviews (+ provider detail)": ("page-ratings-reviews-host", "qml/ratingsreviews/RatingsReviewsProviderDetail.qml"),
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

    def test_webui_reports_branch_only_paths(self) -> None:
        result = context_for_task(ROOT, str(MAP), "webui")
        self.assertEqual([domain["id"] for domain in result["domains"]], ["webui"])
        domain = result["domains"][0]
        self.assertEqual(domain["featureStatus"], "absent_on_master")
        self.assertEqual(domain["plannedPaths"], ["native/webui", "web/developer-colosseum"])
        self.assertEqual(domain["sourceRoots"], domain["plannedPaths"])
        self.assertEqual(domain["verification"], [])
        scopes = self.doc["repo_basis"]["semantic_worktree"]["watch_scopes"]
        self.assertIn("native/webui", scopes)
        self.assertIn("web/developer-colosseum", scopes)

    def test_page_host_and_planned_paths_are_validated(self) -> None:
        bad_host = copy.deepcopy(self.doc)
        bad_host["domains"][-2]["host"]["anchor"] = "missing Main.qml route"
        self.assertTrue(any("host.anchor" in error for error in validate_map(bad_host, ROOT)["errors"]))

        bad_path = copy.deepcopy(self.doc)
        bad_path["domains"][-1]["planned_paths"] = ["../elsewhere"]
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
