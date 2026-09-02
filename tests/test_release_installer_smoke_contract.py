from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseInstallerSmokeContract(unittest.TestCase):
    def test_release_metadata_targets_1_1_5(self):
        cmake = (ROOT / 'native/CMakeLists.txt').read_text(encoding='utf-8')
        workflow = (ROOT / '.github/workflows/release-installer-smoke.yml').read_text(encoding='utf-8')
        self.assertIn('project(colosseum VERSION 1.1.6 ', cmake)
        self.assertIn("default: \"1.1.6\"", workflow)

    def test_installer_branding_is_product_named_and_iconed(self):
        installer = (ROOT / 'scripts/installer/colosseum.nsi').read_text(encoding='utf-8')
        self.assertIn('Name "Colosseum"', installer)
        self.assertIn('Caption "Colosseum"', installer)
        self.assertIn('UninstallCaption "Colosseum"', installer)
        self.assertIn('!define MUI_ICON "${STAGE}\\assets\\icons\\colosseum.ico"', installer)
        self.assertIn('!define MUI_UNICON "${STAGE}\\assets\\icons\\colosseum.ico"', installer)
        self.assertTrue((ROOT / 'assets/icons/colosseum.ico').is_file())

    def test_github_workflow_runs_exact_installer_smoke_on_windows(self):
        workflow = (ROOT / '.github/workflows/release-installer-smoke.yml').read_text(encoding='utf-8')
        script = (ROOT / 'tests/installer/release_installer_smoke.ps1').read_text(encoding='utf-8')
        self.assertIn('workflow_dispatch:', workflow)
        self.assertIn('runs-on: windows-latest', workflow)
        self.assertIn('source_sha', workflow)
        self.assertIn('persist-credentials: false', workflow)
        self.assertIn('git fetch origin master', workflow)
        self.assertIn('git merge-base --is-ancestor', workflow)
        self.assertNotIn("ref: ${{ inputs.source_sha", workflow)
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
