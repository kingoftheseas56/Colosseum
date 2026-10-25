import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'linux-release'))
from smoke_installed_player import parse_driver_proof


class InstalledPlayerProofParsingTests(unittest.TestCase):
    def test_accepts_one_json_proof_marker(self):
        record = parse_driver_proof(
            'unrelated Qt output\nINSTALLED_PLAYER_PROOF={"status":"PASS","exit_code":0}\n')
        self.assertEqual(record, {'status': 'PASS', 'exit_code': 0})

    def test_rejects_missing_or_duplicate_proof_markers(self):
        with self.assertRaisesRegex(RuntimeError, 'found 0'):
            parse_driver_proof('no marker')
        marker = 'INSTALLED_PLAYER_PROOF={"status":"PASS"}'
        with self.assertRaisesRegex(RuntimeError, 'found 2'):
            parse_driver_proof(marker + '\n' + marker)

    def test_rejects_malformed_json_proof_marker(self):
        with self.assertRaisesRegex(RuntimeError, 'malformed'):
            parse_driver_proof('INSTALLED_PLAYER_PROOF={bad json}')


if __name__ == '__main__':
    unittest.main(verbosity=2)
