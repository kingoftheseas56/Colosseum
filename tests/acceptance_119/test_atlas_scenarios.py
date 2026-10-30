"""Static scenario regressions; these do not substitute for actual GUI evidence."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = (
    ROOT / 'lanista_scenarios/acceptance_119/atlas_four_sea_journey.json',
    ROOT / 'acceptance_119/diagnostics/atlas_route_only.json',
)
REPLY = {'path': 'type', 'op': '==', 'value': 'reply'}


class AtlasScenarioContractTests(unittest.TestCase):
    def scenarios(self):
        for path in SCENARIOS:
            yield path, json.loads(path.read_text(encoding='utf-8'))['steps']

    def test_paradise_keyboard_selects_first_row_explicitly(self):
        # toggleIndex restores the previous arc, so a fresh index need not focus row 0.
        for path, steps in self.scenarios():
            with self.subTest(path=path.name):
                start = next(i for i, step in enumerate(steps)
                             if step.get('payload', {}).get('target') == 'paradiseIndexButton')
                opened, home, focused, activate = steps[start + 1:start + 5]
                self.assertEqual(opened['payload']['object'], 'paradiseIndexPanel')
                self.assertEqual(opened['payload']['prop'], 'visible')
                self.assertIs(opened['payload']['value'], True)
                self.assertEqual(home['cmd'], 'ui-keypress')
                self.assertEqual(home['payload'], {'key': 'Home'})
                self.assertIn({'path': 'pressed', 'op': '==', 'value': 'Home'}, home['expect'])
                self.assertEqual(focused['cmd'], 'ui-wait-for')
                self.assertEqual(focused['payload']['object'], 'paradiseIndexRow-reverse-mountain')
                self.assertEqual(focused['payload']['prop'], 'activeFocus')
                self.assertIs(focused['payload']['value'], True)
                self.assertGreater(focused['payload']['timeout_ms'], 0)
                self.assertEqual(activate['payload'], {'key': 'Return'})

    def test_real_focus_and_target_size_failures_remain_strict(self):
        for path, steps in self.scenarios():
            with self.subTest(path=path.name):
                focus = next(s for s in steps if s['label'] == '061 eastBlueAtlas.activeFocus reaches True')
                self.assertEqual(focus['payload']['object'], 'eastBlueAtlas')
                self.assertEqual(focus['payload']['prop'], 'activeFocus')
                self.assertIs(focus['payload']['value'], True)
                self.assertIn({'path': 'matched', 'op': '==', 'value': True}, focus['expect'])
                gate = next(s for s in steps if s.get('cmd') == 'ui-query'
                            and s.get('payload', {}).get('object') == 'finalSeaToNewWorld')
                self.assertIn({'path': 'rect.height', 'op': '>=', 'value': 44}, gate['expect'])

    def test_unique_labels_and_explicit_protocol_reply_assertions(self):
        for path, steps in self.scenarios():
            with self.subTest(path=path.name):
                self.assertEqual(len(steps), len({s['label'] for s in steps}))
                for step in steps:
                    if 'cmd' in step:
                        self.assertIn(REPLY, step['expect'], step['label'])


if __name__ == '__main__':
    unittest.main()
