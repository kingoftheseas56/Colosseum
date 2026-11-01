#!/usr/bin/env python3
"""Portable discovery fixtures; actual Darwin compiler probe runs in the build."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).with_name('libmpv-include-root.sh')

class IncludeRoot(unittest.TestCase):
    def select(self, path):
        return subprocess.run(['bash', '-c', 'source "$1"; colosseum_libmpv_include_root "$2"',
                               'test', str(HELPER), str(path)], capture_output=True, text=True)

    def test_public_root_and_leaf_both_resolve_to_public_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'keg with spaces/include'
            leaf = root / 'mpv'
            leaf.mkdir(parents=True)
            for name in ('client.h', 'render_gl.h'):
                (leaf / name).write_text('// fixture\n')
            for reported in (root, leaf):
                result = self.select(reported)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), str(root.resolve()))
            self.assertNotEqual(self.select(root / 'missing').returncode, 0)

    @unittest.skipUnless(shutil.which('c++'), 'requires a real C++ compiler')
    def test_public_include_form_fails_at_leaf_and_compiles_at_selected_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'include'
            leaf = root / 'mpv'
            leaf.mkdir(parents=True)
            for name in ('client.h', 'render_gl.h'):
                (leaf / name).write_text('// fixture\n')
            source = Path(directory) / 'probe.cpp'
            source.write_text('#include <mpv/client.h>\n#include <mpv/render_gl.h>\nint main() { return 0; }\n')
            bad = subprocess.run(['c++', '-fsyntax-only', '-isystem', str(leaf), str(source)], capture_output=True)
            self.assertNotEqual(bad.returncode, 0)
            selected = self.select(leaf)
            self.assertEqual(selected.returncode, 0, selected.stderr)
            good = subprocess.run(['c++', '-fsyntax-only', '-isystem', selected.stdout.strip(), str(source)], capture_output=True)
            self.assertEqual(good.returncode, 0, good.stderr)

    def test_missing_or_partial_headers_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'include'
            leaf = root / 'mpv'
            leaf.mkdir(parents=True)
            for reported in (root, leaf, root / 'missing'):
                self.assertNotEqual(self.select(reported).returncode, 0)
            (leaf / 'client.h').write_text('// fixture\n')
            self.assertNotEqual(self.select(root).returncode, 0)
            self.assertNotEqual(self.select(leaf).returncode, 0)

    def test_unrelated_flat_headers_are_not_an_mpv_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'include'
            root.mkdir()
            for name in ('client.h', 'render_gl.h'):
                (root / name).write_text('// unrelated fixture\n')
            self.assertNotEqual(self.select(root).returncode, 0)

if __name__ == '__main__':
    unittest.main()
