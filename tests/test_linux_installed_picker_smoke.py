import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'installed_picker_smoke', ROOT / 'tools/linux-release/smoke_installed.py')
smoke = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(smoke)


class InstalledGtkPickerSmokeTests(unittest.TestCase):
    def run_proof(self, x_replies, ipc_replies, returncode=None, clock=None, poll_results=None):
        x_replies = ['99', 'Colosseum', 'WIDTH=1280\nHEIGHT=720', '', ''] + x_replies
        clock = [-2, -1] + clock
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            process = Mock(pid=1234, returncode=returncode)
            process.poll.return_value = returncode
            process.poll.side_effect = poll_results
            with patch.object(smoke, 'command', side_effect=ipc_replies) as ipc, \
                    patch.object(smoke, 'gtk_runtime_evidence', return_value={'packaged': True}), \
                    patch.object(smoke, 'xdotool', side_effect=x_replies) as x11, \
                    patch.object(smoke.time, 'sleep'), \
                    patch.object(smoke.time, 'monotonic', side_effect=clock):
                error = None
                try:
                    smoke.prove_gtk_picker('test.sock', process, output,
                                           {'source_commit': 'candidate'})
                except RuntimeError as caught:
                    error = caught
            self.assertTrue(all(call.args[1] == 'get-state' for call in ipc.call_args_list))
            self.assertTrue(all('--all' in call.args for call in x11.call_args_list
                                if call.args[0] == 'search'))
            receipt = json.loads((output / 'installed-picker-smoke.json').read_text())
            return receipt, error, x11.call_args_list

    def test_native_window_opens_xtest_cancels_and_app_remains_alive(self):
        receipt, error, calls = self.run_proof(
            ['42', 'Open Media', 'WIDTH=640\nHEIGHT=480', '', '', '99', ''],
            [{'windows': [{'visible': True}]}], clock=[0, 1, 2, 3])
        self.assertIsNone(error)
        self.assertEqual(receipt['status'], 'PASS')
        self.assertEqual(receipt['backend'], 'native GTK')
        self.assertEqual(calls[0].args[-1], '^Colosseum.*')
        self.assertEqual(calls[3].args, ('windowfocus', '--sync', '99'))
        self.assertEqual(calls[4].args, ('key', '--clearmodifiers', 'ctrl+o'))
        self.assertEqual(calls[8].args, ('windowfocus', '--sync', '42'))
        self.assertEqual(calls[9].args, ('key', '--clearmodifiers', 'Escape'))
        self.assertEqual(calls[10].args[-1], '.*')
        self.assertEqual(calls[11].args, ('windowfocus', '--sync', '99'))
        self.assertEqual(receipt['main_window']['title'], 'Colosseum')
        self.assertEqual(receipt['chooser']['title'], 'Open Media')

    def test_no_visible_native_window_fails_with_receipt(self):
        receipt, error, _ = self.run_proof([''], [{}], clock=[0, 1, 16])
        self.assertIn('never appeared', str(error))
        self.assertFalse(receipt['opened'])
        self.assertEqual(receipt['status'], 'FAIL')

    def test_process_abort_preserves_failure_receipt(self):
        receipt, error, _ = self.run_proof([], [], returncode=-6, clock=[0, 1],
                                           poll_results=[None, -6])
        self.assertIn('Application exited opening GTK picker: -6', str(error))
        self.assertFalse(receipt['opened'])
        self.assertEqual(receipt['status'], 'FAIL')

    def test_main_window_search_result_cannot_impersonate_chooser(self):
        receipt, error, _ = self.run_proof(['99'], [], clock=[0, 1, 16])
        self.assertIn('never appeared', str(error))
        self.assertFalse(receipt['opened'])
        self.assertNotIn('chooser', receipt)
        self.assertEqual(receipt['status'], 'FAIL')

    def test_zero_geometry_window_cannot_pass(self):
        receipt, error, _ = self.run_proof(
            ['42', 'Open Media', 'WIDTH=0\nHEIGHT=480'], [{}], clock=[0, 1, 16])
        self.assertIn('never appeared', str(error))
        self.assertFalse(receipt['opened'])

    def test_original_window_still_visible_cannot_pass_cancellation(self):
        receipt, error, _ = self.run_proof(
            ['42', 'Open Media', 'WIDTH=640\nHEIGHT=480', '', '', '42'], [{}],
            clock=[0, 1, 2, 3, 18])
        self.assertIn('did not close', str(error))
        self.assertTrue(receipt['opened'])
        self.assertFalse(receipt['cancelled'])


class GtkRuntimeEvidenceTests(unittest.TestCase):
    def inspect(self, cache_module, mapped_module):
        module_dir = '/opt/colosseum/usr/lib/gdk-pixbuf-loaders'
        environment = (f'GDK_PIXBUF_MODULEDIR={module_dir}\0'
                       'GDK_PIXBUF_MODULE_FILE=/tmp/test-loaders.cache\0').encode()
        maps = ('0-1 r-xp 000 00:00 1 /opt/colosseum/usr/plugins/platformthemes/libqgtk3.so\n'
                f'1-2 r-xp 000 00:00 2 {mapped_module}\n')

        def read_text(path, *args, **kwargs):
            return maps if path.name == 'maps' else f'"{cache_module}"\n'

        with patch.object(Path, 'read_bytes', return_value=environment), \
                patch.object(Path, 'read_text', read_text), \
                patch.object(Path, 'is_file', return_value=True):
            return smoke.gtk_runtime_evidence(1234)

    def test_records_packaged_svg_origin(self):
        module = '/opt/colosseum/usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so'
        self.assertEqual(self.inspect(module, module)['mapped_svg_modules'], [module])
        with self.assertRaisesRegex(RuntimeError, 'no packaged SVG module'):
            self.inspect(module.replace('-svg.so', '-png.so'), module)

    def test_rejects_host_loader_cache_entry(self):
        with self.assertRaisesRegex(RuntimeError, 'nonpackaged modules'):
            self.inspect('/usr/lib/gdk-pixbuf/libpixbufloader-svg.so', '/usr/lib/other.so')

    def test_rejects_host_svg_mapping(self):
        with self.assertRaisesRegex(RuntimeError, 'Host SVG loader'):
            self.inspect('/opt/colosseum/usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so',
                         '/usr/lib/gdk-pixbuf/libpixbufloader-svg.so')


if __name__ == '__main__':
    unittest.main(verbosity=2)
