"""Keep production, destructive proof and disabled size-baseline outputs isolated.

This is a CI build-graph contract; SDK compilation still verifies generated moc.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / '.github/workflows/android.yml').read_text()


def step(name):
    marker = '      - name: ' + name + '\n'
    start = WORKFLOW.index(marker) + len(marker)
    end = WORKFLOW.find('\n      - ', start)
    return WORKFLOW[start:end if end >= 0 else len(WORKFLOW)]


def root(block):
    return re.search(r'^\s*COLOSSEUM_ANDROID_BUILD: (.+)$', block, re.M)[1]


class BuildVariantTests(unittest.TestCase):
    def test_variants_have_three_distinct_generated_state_roots(self):
        production = root(WORKFLOW[:WORKFLOW.index('\njobs:')])
        proof = root(step('Build instrumented Nuvio proof APK for emulator only'))
        baseline = root(step('Build Nuvio-disabled APK size baselines'))
        self.assertEqual(3, len({production, proof, baseline}))
        self.assertTrue(all(value.startswith('${{ github.workspace }}/native/build-android')
                            for value in (production, proof, baseline)))
        for name in ('Build x86_64 APK', 'Build arm64-v8a APK'):
            self.assertNotIn('COLOSSEUM_ANDROID_BUILD:', step(name))  # inherit normal root
            self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID_PROOF=OFF', step(name))

    def test_proof_meta_object_contains_both_instrumentation_signals(self):
        proof = step('Build instrumented Nuvio proof APK for emulator only')
        self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID_PROOF=ON', proof)
        for signal in ('proofWorkerStarted', 'proofWorkerFinished'):
            self.assertIn("grep -q '" + signal + "'", proof)
        self.assertIn('apk-nuvio-proof/proof-moc_NuvioSourceResolver.cpp', proof)
        self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID=OFF', step('Build Nuvio-disabled APK size baselines'))


if __name__ == '__main__':
    unittest.main()
