"""Cache paths must be canonical before actions/cache's glob validation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/android/prepare_cache_paths.sh"
VARIABLES = ("COLOSSEUM_ANDROID_TOOLCHAIN", "COLOSSEUM_ANDROID_DEPS")


class AndroidCachePathsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="android-cache-paths-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "github-env"
        self.output.write_text("EXISTING=value\n")
        self.step_output = self.root / "github-output"
        self.step_output.write_text("existing=value\n")
        self.env = {key: value for key, value in os.environ.items()
                    if key not in (*VARIABLES, "GITHUB_ENV", "GITHUB_OUTPUT", "ANDROID_SDK_ROOT")}
        self.env.update({"GITHUB_ENV": str(self.output), "GITHUB_OUTPUT": str(self.step_output),
                         VARIABLES[0]: str(self.root / "work/Colosseum/Colosseum/../colosseum-android"),
                         VARIABLES[1]: str(self.root / "work/Colosseum/Colosseum/../colosseum-android-deps")})

    def run_script(self):
        return subprocess.run(["bash", str(SCRIPT)], env=self.env, capture_output=True, text=True)

    def test_ci_shaped_paths_are_normalized_without_creating_directories(self):
        result = self.run_script()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("EXISTING=value\n" + "".join(
            f"{name}={Path(self.env[name]).resolve()}\n" for name in VARIABLES)
            + f"ANDROID_SDK_ROOT={Path(self.env[VARIABLES[0]]).resolve()}/android-sdk\n", self.output.read_text())
        self.assertEqual("existing=value\n" + "".join(
            f"{key}={Path(self.env[name]).resolve()}\n"
            for key, name in zip(("toolchain", "deps"), VARIABLES))
            + f"sdk={Path(self.env[VARIABLES[0]]).resolve()}/android-sdk\n", self.step_output.read_text())
        self.assertFalse((self.root / "work").exists())

    def test_inherited_sdk_root_is_the_cache_and_installer_root(self):
        self.env["ANDROID_SDK_ROOT"] = "/usr/local/lib/android/sdk/../sdk"
        result = self.run_script()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("sdk=/usr/local/lib/android/sdk\n", self.step_output.read_text())
        self.assertIn("ANDROID_SDK_ROOT=/usr/local/lib/android/sdk\n", self.output.read_text())

    def test_invalid_inherited_sdk_root_fails_before_writing(self):
        for value in ("/", "/tmp/..", "relative/sdk", "/tmp/sdk\nINJECTED=value", "/tmp/sdk\r"):
            with self.subTest(value=value):
                self.env["ANDROID_SDK_ROOT"] = value
                result = self.run_script()
                self.assertNotEqual(0, result.returncode)
                self.assertEqual("EXISTING=value\n", self.output.read_text())
                self.assertEqual("existing=value\n", self.step_output.read_text())

    def test_existing_outside_checkout_cache_contents_are_preserved(self):
        for name in VARIABLES:
            prefix = Path(self.env[name]).resolve()
            prefix.mkdir(parents=True)
            (prefix / "cached-tool").write_bytes(b"keep existing cache")
        result = self.run_script()
        self.assertEqual(0, result.returncode, result.stderr)
        for name in VARIABLES:
            self.assertEqual(b"keep existing cache", (Path(self.env[name]).resolve() / "cached-tool").read_bytes())

    def test_missing_required_variables_fail_without_partial_env_write(self):
        for name in (*VARIABLES, "GITHUB_ENV", "GITHUB_OUTPUT"):
            with self.subTest(name=name):
                value = self.env.pop(name)
                result = self.run_script()
                self.env[name] = value
                self.assertNotEqual(0, result.returncode)
                self.assertIn(name, result.stderr)
                self.assertEqual("EXISTING=value\n", self.output.read_text())
                self.assertEqual("existing=value\n", self.step_output.read_text())

    def test_invalid_prefixes_fail_without_partial_env_write(self):
        for value in ("", "/", "/tmp/..", "relative/path", "/tmp/cache\nINJECTED=value", "/tmp/cache\r"):
            with self.subTest(value=value):
                self.env[VARIABLES[1]] = value
                result = self.run_script()
                self.assertNotEqual(0, result.returncode)
                self.assertEqual("EXISTING=value\n", self.output.read_text())
                self.assertEqual("existing=value\n", self.step_output.read_text())

    def test_toolchain_cache_saves_before_later_build_failures(self):
        workflow = (ROOT / ".github/workflows/android.yml").read_text()
        restore = workflow.split("id: toolchain-cache", 1)[1].split("- name:", 1)[0]
        self.assertIn("uses: actions/cache/restore@v4", restore)
        save = workflow.split("- name: Save installed toolchain", 1)[1].split("- name:", 1)[0]
        self.assertIn("uses: actions/cache/save@v4", save)
        self.assertIn("if: steps.toolchain-cache.outputs.cache-hit != 'true'", save)
        self.assertLess(workflow.index("run: scripts/android/install_toolchain.sh"),
                        workflow.index("- name: Save installed toolchain"))
        self.assertLess(workflow.index("- name: Save installed toolchain"),
                        workflow.index("- name: Compile Android-selected QML"))
        self.assertIn("${{ steps.android-paths.outputs.toolchain }}/Qt", save)
        self.assertIn("${{ steps.android-paths.outputs.sdk }}/ndk/27.2.12479018", save)

    def test_workflow_normalizes_before_first_cache_action(self):
        workflow = (ROOT / ".github/workflows/android.yml").read_text()
        invocation = "run: bash scripts/android/prepare_cache_paths.sh"
        self.assertIn(invocation, workflow)
        self.assertLess(workflow.index(invocation), workflow.index("uses: actions/cache"))
        self.assertIn("python tests/test_android_cache_paths.py", workflow)
        self.assertIn("id: android-paths", workflow)
        self.assertIn("${{ steps.android-paths.outputs.toolchain }}/Qt", workflow)
        self.assertIn("${{ steps.android-paths.outputs.sdk }}/ndk/27.2.12479018", workflow)
        self.assertNotIn("${{ steps.android-paths.outputs.toolchain }}/android-sdk", workflow)
        self.assertNotIn("${{ steps.android-paths.outputs.sdk }}\n", workflow)
        self.assertEqual(2, workflow.count("path: ${{ steps.android-paths.outputs.deps }}"))
        self.assertNotIn("${{ env.COLOSSEUM_ANDROID_TOOLCHAIN }}", workflow)
        self.assertNotIn("${{ env.COLOSSEUM_ANDROID_DEPS }}", workflow)


if __name__ == "__main__":
    unittest.main()
