import importlib.util
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


class LinuxReleaseQualification(unittest.TestCase):
    def module(self):
        path = pathlib.Path(__file__).parents[1] / 'scripts/linux_release/qualify.py'
        self.assertTrue(path.is_file(), 'Linux package qualifier is not implemented')
        spec = importlib.util.spec_from_file_location('qualify', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def elf(self, root, name, kind, byteorder='little', elf_class=2):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        ident = b'\x7fELF' + bytes([elf_class, 1 if byteorder == 'little' else 2, 1]) + bytes(9)
        path.write_bytes(ident + kind.to_bytes(2, byteorder) + bytes(46))
        return path

    def test_runtime_elf_selection_excludes_relocatable_objects(self):
        q = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for byteorder in ['little', 'big']:
                for elf_class in [1, 2]:
                    for kind in [0, 1, 2, 3, 4]:
                        with self.subTest(byteorder=byteorder, elf_class=elf_class, kind=kind):
                            path = self.elf(root, 'file', kind, byteorder, elf_class)
                            self.assertEqual(q.is_elf(path), kind in [2, 3])
            for data in [b'', b'\x7fELF', b'!<arch>\n', b'\x7fELF' + bytes(60)]:
                path.write_bytes(data)
                self.assertFalse(q.is_elf(path))
            self.assertFalse(q.is_elf(root))

    def test_closure_scans_runtime_plugins_but_not_sdk_objects(self):
        q = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            executable = self.elf(root, 'app', 2)
            plugin = self.elf(root, 'qml/plugin.so', 3)
            self.elf(root, 'qml/objects-RelWithDebInfo/plugin.cpp.o', 1)
            def ldd(command, **kwargs):
                if command[1] not in [str(executable), str(plugin)]:
                    return subprocess.CompletedProcess(command, 1, '', 'not a dynamic executable')
                return subprocess.CompletedProcess(command, 0, 'libc.so.6 => /lib/libc.so.6 (0x1)', '')
            with patch.object(q.subprocess, 'run', side_effect=ldd):
                q.closure(root, root, {})
            log = (root / 'elf-closure.log').read_text()
            self.assertIn('qml/plugin.so', log)
            self.assertIn('app', log)
            self.assertNotIn('plugin.cpp.o', log)

    def test_closure_rejects_unresolved_runtime_plugin_and_ldd_failure(self):
        q = self.module()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            plugin = self.elf(root, 'qml/plugin.so', 3)
            for code, output in [(0, 'libmissing.so => not found'), (1, 'loader failure')]:
                with self.subTest(code=code):
                    result = subprocess.CompletedProcess(['ldd', str(plugin)], code, output, '')
                    with patch.object(q.subprocess, 'run', return_value=result):
                        with self.assertRaises(RuntimeError):
                            q.closure(root, root, {})

    def test_qt_runtime_copy_omits_sdk_artifacts_and_retains_plugins(self):
        scripts = pathlib.Path(__file__).parents[1] / 'scripts/linux_release'
        with patch.object(sys, 'path', [str(scripts), *sys.path]):
            import package
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'source'
            source.mkdir()
            for name in ['qmldir', 'plugin.qmltypes', 'libplugin.so', 'libstatic.a', 'plugin.prl', 'objects-RelWithDebInfo/plugin.cpp.o']:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            package.copy_qt_runtime(source, root / 'staged')
            files = {str(path.relative_to(root / 'staged')) for path in (root / 'staged').rglob('*') if path.is_file()}
            self.assertEqual(files, {'qmldir', 'plugin.qmltypes', 'libplugin.so'})

    def test_workflow_reports_failures_and_continues_independent_gates(self):
        workflow = (pathlib.Path(__file__).parents[1] / '.github/workflows/linux-117-package.yml').read_text()
        steps = re.split(r'^      - ', workflow, flags=re.M)[1:]
        for gate, prerequisite in [('tests', 'build'), ('package', 'build'), ('runtime', 'package'), ('tidy', 'build')]:
            step = next(step for step in steps if re.search(r'^        id: ' + gate + r'$', step, re.M))
            with self.subTest(gate=gate):
                self.assertNotIn('continue-on-error:', step)
                self.assertIn("!cancelled() && steps." + prerequisite + ".outcome == 'success'", step)
        for name in ['Record final verdict', 'Upload evidence on success or failure', 'Fail closed unless all gates passed']:
            step = next(step for step in steps if step.startswith('name: ' + name + '\n'))
            self.assertIn('if: always()', step)
        self.assertLess(workflow.index('id: runtime'), workflow.index('id: tidy'))
        self.assertIn("all(value == 'success' for value in results.values())", workflow)
        self.assertIn("names = ['BUILD', 'TEST', 'TIDY', 'PACKAGE', 'RUNTIME']", workflow)

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
        self.assertTrue(q.qml_errors("file:///opt/app/qml/PlayerPage.qml:3700: TypeError: Cannot call method 'playbackStateChanged' of undefined"))
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
