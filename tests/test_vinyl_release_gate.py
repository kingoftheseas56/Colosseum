import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION_CONTENT_API = 'https://colosseum-content-api.colosseum-watchparty-relay.workers.dev'


class VinylReleaseGateTests(unittest.TestCase):
    def test_development_defaults_on_and_both_release_builds_opt_out(self):
        cmake = (ROOT / 'native/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('option(COLOSSEUM_VINYL_IN_APP\n    "Enable the Vinyl world in the application"\n    ON)', cmake)
        self.assertIn('set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS COLOSSEUM_VINYL_IN_APP=1)', cmake)
        self.assertIn('set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS COLOSSEUM_VINYL_IN_APP=0)', cmake)

        windows = (ROOT / '.github/workflows/release-installer-smoke.yml').read_text(encoding='utf-8')
        linux = (ROOT / '.github/workflows/release-linux-deb.yml').read_text(encoding='utf-8')
        self.assertIn('-DCOLOSSEUM_VINYL_IN_APP=OFF', windows)
        self.assertIn('-DCOLOSSEUM_VINYL_IN_APP=OFF', linux)

        availability = (ROOT / 'native/vinyl/VinylAvailability.h').read_text(encoding='utf-8')
        disabled = availability.index('#if defined(COLOSSEUM_VINYL_IN_APP) && !COLOSSEUM_VINYL_IN_APP')
        opt_in = availability.index('qEnvironmentVariableIntValue("COLOSSEUM_VINYL_LIVE")')
        self.assertLess(disabled, opt_in)

    def test_windows_packager_requires_vinyl_off_only_for_1110(self):
        bash = Path('C:/Program Files/Git/bin/bash.exe')
        bash_command = str(bash) if bash.is_file() else shutil.which('bash')
        if not bash_command:
            self.skipTest('Bash is required to execute the Windows package guard')

        script = (ROOT / 'scripts/installer/package_release.sh').read_text(encoding='utf-8')
        start = script.index('if [ "$VERSION" = "1.1.10" ]; then')
        end = script.index('# API builds use semantic queries', start)
        guard = script[start:end]
        with tempfile.TemporaryDirectory(prefix='colosseum-vinyl-package-guard-') as directory:
            cache = Path(directory) / 'CMakeCache.txt'
            common = ('CMAKE_BUILD_TYPE:STRING=Release\n'
                      'COLOSSEUM_UPDATE_TESTING:BOOL=OFF\n'
                      'COLOSSEUM_ENABLE_VINYL_QUALIFICATION:BOOL=OFF\n'
                      'COLOSSEUM_ENABLE_VINYL_TEST_FIXTURES:BOOL=OFF\n'
                      'COLOSSEUM_NO_ACCOUNT_EDITION:BOOL=OFF\n'
                      f'COLOSSEUM_CONTENT_API_URL:STRING={PRODUCTION_CONTENT_API}\n')
            for version, vinyl_value, expected in (
                    ('1.1.10', 'OFF', 0), ('1.1.10', 'ON', 1), ('1.1.10', None, 1),
                    ('1.1.9', 'ON', 0)):
                with self.subTest(version=version, vinyl_value=vinyl_value):
                    vinyl_line = '' if vinyl_value is None else f'COLOSSEUM_VINYL_IN_APP:BOOL={vinyl_value}\n'
                    cache.write_text(common + vinyl_line, encoding='utf-8')
                    result = subprocess.run(
                        [bash_command, '--noprofile', '--norc', '-c', guard + 'echo ACCEPTED'],
                        cwd=directory,
                        env=dict(os.environ, BUILD_DIR=directory, VERSION=version),
                        capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    self.assertEqual('ACCEPTED' in result.stdout, expected == 0)
                    if version == '1.1.10' and vinyl_value != 'OFF':
                        self.assertIn('refusing 1.1.10 with Vinyl enabled or unspecified', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
