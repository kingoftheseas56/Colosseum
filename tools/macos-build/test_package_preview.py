#!/usr/bin/env python3
"""Portable adverse fixtures for the Mac packaging boundary; no Mac build claim."""
import importlib.util
import os
from pathlib import Path
import plistlib
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

spec = importlib.util.spec_from_file_location('package_preview', Path(__file__).with_name('package-preview.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackagingBoundary(unittest.TestCase):
    def test_qt_license_case_aliases_are_copied_once(self):
        qt = MagicMock()
        directories = [MagicMock() for _ in range(3)]
        for directory in directories:
            directory.is_dir.return_value = True
            directory.stat.return_value = SimpleNamespace(st_dev=1, st_ino=42)
        qt.__truediv__.side_effect = directories
        with patch.object(package.shutil, 'copytree') as copy:
            package.copy_qt_license_directories(qt, Path('/notices'))
            copy.assert_called_once_with(directories[0], Path('/notices/Qt-licenses-1'))
        # Distinct source directories retain their contents under unique names.
        qt.__truediv__.side_effect = directories
        directories[2].stat.return_value = SimpleNamespace(st_dev=1, st_ino=43)
        with patch.object(package.shutil, 'copytree') as copy:
            package.copy_qt_license_directories(qt, Path('/notices'))
            self.assertEqual(copy.call_count, 2)
            self.assertEqual(copy.call_args.args[1], Path('/notices/Qt-licenses-2'))

    def test_homebrew_provenance_selects_only_installed_runtime_closure(self):
        def formula(name, dependencies=(), aliases=(), installed=True):
            return dict(name=name, dependencies=list(dependencies), aliases=list(aliases),
                        installed=[{'version': '1'}] if installed else [])
        metadata = [formula('mpv', ('ffmpeg',)), formula('ffmpeg', ('ssl-alias',)),
                    formula('openssl@3', ('mpv',), ('ssl-alias',)),
                    formula('python'), formula('llvm'), formula('old-keg', installed=False)]
        selected = package.runtime_formula_closure(metadata, roots=('mpv',))
        self.assertEqual([entry['name'] for entry in selected], ['ffmpeg', 'mpv', 'openssl@3'])
        with self.assertRaisesRegex(RuntimeError, 'absent from installed'):
            package.runtime_formula_closure(metadata, roots=('old-keg',))
        metadata[1]['dependencies'].append('missing-runtime')
        with self.assertRaisesRegex(RuntimeError, 'missing-runtime'):
            package.runtime_formula_closure(metadata, roots=('mpv',))

    def test_gui_requires_visible_window_and_completed_boot_splash(self):
        state = {'windows': [{'visible': True, 'width': 1280, 'height': 720}]}
        for splash in ({}, {'props': {}}, {'props': {'visible': True}}, {'props': {'visible': 'false'}}):
            self.assertFalse(package.startup_ready(state, splash))
        completed = {'props': {'visible': False}}
        self.assertTrue(package.startup_ready(state, completed))
        self.assertFalse(package.startup_ready({'windows': []}, completed))
        state['windows'][0]['visible'] = False
        self.assertFalse(package.startup_ready(state, completed))

    def test_audit_traverses_helper_context_and_rejects_newer_minimum(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Colosseum.app'
            helper_app = app / 'Contents/Frameworks/Engine.app'
            main = app / 'Contents/MacOS/colosseum'
            helper = helper_app / 'Contents/MacOS/engine'
            library = app / 'Contents/Frameworks/libshared.dylib'
            for path in (main, helper, library):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
            for bundle, name in ((app, 'colosseum'), (helper_app, 'engine')):
                with (bundle / 'Contents/Info.plist').open('wb') as stream:
                    plistlib.dump({'CFBundleExecutable': name}, stream)
            metadata = {}
            for path in (main, helper, library):
                anchor = '@loader_path/' + os.path.relpath(library.parent, path.parent)
                metadata[path.resolve()] = (['@rpath/libshared.dylib'] if path != library else [], [anchor], ['26.1'])
            def header(*args):
                return 'EXECUTE' if Path(args[-1]).resolve() in (main.resolve(), helper.resolve()) else 'DYLIB'
            with patch.object(package, 'inspect_macho', side_effect=lambda path: metadata[path.resolve()]), patch.object(package, 'run', side_effect=header):
                report = package.audit(app)
                self.assertEqual(report['executable_count'], 2)
                self.assertEqual(report['macho_count'], 3)
                self.assertEqual({edge['executable'] for edge in report['edges']},
                                 {str(main.relative_to(app)), str(helper.relative_to(app))})
                metadata[library.resolve()] = ([], metadata[library.resolve()][1], ['26.2'])
                with self.assertRaisesRegex(RuntimeError, 'incompatible macOS minimum'):
                    package.audit(app)
                metadata[library.resolve()] = ([], metadata[library.resolve()][1], [])
                with self.assertRaisesRegex(RuntimeError, 'Missing'):
                    package.audit(app)

    def test_load_commands_distinguish_id_and_dependency(self):
        commands = '''Load command 0
          cmd LC_ID_DYLIB
         name /opt/homebrew/lib/libself.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLIB
         name @rpath/libchild.dylib (offset 24)
Load command 2
          cmd LC_RPATH
         path @loader_path/../Frameworks (offset 12)
Load command 3
          cmd LC_BUILD_VERSION
     platform 1
        minos 26.1
          sdk 26.2
'''
        self.assertEqual(package.parse_load_commands(commands),
                         (['@rpath/libchild.dylib'], ['@loader_path/../Frameworks'], ['26.1']))
        self.assertGreater(package.version('26.2'), package.version('26.1'))
        with self.assertRaisesRegex(RuntimeError, 'Non-macOS'):
            package.parse_load_commands(commands.replace('platform 1', 'platform 2'))

    def test_closure_rejects_missing_external_and_ambiguous_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Colosseum.app'
            loader = app / 'Contents/MacOS/colosseum'
            loader.parent.mkdir(parents=True)
            loader.touch()
            frameworks = app / 'Contents/Frameworks'
            frameworks.mkdir()
            child = frameworks / 'libchild.dylib'
            child.touch()
            resolve = lambda name, paths=(frameworks,): package.resolve_dependency(name, loader, loader, paths, app)
            self.assertEqual(resolve('@rpath/libchild.dylib'), child.resolve())
            self.assertIsNone(resolve('/usr/lib/libSystem.B.dylib'))
            for name in ('/opt/homebrew/lib/libchild.dylib', '@rpath/missing.dylib',
                         '/usr/lib/../../opt/homebrew/lib/libchild.dylib',
                         '@loader_path/../../../outside.dylib', 'libchild.dylib'):
                with self.subTest(name=name), self.assertRaises(RuntimeError):
                    resolve(name)
            second = app / 'Contents/other'
            second.mkdir()
            (second / child.name).touch()
            with self.assertRaisesRegex(RuntimeError, 'ambiguous'):
                resolve('@rpath/libchild.dylib', (frameworks, second))

    def test_nested_executable_path_uses_helper_context(self):
        app = Path('/bundle/Colosseum.app')
        helper = app / 'Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess'
        loader = app / 'Contents/Frameworks/libshared.dylib'
        self.assertEqual(package.expand_anchor('@executable_path/../Frameworks', loader, helper), helper.parent / '../Frameworks')
        self.assertNotEqual(package.expand_anchor('@executable_path/../Frameworks', loader, helper), loader.parent / '../Frameworks')

    def test_resource_stage_preserves_qml_and_only_selected_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, app = root / 'source', root / 'Colosseum.app'
            for name in ('qml', 'assets', 'resources', 'data'):
                (source / name).mkdir(parents=True)
            (source / 'qml/Main.qml').write_bytes(b'import QtQuick\nItem {}\n')
            (source / 'qml/helper.js').write_text('function value() { return 1; }')
            (source / 'resources/reader-vendor.html').write_text('reader')
            (source / 'data/title-identities-v1.json').write_text('{}')
            (source / 'data/private.db').write_text('never package')
            macos = app / 'Contents/MacOS'
            (macos / 'scripts/vinyl').mkdir(parents=True)
            (macos / 'colosseum').touch()
            (macos / 'qml-build.manifest').write_text('schema=1\n')
            (macos / 'scripts/vinyl/provider.py').write_text('provider')
            runtime = package.stage_runtime(source, app)
            self.assertEqual((runtime / 'qml/Main.qml').read_bytes(), (source / 'qml/Main.qml').read_bytes())
            self.assertTrue((runtime / 'qml/helper.js').is_file())
            self.assertTrue((runtime / 'scripts/vinyl/provider.py').is_file())
            self.assertEqual([path.name for path in (runtime / 'data').iterdir()], ['title-identities-v1.json'])
            self.assertEqual([path.name for path in macos.iterdir()], ['colosseum'])
            self.assertFalse((app / 'Contents/Resources/qml/Main.qml').exists())


if __name__ == '__main__':
    unittest.main()
