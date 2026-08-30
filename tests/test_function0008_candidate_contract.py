import json
import pathlib
import unittest

HERE = pathlib.Path(__file__).resolve().parent
CANDIDATE = HERE.parent
ALLOWED = {"ping", "get-state", "qml-get", "ui-click", "ui-wait-for", "grab"}


class CandidateContractTest(unittest.TestCase):
    def test_qml_seams_exist_without_replacing_existing_projections(self):
        text = (CANDIDATE / "qml" / "DownloadsPage.qml").read_text(encoding="utf-8")
        self.assertIn('objectName: "downloadsPage"', text)
        self.assertEqual(text.count('property int liveJobCount: 0'), 1)
        self.assertEqual(text.count('property int attentionCount: 0'), 1)
        self.assertEqual(text.count('property var totalsMap: ({})'), 1)
        self.assertEqual(text.count('objectName: "downloadsPlayArriving_"'), 1)
        self.assertIn('"downloadsPlayArrivingGroup_"', text)
        self.assertIn('grp.modelData.single', text)
        self.assertEqual(text.count('readonly property bool diskFirstReady:'), 2)

    def test_runtime_driver_and_fixture_exist(self):
        self.assertTrue((HERE / "function0008_lanista_runtime.py").is_file())
        self.assertTrue((HERE / "function0008_loopback_fixture.py").is_file())

    def test_scenarios_use_only_arc_ledger_subset(self):
        scenario_dir = HERE / "lanista_scenarios"
        paths = sorted(scenario_dir.glob("function0008_*.json"))
        self.assertEqual(len(paths), 2)
        for path in paths:
            doc = json.loads(path.read_text(encoding="utf-8"))
            for step in doc["steps"]:
                self.assertIn(step["cmd"], ALLOWED, f"{path.name}: {step['cmd']}")

    def test_arriving_scenario_proves_disk_first_then_post_frontier_decode(self):
        path = HERE / "lanista_scenarios" / "function0008_arriving_play.json"
        text = path.read_text(encoding="utf-8")
        self.assertIn('"props.mediaTransport", "op": "==", "value": "Downloaded"', text)
        self.assertIn('"props.currentPlaybackUrl", "op": "contains", "value": ".part"', text)
        self.assertIn('"currentPlaybackUrl", "value": "__VIDEO_URL__"', text)
        self.assertGreaterEqual(text.count('"prop": "playerReady", "value": true'), 2)

    def test_fixture_contract_mentions_required_headers_and_negative_sentinel(self):
        text = (HERE / "function0008_loopback_fixture.py").read_text(encoding="utf-8")
        for header in ("Referer", "Origin", "X-Function-0008"):
            self.assertIn(header, text)
        self.assertIn("X-Function-0008-Negative-Control", text)


if __name__ == "__main__":
    unittest.main()
