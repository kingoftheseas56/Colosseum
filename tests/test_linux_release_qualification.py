import importlib.util
import pathlib
import tempfile
import unittest


class LinuxReleaseQualification(unittest.TestCase):
    def module(self):
        path = pathlib.Path(__file__).parents[1] / 'scripts/linux_release/qualify.py'
        self.assertTrue(path.is_file(), 'Linux package qualifier is not implemented')
        spec = importlib.util.spec_from_file_location('qualify', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_loader_rejects_missing_library(self):
        q = self.module()
        self.assertTrue(q.loader_errors('libmpv.so.2 => not found', pathlib.Path('/opt/app')))

    def test_loader_rejects_builder_library(self):
        q = self.module()
        self.assertTrue(q.loader_errors('libQt6Core.so.6 => /opt/Qt/lib/libQt6Core.so.6 (0x1)', pathlib.Path('/opt/app')))

    def test_loader_allows_bundle_and_host_glibc(self):
        q = self.module()
        self.assertEqual(q.loader_errors('libQt6Core.so.6 => /opt/app/usr/lib/libQt6Core.so.6 (0x1)\nlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x2)', pathlib.Path('/opt/app')), [])

    def test_empty_catalog_does_not_qualify(self):
        q = self.module()
        self.assertFalse(q.catalog_ready('[catalog-selftest] tab movies -> 0 rows'))
        self.assertFalse(q.catalog_ready('[catalog-selftest] row 0 House 0 items, first: -'))
        self.assertTrue(q.catalog_ready('[catalog-selftest] row 0 House 20 items, first: tt123'))

    def test_qml_import_failure_is_fatal(self):
        q = self.module()
        self.assertTrue(q.qml_errors('module "QtWebEngine" is not installed'))
        self.assertTrue(q.qml_errors('QQmlApplicationEngine failed to load component'))
        self.assertFalse(q.qml_errors('CatalogVaultClient: manifest fetch failed'))

    def test_missing_extractor_rejected_even_with_media_tools(self):
        q = self.module()
        self.assertTrue(hasattr(q, 'required_tools'), 'runtime executable gate missing')
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ['ffmpeg', 'ffprobe']:
                (root / name).write_text('#!/bin/sh\n')
                (root / name).chmod(0o755)
            with self.assertRaisesRegex(RuntimeError, 'bsdtar'):
                q.required_tools(root)
            (root / 'bsdtar').write_text('#!/bin/sh\n')
            with self.assertRaisesRegex(RuntimeError, 'bsdtar'):
                q.required_tools(root)
            (root / 'bsdtar').chmod(0o755)
            self.assertEqual(set(q.required_tools(root)), {'ffmpeg', 'ffprobe', 'bsdtar'})


if __name__ == '__main__':
    unittest.main()
