from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseInstallerSmokeContract(unittest.TestCase):
    def test_github_workflow_runs_exact_installer_smoke_on_windows(self):
        workflow = (ROOT / '.github/workflows/release-installer-smoke.yml').read_text(encoding='utf-8')
        script = (ROOT / 'tests/installer/release_installer_smoke.ps1').read_text(encoding='utf-8')
        self.assertIn('workflow_dispatch:', workflow)
        self.assertIn('runs-on: windows-latest', workflow)
        self.assertIn('source_sha', workflow)
        self.assertIn('qtimageformats', workflow)
        self.assertIn('curl.exe --fail --location --retry 5 --retry-all-errors --retry-delay 2', workflow)
        self.assertIn('actions/upload-artifact', workflow)
        self.assertIn('actions/download-artifact', workflow)
        self.assertIn('release_installer_smoke.ps1', workflow)
        for token in ('DisplayVersion', 'Qt6Core.dll', 'qwindows.dll', 'qwebp.dll',
                      'QtWebEngineProcess.exe', 'stremio-runtime.exe', 'COLOSSEUM_APPDATA_TAG',
                      'uninstall.exe', 'SHA256'):
            self.assertIn(token, script)


if __name__ == '__main__':
    unittest.main()
