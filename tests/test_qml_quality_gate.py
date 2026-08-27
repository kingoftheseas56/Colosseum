from pathlib import Path
import tempfile
import unittest

from scripts import qml_quality_gate


class QmlQualityGateTests(unittest.TestCase):
    def test_hard_finding_fingerprint_skips_qmllint_note(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "qml"
            target = root / "player2" / "controls" / "StatsOverlay.qml"
            target.parent.mkdir(parents=True)
            target.touch()
            output = (
                f"Warning: {target}:51:5: Duplicate binding on property 'data' [duplicate-property-binding]\n"
                f"Warning: {target}:12:24: Note: previous binding on 'data' here [duplicate-property-binding]\n"
            )
            findings = qml_quality_gate.finding_fingerprints(output, root)
            self.assertEqual(
                findings,
                {"qml/player2/controls/StatsOverlay.qml|duplicate-property-binding|"
                 "Duplicate binding on property 'data'"},
            )

    def test_baseline_loader_ignores_comments_and_blank_lines(self):
        with tempfile.TemporaryDirectory() as temp:
            baseline = Path(temp) / "baseline.txt"
            baseline.write_text("# known debt\n\nqml/A.qml|alias-cycle|Alias cycle\n", encoding="utf-8")
            self.assertEqual(qml_quality_gate.load_baseline(baseline),
                             {"qml/A.qml|alias-cycle|Alias cycle"})
