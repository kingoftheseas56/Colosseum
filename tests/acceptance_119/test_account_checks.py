"""Fail-closed evidence parsing, including deliberate false-green controls."""
import json
import tempfile
import unittest
from pathlib import Path

import account_checks as checks


class EvidenceTests(unittest.TestCase):
    def event(self, action, test='Case'):
        return json.dumps({'Action': action, 'Test': test})

    def test_go_requires_each_named_test_to_pass(self):
        status, _ = checks.go_verdict(self.event('pass'), 0, ['Case', 'Absent'])
        self.assertEqual(status, 'BLOCKED')

    def test_go_skip_is_never_pass(self):
        status, _ = checks.go_verdict(self.event('skip'), 0, ['Case'])
        self.assertEqual(status, 'BLOCKED')

    def test_go_child_skip_is_never_pass(self):
        log = self.event('skip', 'Case/required_child') + '\n' + self.event('pass')
        self.assertEqual(checks.go_verdict(log, 0, ['Case'])[0], 'BLOCKED')

    def test_go_failure_or_bad_exit_cannot_be_green(self):
        self.assertEqual(checks.go_verdict(self.event('fail'), 0, ['Case'])[0], 'FAIL')
        self.assertEqual(checks.go_verdict(self.event('pass'), 2, ['Case'])[0], 'FAIL')

    def test_go_pass_is_evidenced(self):
        self.assertEqual(checks.go_verdict(self.event('pass'), 0, ['Case'])[0], 'PASS')

    def test_qt_requires_named_pass_and_no_skips(self):
        xml = '<TestCase><TestFunction name="Case"><Incident type="pass"/></TestFunction></TestCase>'
        self.assertEqual(checks.qt_verdict(xml, 0, ['Case'])[0], 'PASS')
        self.assertEqual(checks.qt_verdict(xml, 0, ['Other'])[0], 'BLOCKED')
        self.assertEqual(checks.qt_verdict(xml.replace('pass', 'skip'), 0, ['Case'])[0], 'BLOCKED')
        self.assertEqual(checks.qt_verdict(xml.replace('pass', 'fail'), 0, ['Case'])[0], 'FAIL')
        self.assertEqual(checks.qt_verdict('not XML', 0, ['Case'])[0], 'FAIL')

    def test_scenarios_require_valid_protocol_replies(self):
        folder = Path(__file__).resolve().parents[1] / 'lanista_scenarios/acceptance_119'
        for name in ['account_local_ui.json', 'account_local_restart_ui.json', 'account_trackers_linux_ui.json']:
            scenario = json.loads((folder / name).read_text())
            commands = [step['cmd'] for step in scenario['steps']]
            self.assertIn('ping', commands)
            self.assertIn('get-state', commands)
            for step in scenario['steps']:
                self.assertIn({'path': 'type', 'op': '==', 'value': 'reply'}, step['expect'])

    def test_local_account_selectors_exist_in_pinned_presentation(self):
        source = Path(__file__).resolve().parents[2]
        qml = '\n'.join((source / name).read_text() for name in [
            'qml/Main.qml', 'qml/TopBar.qml', 'qml/account/AccountFlyout.qml',
            'qml/account/AccountWelcome.qml', 'qml/account/AccountSignIn.qml'])
        for name in ['account_local_ui.json', 'account_local_restart_ui.json']:
            scenario = json.loads((source / 'tests/lanista_scenarios/acceptance_119' / name).read_text())
            for step in scenario['steps']:
                payload = step.get('payload', {})
                target = payload.get('object', payload.get('target', ''))
                if target.startswith(('account', 'colosseumTopbar')):
                    self.assertIn('objectName: "' + target + '"', qml, target)

    def test_boot_wait_covers_offline_fallback_and_restart_has_welcome_choice(self):
        folder = Path(__file__).resolve().parents[1] / 'lanista_scenarios/acceptance_119'
        for name in ['account_local_ui.json', 'account_local_restart_ui.json', 'account_trackers_linux_ui.json']:
            scenario = json.loads((folder / name).read_text())
            waits = [step['payload'] for step in scenario['steps']
                     if step.get('cmd') == 'ui-wait-for'
                     and step.get('payload', {}).get('object') == 'bootSplash']
            self.assertEqual(len(waits), 1)
            self.assertGreaterEqual(waits[0]['timeout_ms'], 60000)
        restart = json.loads((folder / 'account_local_restart_ui.json').read_text())
        self.assertTrue(any(step.get('cmd') == 'ui-click'
                            and step.get('payload', {}).get('target') == 'accountWelcomeSignIn'
                            for step in restart['steps']))

    def test_subprocess_nonzero_and_forced_timeout_are_preserved(self):
        import os
        import sys
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / 'exit.txt'
            run = checks.run_command([sys.executable, '-c', 'raise SystemExit(7)'],
                                     Path(root), dict(os.environ), log, 3)
            self.assertEqual(run['exit_code'], 7)
            self.assertFalse(run['forced_kill'])
            run = checks.run_command([sys.executable, '-c', 'import time; time.sleep(30)'],
                                     Path(root), dict(os.environ), log, .15)
            self.assertTrue(run['forced_kill'])
            self.assertIsNotNone(run['exit_code'])


if __name__ == '__main__':
    unittest.main()
