import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/linux-release'))
from smoke_installed_runtime import validate_proof


class RuntimeEvidenceTests(unittest.TestCase):
    def proof(self):
        return dict(phase='restart', webResult='Colosseum WebEngine qualification 130',
                    mode='localOnly', onboardingRequired=False, profileRetained=True)

    def test_complete_restart_evidence(self):
        validate_proof(self.proof(), 'restart')

    def test_wrong_page_or_failed_dom_cannot_pass(self):
        for result in ['', None, 'unrelated page']:
            record = self.proof()
            record['webResult'] = result
            with self.assertRaisesRegex(RuntimeError, 'DOM'):
                validate_proof(record, 'restart')

    def test_first_launch_evidence_cannot_stand_in_for_restart(self):
        record = self.proof()
        record['phase'] = 'first'
        with self.assertRaises(RuntimeError):
            validate_proof(record, 'restart')

    def test_reset_profile_cannot_pass(self):
        for changes in [dict(profileRetained=False), dict(mode='signedOut'), dict(onboardingRequired=True)]:
            record = self.proof()
            record.update(changes)
            with self.assertRaises(RuntimeError):
                validate_proof(record, 'restart')


if __name__ == '__main__':
    unittest.main()
