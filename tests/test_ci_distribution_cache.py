"""No-network checks for the CI download cache and bounded Gradle retries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class GradlePreparationTests(unittest.TestCase):
    def run_fixture(self, failures):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            template = root / 'template'
            (template / 'gradle/wrapper').mkdir(parents=True)
            (template / 'gradle/wrapper/gradle-wrapper.properties').write_text('distributionUrl=https://example.invalid/gradle.zip\n')
            (template / 'gradlew').write_text('''#!/bin/bash
[[ "$*" == "--version --no-daemon" ]] || exit 90
n=0; [[ ! -f "$TEST_COUNT" ]] || n=$(cat "$TEST_COUNT")
n=$((n+1)); echo "$n" > "$TEST_COUNT"
[[ "$n" -gt "$TEST_FAILURES" ]] || exit 42
''')
            bin_dir = root / 'bin'
            bin_dir.mkdir()
            sleep = bin_dir / 'sleep'
            sleep.write_text('#!/bin/bash\necho "$1" >> "$TEST_DELAYS"\n')
            sleep.chmod(0o755)
            env = dict(os.environ, TEST_COUNT=str(root/'count'), TEST_DELAYS=str(root/'delays'),
                       TEST_FAILURES=str(failures), PATH=str(bin_dir)+os.pathsep+os.environ['PATH'])
            result = subprocess.run(['bash',str(ROOT/'scripts/android/prepare_gradle_distribution.sh'),str(template)],env=env,capture_output=True,text=True)
            delays = (root/'delays').read_text().splitlines() if (root/'delays').exists() else []
            return result.returncode, int((root/'count').read_text()), delays

    def test_warm_cache_needs_one_invocation(self):
        self.assertEqual(self.run_fixture(0),(0,1,[]))

    def test_two_transient_failures_then_success(self):
        self.assertEqual(self.run_fixture(2),(0,3,['5','10']))

    def test_failure_stops_after_three_and_preserves_status(self):
        self.assertEqual(self.run_fixture(9),(42,3,['5','10']))

    def test_missing_template_fails_closed(self):
        result = subprocess.run(['bash',str(ROOT/'scripts/android/prepare_gradle_distribution.sh'),'/nonexistent-gradle-template'],capture_output=True)
        self.assertNotEqual(result.returncode,0)

    def test_workflow_cache_keys_and_fail_closed_guards(self):
        android = (ROOT/'.github/workflows/android.yml').read_text()
        desktop = (ROOT/'.github/workflows/desktop-ci.yml').read_text()
        self.assertIn('sha256sum "$properties"',android)
        self.assertIn('path: ~/.gradle/wrapper/dists',android)
        self.assertIn('steps.gradle-wrapper.outputs.hash',android)
        self.assertLess(android.index('Prepare Gradle distribution'),android.index('Build x86_64 APK'))
        self.assertIn('runner.arch }}-${{ env.LLVM_VERSION }}-${{ env.LLVM_SHA256 }}',desktop)
        llvm = desktop.split('      - name: Install pinned LLVM clang-tidy',1)[1].split('      - name: Install native dependencies',1)[0]
        self.assertIn('$attempt -le 3',llvm)
        self.assertIn('if ($attempt -eq 3) { throw }',llvm)
        self.assertIn('LLVM installer checksum mismatch',llvm)
        self.assertIn('Cached clang-tidy does not match pinned LLVM',llvm)
        self.assertIn("steps.clang_tidy_setup.outcome == 'success'",llvm)

if __name__ == '__main__':
    unittest.main()
