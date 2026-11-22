import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_appimage', ROOT / 'tools/linux-release/package_appimage.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sys.path.insert(0, str(ROOT / 'tools/linux-release'))
from check_package import baseline_library_name


class AppImageIdentityTests(unittest.TestCase):
    def test_arch_absolute_loader_remains_system_baseline(self):
        self.assertTrue(baseline_library_name('ld-linux-x86-64.so.2'))
        self.assertTrue(baseline_library_name('/lib64/ld-linux-x86-64.so.2'))
        self.assertTrue(baseline_library_name('/usr/lib64/ld-linux-x86-64.so.2'))

    def test_absolute_nonbaseline_and_untrusted_loader_paths_rejected(self):
        for name in ['/usr/lib/libssl.so.3', '/tmp/ld-linux-x86-64.so.2', '/lib64/not-the-loader.so', 'usr/lib64/ld-linux-x86-64.so.2']:
            self.assertFalse(baseline_library_name(name), name)

    def test_changed_or_missing_original_file_rejected(self):
        original = {'AppRun': {'sha256': 'original', 'mode': 493}}
        for final in [{}, {'AppRun': {'sha256': 'changed', 'mode': 493}}, {'AppRun': {'sha256': 'original', 'mode': 420}}]:
            with self.assertRaisesRegex(RuntimeError, 'payload changed'):
                module.assert_unchanged(original, final)

    def test_only_packaging_additions_allowed(self):
        original = {'AppRun': {'sha256': 'original', 'mode': 493}}
        module.assert_unchanged(original, dict(original, **{'colosseum.desktop': {}, 'colosseum.svg': {}, '.DirIcon': {}, 'APPIMAGE-PACKAGING.json': {}}))
        with self.assertRaisesRegex(RuntimeError, 'Unexpected'):
            module.assert_unchanged(original, dict(original, **{'usr/bin/other-runtime': {}}))

    def test_cross_source_qml_or_abi_drift_rejected(self):
        baseline = dict(status='PASS', source_commit='source', executable_sha256='binary', qml_fingerprint='qml', elf_count=503,
                        required_symbol_versions_max={'GLIBC': '2.38', 'GLIBCXX': '3.4.32', 'CXXABI': '1.3.13'})
        module.assert_audit(baseline, baseline)
        for key, value in [('status', 'FAIL'), ('source_commit', 'other'), ('qml_fingerprint', 'other'), ('executable_sha256', 'other'), ('elf_count', 502), ('required_symbol_versions_max', {'GLIBC': '2.40'})]:
            changed = dict(baseline)
            changed[key] = value
            with self.assertRaisesRegex(RuntimeError, 'audit differs'):
                module.assert_audit(changed, baseline)

    def test_real_inventory_detects_changed_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'AppRun').write_text('original')
            original = module.inventory(root)
            (root / 'AppRun').write_text('changed')
            with self.assertRaises(RuntimeError):
                module.assert_unchanged(original, module.inventory(root))

    def test_pins_keep_same_checked_source_and_floor(self):
        pins = module.PINS
        self.assertEqual(pins['version'], '1.4.0')
        self.assertEqual(pins['application_source_commit'], 'ab62d156efd2fa4c923de5970704f0860e086f23')
        self.assertEqual(pins['appdir_run'], '45015487828')
        self.assertEqual(pins['supported_glibc_floor'], '2.39')
        self.assertEqual(pins['appdir_sha256'], 'a451b302d6e0f764594e27ccb1317b0306686b2ea9772fbd70f70627ec5c097b')
        for key in ['appimagetool', 'runtime']:
            self.assertRegex(pins[key]['sha256'], r'^[0-9a-f]{64}$')


if __name__ == '__main__':
    unittest.main()
