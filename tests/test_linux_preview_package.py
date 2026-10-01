import importlib.util
import pathlib
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts/linux_release'


class LinuxPreviewPackage(unittest.TestCase):
    def package_module(self):
        with patch.object(sys, 'path', [str(SCRIPTS), *sys.path]):
            spec = importlib.util.spec_from_file_location('preview_package', SCRIPTS / 'package.py')
            package = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(package)
        return package

    def test_required_image_decoder_is_staged_and_missing_decoder_is_rejected(self):
        package = self.package_module()
        self.assertTrue(hasattr(package, 'stage_qt_plugins'), 'required image plugin gate is missing')
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            qt, stage = root / 'qt', root / 'stage'
            stage.mkdir()
            for group in ['platforms', 'imageformats', 'iconengines', 'tls', 'xcbglintegrations', 'sqldrivers']:
                (qt / 'plugins' / group).mkdir(parents=True)
            for group, names in {'platforms': ['libqxcb.so', 'libqoffscreen.so', 'libqwayland.so'],
                                 'imageformats': ['libqwebp.so', 'libqtiff.so'], 'sqldrivers': ['libqsqlite.so']}.items():
                for name in names:
                    (qt / 'plugins' / group / name).write_bytes(b'fixture plugin')
            package.stage_qt_plugins(qt, stage)
            self.assertEqual((stage / 'usr/plugins/imageformats/libqwebp.so').read_bytes(), b'fixture plugin')
            self.assertFalse((stage / 'usr/plugins/imageformats/libqtiff.so').exists())
            self.assertEqual({p.name for p in (stage / 'usr/plugins/platforms').iterdir()},
                             {'libqxcb.so', 'libqoffscreen.so'})
            (qt / 'plugins/imageformats/libqwebp.so').unlink()
            missing_stage = root / 'missing'
            missing_stage.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'libqwebp'):
                package.stage_qt_plugins(qt, missing_stage)

    @unittest.skipIf(os.name == 'nt', 'launcher executes in a POSIX shell')
    def test_launcher_defaults_to_bundled_x11_and_preserves_explicit_override(self):
        package = self.package_module()
        self.assertTrue(hasattr(package, 'write_launcher'), 'X11 launcher policy is missing')
        with tempfile.TemporaryDirectory(prefix='colosseum package ') as directory:
            stage = pathlib.Path(directory)
            binary = stage / 'usr/bin/colosseum'
            binary.parent.mkdir(parents=True)
            binary.write_text('#!/bin/sh\nprintf "%s" "$QT_QPA_PLATFORM"\n')
            binary.chmod(0o755)
            package.write_launcher(stage)
            env = os.environ.copy()
            env.pop('QT_QPA_PLATFORM', None)
            self.assertEqual(subprocess.check_output([str(stage / 'AppRun')], env=env, text=True), 'xcb')
            env['QT_QPA_PLATFORM'] = 'offscreen'
            self.assertEqual(subprocess.check_output([str(stage / 'AppRun')], env=env, text=True), 'offscreen')

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
