"""Keep normal APKs and destructive emulator proof outputs isolated.

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
    def test_normal_and_proof_variants_use_distinct_generated_state_roots(self):
        production = root(WORKFLOW[:WORKFLOW.index('\njobs:')])
        proof = root(step('Build instrumented Nuvio proof APK for emulator only'))
        self.assertNotEqual(production, proof)
        self.assertTrue(all(value.startswith('${{ github.workspace }}/native/build-android')
                            for value in (production, proof)))
        for name in ('Build x86_64 APK', 'Build arm64-v8a APK'):
            self.assertNotIn('COLOSSEUM_ANDROID_BUILD:', step(name))  # inherit normal root
            self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID=ON', step(name))
            self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID_PROOF=OFF', step(name))
            self.assertIn('COLOSSEUM_TMDB_KEY: ${{ secrets.COLOSSEUM_TMDB_KEY }}', step(name))

    def test_proof_meta_object_contains_both_instrumentation_signals(self):
        proof = step('Build instrumented Nuvio proof APK for emulator only')
        self.assertIn('-DCOLOSSEUM_NUVIO_ANDROID_PROOF=ON', proof)
        for signal in ('proofWorkerStarted', 'proofWorkerFinished'):
            self.assertIn("grep -q '" + signal + "'", proof)
        self.assertIn('apk-nuvio-proof/proof-moc_NuvioSourceResolver.cpp', proof)
        self.assertIn('COLOSSEUM_TMDB_KEY: ${{ secrets.COLOSSEUM_TMDB_KEY }}', proof)

    def test_proof_build_transport_and_job_require_dispatch_opt_in(self):
        trigger = WORKFLOW.split('\non:\n', 1)[1].split('\npermissions:', 1)[0]
        self.assertIn('  workflow_dispatch:', trigger)
        self.assertNotIn('  push:', trigger)
        self.assertRegex(trigger, r'(?s)nuvio_proof:.*?type: boolean\s+default: false')
        proof = step('Build instrumented Nuvio proof APK for emulator only')
        self.assertIn('if: inputs.nuvio_proof', proof)
        self.assertRegex(WORKFLOW, r'(?m)^  nuvio-emulator-proof:\n    if: inputs\.nuvio_proof$')
        transport = re.search(
            r'(?m)^      - uses: actions/upload-artifact@v\d+\n'
            r'        if: inputs\.nuvio_proof\n        with:\n'
            r'          name: nuvio-proof-x86_64-debug-apk\n'
            r'          path: apk-nuvio-proof\n'
            r'          retention-days: 1$', WORKFLOW)
        self.assertIsNotNone(transport)


if __name__ == '__main__':
    unittest.main()
