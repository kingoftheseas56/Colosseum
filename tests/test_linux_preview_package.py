import importlib.util
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts/linux_release'


class LinuxPreviewPackage(unittest.TestCase):
    def test_preview_notice_and_metadata_are_staged_without_changing_version(self):
        with patch.object(sys, 'path', [str(SCRIPTS), *sys.path]):
            spec = importlib.util.spec_from_file_location('preview_package', SCRIPTS / 'package.py')
            package = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(package)
        self.assertTrue(hasattr(package, 'stage_preview_notice'), 'preview package notice is missing')
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            metadata = package.stage_preview_notice(stage)
            self.assertEqual(metadata['release_channel'], 'linux-preview')
            self.assertEqual(metadata['display_name'], 'Colosseum 1.1.7 Linux PREVIEW')
            self.assertEqual(metadata['derived_from_version'], '1.1.7')
            self.assertEqual(metadata['unsupported_features'],
                             ['account credential persistence', 'tracker credential persistence'])
            notice = (stage / metadata['preview_notice']).read_text()
            self.assertEqual(notice, (SCRIPTS / 'PREVIEW.md').read_text())
            self.assertIn(package.SOURCE, notice)
            self.assertIn('No plaintext credential fallback', notice)
        script = (SCRIPTS / 'package.py').read_text()
        self.assertIn('**stage_preview_notice(stage)', script)
        self.assertIn("'version': '1.1.7'", script)
        self.assertIn("stage.name + '-candidate.tar.gz'", script)


if __name__ == '__main__':
    unittest.main()
