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
        doc = json.loads(text)
        steps = doc["steps"]
        expects = [expect for step in steps for expect in step.get("expect", [])]
        self.assertEqual(len(steps), 22)
        self.assertTrue(any(e.get("path") == "props.mediaTransport" and e.get("op") == "==" and e.get("value") == "Downloaded" for e in expects))
        self.assertTrue(any(e.get("path") == "props.currentPlaybackUrl" and e.get("op") == "contains" and e.get("value") == ".part" for e in expects))
        self.assertTrue(any(e.get("path") == "props.currentPlaybackUrl" and e.get("op") == "matches" and str(e.get("value", "")).endswith("mp4$") for e in expects))
        self.assertNotIn("__VIDEO_URL__", text)
        player_ready_waits = [step for step in steps if step.get("payload", {}).get("object") == "player" and step.get("payload", {}).get("prop") == "playerReady" and step.get("payload", {}).get("value") is True]
        self.assertGreaterEqual(len(player_ready_waits), 2)

    def test_arriving_landing_gate_is_driver_owned_after_player_navigation(self):
        scenario = (HERE / "lanista_scenarios" / "function0008_arriving_play.json").read_text(encoding="utf-8")
        driver = (HERE / "function0008_lanista_runtime.py").read_text(encoding="utf-8")
        self.assertNotIn('"object": "downloadsPage", "prop": "liveJobCount", "value": 0', scenario)
        self.assertIn("if not paths.output_path.is_file():", driver)
        self.assertIn("download output did not land inside tagged root", driver)

    def test_arriving_navigation_uses_auto_revealed_downloads_without_toggle(self):
        path = HERE / 'lanista_scenarios' / 'function0008_arriving_play.json'
        doc = json.loads(path.read_text(encoding='utf-8'))
        steps = doc['steps']
        self.assertFalse(any(step.get('payload', {}).get('target') == 'colosseumTaskbarHomeButton' for step in steps))
        taskbar_reads = [step for step in steps if step.get('cmd') == 'qml-get' and step.get('payload', {}).get('object') == 'colosseumTaskbar']
        self.assertTrue(taskbar_reads)
        self.assertTrue(any(expect.get('path') == 'props.open' and expect.get('op') == '==' and expect.get('value') is True for expect in taskbar_reads[0].get('expect', [])))

    def test_arriving_driver_holds_download_until_playback_request_or_scenario_exit(self):
        import sys
        import tempfile
        import time
        from types import SimpleNamespace
        import function0008_lanista_runtime as runtime

        with tempfile.TemporaryDirectory() as temp_dir:
            done = pathlib.Path(temp_dir) / 'done.txt'

            class NeverPlayback:
                def wait(self, timeout=None):
                    time.sleep(min(float(timeout or 0), 0.01))
                    return False

            class ReleaseProbe:
                def __init__(self):
                    self.released_before_exit = False
                def set(self):
                    self.released_before_exit = not done.exists()

            release = ReleaseProbe()
            fixture = SimpleNamespace(playback_request_seen=NeverPlayback(), release_download=release)
            command = [sys.executable, '-c', f"import time,pathlib; time.sleep(0.2); pathlib.Path(r\'{done}\').write_text(\'done\')"]
            code, _, _, _ = runtime.run_process(command, arriving=True, fixture=fixture, timeout_s=15)
            self.assertEqual(code, 0)
            self.assertFalse(release.released_before_exit)

    def test_runtime_wrapper_budget_covers_declared_session_waits(self):
        import function0008_lanista_runtime as runtime

        scenario = HERE / 'lanista_scenarios' / 'function0008_arriving_play.json'
        self.assertTrue(hasattr(runtime, 'scenario_timeout_seconds'))
        budget = runtime.scenario_timeout_seconds(scenario, ready_ms=120000)
        self.assertGreaterEqual(budget, 420)

    def test_runtime_manifest_has_no_stale_author_ownership(self):
        driver = (HERE / "function0008_lanista_runtime.py").read_text(encoding="utf-8")
        self.assertNotIn('"integratedRuntimeStillOwnedByClaude": True', driver)
        self.assertNotIn('result["currentDd576634Prerequisite"]', driver)
    def test_fixture_contract_mentions_required_headers_and_negative_sentinel(self):
        text = (HERE / "function0008_loopback_fixture.py").read_text(encoding="utf-8")
        for header in ("Referer", "Origin", "X-Function-0008"):
            self.assertIn(header, text)
        self.assertIn("X-Function-0008-Negative-Control", text)

    def test_fixture_mp4_is_fast_start_before_disk_first_frontier(self):
        from function0008_loopback_fixture import FAST_PREFIX_BYTES, build_fixture_mp4

        media = build_fixture_mp4()
        self.assertEqual(media[4:8], b"ftyp")
        self.assertIn(b"moov", media[:FAST_PREFIX_BYTES])
        self.assertIn(b"mdat", media[:FAST_PREFIX_BYTES])
        self.assertGreater(len(media), FAST_PREFIX_BYTES)


if __name__ == "__main__":
    unittest.main()
