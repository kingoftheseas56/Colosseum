from __future__ import annotations

import json
import os
import stat
import sys
import tempfile
import threading
import textwrap
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

from tools.server_lab.probes.player import LibMpvAdapter, PlayerProbe


FAKE_PLAYER = textwrap.dedent(
    """
    import json, os, sys, time
    from pathlib import Path
    if '--version' in sys.argv:
        print('fake-player 7.2.1')
        raise SystemExit(0)
    event_file = Path(os.environ['COLOSSEUM_PLAYER_PROBE_EVENT_FILE'])
    time.sleep(0.05)
    events = [
        {'event': 'valid_media_decode', 'at_ms': 20},
        {'event': 'presented_frame', 'at_ms': 30},
        {'event': 'seek_complete', 'at_ms': 60, 'target_seconds': 12.5},
        {'event': 'stall', 'at_ms': 80, 'duration_ms': 40},
    ]
    event_file.write_text(''.join(json.dumps(item) + '\\n' for item in events), encoding='utf-8')
    time.sleep(0.2)
    """
)


class _RangeHandler(BaseHTTPRequestHandler):
    requests = []

    def do_GET(self):  # noqa: N802 - stdlib callback name
        self.__class__.requests.append((self.command, self.headers.get("Range")))
        self.send_response(206)
        self.send_header("Content-Range", "bytes 0-0/1")
        self.send_header("Content-Length", "1")
        self.end_headers()
        self.wfile.write(b"x")

    def do_HEAD(self):  # noqa: N802 - stdlib callback name
        self.send_error(405)

    def log_message(self, *_args):
        return


class PlayerProbeTests(unittest.TestCase):
    def test_synthetic_fixture_is_explicit_and_cannot_produce_runtime_pass(self):
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            executable = root_path / 'fake_player.py'
            executable.write_text(FAKE_PLAYER, encoding='utf-8')
            config = root_path / 'player.conf'
            config.write_text('buffer=changed\n', encoding='utf-8')
            output = root_path / 'evidence'
            receipt = PlayerProbe().run(
                executable=executable,
                media_url='http://127.0.0.1:9/media.mp4',
                config=config,
                data_root=root_path / 'data',
                evidence_dir=output,
                run_id='p07-02-green',
                case_inputs={
                    'warm_cache': True,
                    'player_buffer': 'changed',
                    'file_index': 2,
                    'peer_script': 'disabled',
                    'resource_counter': None,
                },
                http_ready=False,
                synthetic_fixture=True,
            )
            self.assertEqual(receipt['result'], 'NOT_RUN')
            self.assertEqual(receipt['configuration']['evidence_class'], 'synthetic_unit_fixture')
            observations = receipt['normalized_lane']['observations']
            self.assertEqual(observations['http_readiness']['state'], 'NOT_RUN')
            self.assertEqual(observations['first_presented_frame']['state'], 'PASS')
            self.assertEqual(observations['seek_completion']['state'], 'PASS')
            self.assertEqual(observations['stall']['state'], 'PASS')
            self.assertEqual(receipt['fixture_identifiers']['case_inputs']['file_index'], 2)
            self.assertEqual(receipt['resource_observations'][0]['state'], 'UNAVAILABLE')
            self.assertIn('fake-player 7.2.1', receipt['configuration']['player_version']['stdout'])
            self.assertTrue(receipt['configuration']['config']['sha256'])

    def test_unmarked_fake_player_cannot_produce_runtime_pass(self):
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            executable = root_path / 'fake_player.py'
            executable.write_text(FAKE_PLAYER, encoding='utf-8')
            receipt = PlayerProbe().run(
                executable=executable,
                media_url='http://127.0.0.1:9/media.mp4',
                data_root=root_path / 'data',
                evidence_dir=root_path / 'evidence',
                run_id='p07-02-unmarked-fake',
                case_inputs={'warm_cache': False, 'player_buffer': 'default', 'file_index': 0, 'peer_script': 'enabled', 'resource_counter': None},
                http_ready=False,
            )
            self.assertNotEqual(receipt['result'], 'PASS')
            self.assertEqual(receipt['source']['evidence_class'], 'unqualified_external_player')

    def test_http_readiness_uses_get_range_not_head(self):
        _RangeHandler.requests = []
        server = HTTPServer(('127.0.0.1', 0), _RangeHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            receipt = PlayerProbe._http_readiness(f'http://127.0.0.1:{server.server_port}/media.mp4', 1.0)
        finally:
            server.shutdown()
            thread.join(timeout=2)
            server.server_close()
        self.assertEqual(receipt['state'], 'PASS')
        self.assertEqual(_RangeHandler.requests, [('GET', 'bytes=0-0')])

    def test_missing_player_telemetry_is_not_promoted_to_success(self):
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            executable = root_path / 'silent_player.py'
            executable.write_text("import time; time.sleep(0.1)\n", encoding='utf-8')
            receipt = PlayerProbe().run(
                executable=executable,
                media_url='http://127.0.0.1:9/media.mp4',
                data_root=root_path / 'data',
                evidence_dir=root_path / 'evidence',
                run_id='p07-02-red',
                case_inputs={'warm_cache': False, 'player_buffer': 'default', 'file_index': 0, 'peer_script': 'enabled', 'resource_counter': 'unavailable'},
                http_ready=False,
            )
            self.assertNotEqual(receipt['result'], 'PASS')
            observations = receipt['normalized_lane']['observations']
            self.assertEqual(observations['valid_media_decode']['state'], 'UNAVAILABLE')
            self.assertEqual(observations['cancellation']['state'], 'NOT_RUN')
            self.assertTrue((root_path / 'evidence' / 'run.json').is_file())

    @unittest.skipUnless(os.environ.get('COLOSSEUM_TEST_LIBMPV_PATH'), 'pinned libmpv path not supplied')
    def test_libtmpv_adapter_exposes_native_identity(self):
        adapter = LibMpvAdapter(Path(os.environ['COLOSSEUM_TEST_LIBMPV_PATH']))
        identity = adapter.identity()
        self.assertEqual(identity['backend'], 'libmpv')
        self.assertEqual(identity['client_api'], '2.5')
        self.assertEqual(len(identity['sha256']), 64)


if __name__ == '__main__':
    unittest.main()
