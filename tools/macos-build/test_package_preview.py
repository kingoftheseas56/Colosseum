#!/usr/bin/env python3
"""Portable adverse fixtures for the Mac packaging boundary; no Mac build claim."""
import importlib.util
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import zipfile
import stat
import json
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

spec = importlib.util.spec_from_file_location('package_preview', Path(__file__).with_name('package-preview.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackagingBoundary(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'darwin', 'requires actual Xcode Mach-O tools')
    def test_real_xcode_macho_inspection_tool_contract(self):
        with tempfile.TemporaryDirectory(prefix='colosseum-macho-contract-') as directory:
            root = Path(directory)
            source = root / 'contract.c'
            binary = root / 'contract-arm64'
            source.write_text('int main(void) { return 0; }\n')
            subprocess.run(['xcrun', 'clang', '-arch', 'arm64', '-mmacosx-version-min=26.1',
                            '-x', 'c', str(source), '-o', str(binary)], check=True,
                           capture_output=True, text=True)
            dependencies, rpaths, minimums = package.inspect_macho(binary)
            self.assertTrue(minimums)
            self.assertTrue(all(package.version(value) <= package.version('26.1') for value in minimums))
            self.assertIn('/usr/lib/libSystem.B.dylib', dependencies)
            self.assertTrue(all(package.system_path(dependency) for dependency in dependencies))
            self.assertEqual(rpaths, [])

    def test_sqlite_only_deployment_retains_required_driver_and_audits_omissions(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Colosseum.app'
            drivers = app / 'Contents/PlugIns/sqldrivers'
            drivers.mkdir(parents=True)
            for name in ('libqsqlite.dylib', 'libqsqlpsql.dylib', 'libqsqlmimer.dylib', 'libqsqlodbc.dylib'):
                (drivers / name).write_bytes(name.encode())
            report = package.select_sqlite_driver(app)
            self.assertEqual(report['retained'], ['libqsqlite.dylib'])
            self.assertEqual(len(report['omitted']), 3)
            self.assertEqual(list(drivers.iterdir()), [drivers / 'libqsqlite.dylib'])
            self.assertEqual((drivers / 'libqsqlite.dylib').read_bytes(), b'libqsqlite.dylib')

    def test_sqlite_policy_fails_closed_without_required_plugin_or_on_unexpected_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Colosseum.app'
            with self.assertRaises(RuntimeError):
                package.select_sqlite_driver(app)
            drivers = app / 'Contents/PlugIns/sqldrivers'
            drivers.mkdir(parents=True)
            (drivers / 'libqsqlpsql.dylib').write_bytes(b'postgres')
            with self.assertRaises(RuntimeError):
                package.select_sqlite_driver(app)
            self.assertTrue((drivers / 'libqsqlpsql.dylib').exists())
            (drivers / 'libqsqlite.dylib').write_bytes(b'sqlite')
            (drivers / 'unreviewed.dylib').write_bytes(b'unknown')
            with self.assertRaises(RuntimeError):
                package.select_sqlite_driver(app)
            self.assertTrue((drivers / 'libqsqlpsql.dylib').exists())

    def test_archive_crc_paths_and_contained_framework_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'preview.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('Colosseum.app/Contents/MacOS/colosseum', b'executable')
                z.writestr('Colosseum.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore', b'dylib')
                link = zipfile.ZipInfo('Colosseum.app/Contents/Frameworks/QtCore.framework/Versions/Current')
                link.create_system = 3
                link.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(link, 'A')
                alias = zipfile.ZipInfo('Colosseum.app/Contents/Frameworks/QtCore.framework/QtCore')
                alias.create_system = 3
                alias.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(alias, 'Versions/Current/QtCore')
                z.writestr('__MACOSX/Colosseum.app/Contents/._Info.plist', b'resource-fork')
            result = package.verify_archive(archive)
            self.assertEqual(result['status'], 'PASS')
            self.assertEqual(result['members'], 5)
            self.assertEqual(result['symlinks'], 2)

    def test_archive_rejects_unsafe_members_and_symlink_escapes(self):
        cases = [('/tmp/escape', b'x', False), ('../escape', b'x', False),
                 ('Colosseum.app/../escape', b'x', False), ('wrong.app/file', b'x', False),
                 ('Colosseum.app/Contents/link', b'../../../outside', True),
                 ('Colosseum.app/Contents/link', b'/tmp/outside', True),
                 ('Colosseum.app/Contents/link', b'..\\..\\outside', True)]
        for name, value, symbolic in cases:
            with self.subTest(name=name, value=value), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / 'preview.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    entry = zipfile.ZipInfo(name)
                    if symbolic:
                        entry.create_system = 3
                        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                    z.writestr(entry, value)
                with self.assertRaises(RuntimeError):
                    package.verify_archive(archive)

    def test_archive_rejects_collisions_and_children_below_symlinks(self):
        for names in [('Colosseum.app/File', 'Colosseum.app/file'),
                      ('Colosseum.app/File', 'Colosseum.app/File'),
                      ('Colosseum.app/Contents/link', 'Colosseum.app/Contents/link/child')]:
            with self.subTest(names=names), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / 'preview.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    for name in names:
                        entry = zipfile.ZipInfo(name)
                        if name.endswith('/link'):
                            entry.create_system = 3
                            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                        z.writestr(entry, b'other')
                with self.assertRaises(RuntimeError):
                    package.verify_archive(archive)

    def test_archive_resolves_symlink_chains_before_parent_traversal(self):
        cases = [
            {'Colosseum.app/B': '.', 'Colosseum.app/Contents/Frameworks/A': '../../B/../outside'},
            {'Colosseum.app/A': 'B', 'Colosseum.app/B': 'A'},
        ]
        for links in cases:
            with self.subTest(links=links), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / 'preview.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    z.writestr('Colosseum.app/Contents/MacOS/colosseum', b'executable')
                    for name, target in links.items():
                        entry = zipfile.ZipInfo(name)
                        entry.create_system = 3
                        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                        z.writestr(entry, target)
                with self.assertRaises(RuntimeError):
                    package.verify_archive(archive)

    def test_archive_checks_crc(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'preview.zip'
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:
                z.writestr('Colosseum.app/file', b'UNIQUE-PAYLOAD')
            archive.write_bytes(archive.read_bytes().replace(b'UNIQUE-PAYLOAD', b'CORRUPT-BYTES!'))
            with self.assertRaises(RuntimeError):
                package.verify_archive(archive)

    def test_window_capture_requires_png_inside_isolated_run_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'profile'
            run = data / 'lanista/runs/test'
            run.mkdir(parents=True)
            image = run / 'frame.png'
            image.write_bytes(b'\x89PNG\r\n\x1a\n' + (13).to_bytes(4, 'big') + b'IHDR'
                              + (1280).to_bytes(4, 'big') + (720).to_bytes(4, 'big'))
            state = dict(appDataRoot=str(data), runDir=str(run), grabPath=str(image))
            output = root / 'output'
            output.mkdir()
            record = package.copy_window_capture(state, output)
            self.assertEqual(record['status'], 'PASS')
            self.assertEqual((output / 'runtime-window.png').read_bytes(), image.read_bytes())
            outside = root / 'outside.png'
            outside.write_bytes(image.read_bytes())
            with self.assertRaises(RuntimeError):
                package.copy_window_capture(dict(state, grabPath=str(outside)), output)
            alias = run / 'alias.png'
            alias.symlink_to(outside)
            with self.assertRaises(RuntimeError):
                package.copy_window_capture(dict(state, grabPath=str(alias)), output)
            image.write_bytes(b'not PNG')
            with self.assertRaises(RuntimeError):
                package.copy_window_capture(state, output)

    def test_smoke_requires_actual_tagged_roots(self):
        tag = 'mac-preview-123'
        name = 'Colosseum-dltest-' + tag
        valid = {'appDataRoot': '/Users/test/Library/Application Support/' + name,
                 'cacheRoot': '/Users/test/Library/Caches/' + name}
        self.assertTrue(package.isolated_roots(valid, tag))
        for key in valid:
            for wrong in ('', 'relative/' + name, '/Users/test/Library/Colosseum',
                          '/Users/test/' + name + '/personal'):
                changed = dict(valid, **{key: wrong})
                self.assertFalse(package.isolated_roots(changed, tag))

    def test_smoke_persists_failed_version_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = subprocess.CompletedProcess([], 1, stdout='', stderr='dyld failure')
            with patch.object(package, 'verify_archive', return_value={'status': 'PASS'}), \
                    patch.object(package, 'run'), patch.object(package.subprocess, 'run', return_value=result), \
                    self.assertRaises(RuntimeError):
                package.smoke(output)
            report = json.loads((output / 'runtime-smoke.json').read_text())
            self.assertEqual(report['version'], 'FAIL')
            self.assertEqual(report['gui'], 'NOT RUN')
            self.assertIn('dyld failure', report['error'])

    def webengine_fixture(self, root):
        app = root / 'Colosseum.app'
        helper = app / 'Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app'
        expected_path = helper / 'Contents/Resources/QtWebEngineProcess.entitlements'
        expected_path.parent.mkdir(parents=True)
        expected = {'com.apple.security.cs.allow-jit': True}
        expected_path.write_bytes(plistlib.dumps(expected))
        return app, helper, expected_path, expected, root / 'signing-entitlements.json'

    def test_webengine_entitlements_preserve_sdk_values_and_deduplicate_helpers(self):
        with tempfile.TemporaryDirectory() as directory:
            app, helper, _, expected, report_path = self.webengine_fixture(Path(directory))
            actual = dict(expected, **{'another.existing.sdk.key': True})
            result = subprocess.CompletedProcess([], 0, stdout=plistlib.dumps(actual),
                                                 stderr=b'Executable=QtWebEngineProcess\n')
            with patch.object(Path, 'rglob', return_value=[helper, helper]), patch.object(package.subprocess, 'run', return_value=result) as execute:
                report = package.verify_webengine_entitlements(app, report_path)
            self.assertEqual(report['status'], 'PASS')
            self.assertEqual(len(report['helpers']), 1)
            self.assertEqual(report['helpers'][0]['expected'], expected)
            self.assertEqual(report['helpers'][0]['actual'], actual)
            execute.assert_called_once_with(['codesign', '--display', '--entitlements', '-', '--xml', str(helper.resolve())],
                                            check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertIn('"status": "PASS"', report_path.read_text())

    def test_webengine_entitlements_fail_missing_changed_or_invalid_signed_values(self):
        values = (b'', b'not a plist', plistlib.dumps({}),
                  plistlib.dumps({'com.apple.security.cs.allow-jit': True}, fmt=plistlib.FMT_BINARY),
                  plistlib.dumps({'some.other.entitlement': True}),
                  plistlib.dumps({'com.apple.security.cs.allow-jit': False}),
                  plistlib.dumps({'com.apple.security.cs.allow-jit': 1}))
        for value in values:
            with self.subTest(value=value), tempfile.TemporaryDirectory() as directory:
                app, _, _, _, report_path = self.webengine_fixture(Path(directory))
                result = subprocess.CompletedProcess([], 0, stdout=value, stderr=b'codesign diagnostic\n')
                with patch.object(package.subprocess, 'run', return_value=result), self.assertRaises(Exception):
                    package.verify_webengine_entitlements(app, report_path)
                self.assertIn('"status": "FAIL"', report_path.read_text())

    def test_webengine_entitlements_require_valid_nonempty_sdk_plist(self):
        for value in (None, b'', b'invalid', plistlib.dumps({})):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as directory:
                app, _, expected_path, _, report_path = self.webengine_fixture(Path(directory))
                if value is None:
                    expected_path.unlink()
                else:
                    expected_path.write_bytes(value)
                with patch.object(package.subprocess, 'run') as execute, self.assertRaises(Exception):
                    package.verify_webengine_entitlements(app, report_path)
                execute.assert_not_called()
                self.assertIn('"status": "FAIL"', report_path.read_text())

    def test_webengine_entitlements_require_helper_and_successful_codesign(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app, _, _, expected, report_path = self.webengine_fixture(root)
            result = subprocess.CompletedProcess(['codesign'], 1, stdout=plistlib.dumps(expected), stderr=b'unsigned\n')
            with patch.object(package.subprocess, 'run', return_value=result), self.assertRaises(subprocess.CalledProcessError):
                package.verify_webengine_entitlements(app, report_path)
            with self.assertRaisesRegex(RuntimeError, 'No QtWebEngineProcess.app'):
                package.verify_webengine_entitlements(root / 'absent.app', report_path)
            self.assertIn('"status": "FAIL"', report_path.read_text())

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
