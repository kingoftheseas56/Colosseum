from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / 'scripts/installer/verify_browser_runtime.ps1'
# Required by the deployed Qt 6.11 Quick browser, independent of the checker.
FILES = (
    'QtWebEngineProcess.exe', 'Qt6WebEngineCore.dll', 'Qt6WebEngineQuick.dll',
    'Qt6WebChannel.dll', 'resources/icudtl.dat',
    'resources/qtwebengine_resources.pak', 'resources/qtwebengine_resources_100p.pak',
    'resources/qtwebengine_resources_200p.pak', 'resources/qtwebengine_devtools_resources.pak',
    'resources/v8_context_snapshot.bin', 'translations/qtwebengine_locales/en-US.pak',
    'qml/QtWebEngine/qmldir', 'qml/QtWebEngine/qtwebenginequickplugin.dll',
    'qml/QtWebChannel/qmldir', 'qml/QtWebChannel/webchannelquickplugin.dll',
)


@unittest.skipUnless(shutil.which('powershell'), 'requires Windows PowerShell')
class BrowserRuntimePackageTest(unittest.TestCase):
    def run_checker(self, runtime):
        return subprocess.run(
            ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
             str(CHECKER), '-RuntimeRoot', str(runtime)],
            capture_output=True, text=True, timeout=30,
        )

    def test_incomplete_browser_payloads_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix='feria-runtime-package-') as folder:
            runtime = Path(folder)
            for name in FILES:
                target = runtime / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'runtime sentinel')
            result = self.run_checker(runtime)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('BROWSER_RUNTIME_OK', result.stdout)
            for name in FILES:
                with self.subTest(missing=name):
                    target = runtime / name
                    target.unlink()
                    result = self.run_checker(runtime)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('BROWSER_RUNTIME_FAIL', result.stderr)
                    self.assertIn(name.replace('/', '\\'), result.stderr.replace('/', '\\'))
                    target.write_bytes(b'runtime sentinel')
            target = runtime / 'Qt6WebEngineCore.dll'
            target.write_bytes(b'')
            result = self.run_checker(runtime)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('empty runtime file', result.stderr)
            target.unlink()
            target.mkdir()
            result = self.run_checker(runtime)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing runtime file', result.stderr)


if __name__ == '__main__':
    unittest.main()
