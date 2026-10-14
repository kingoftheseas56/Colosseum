import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('content_load', Path(__file__).with_name('content_load.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

class ContentLoadTests(unittest.TestCase):
    def test_preparation_reference_integrity_and_determinism(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root)
            repo, assets, out = p / 'repo', p / 'assets', p / 'out'
            (repo / 'qml').mkdir(parents=True)
            assets.mkdir()
            for source in set(sum(c.TRIBUTES.values(), [])):
                (repo / 'qml' / source).write_text('// embedded source', encoding='utf-8')
            (repo / 'qml/Universes.js').write_text('    { name: "One Piece", c1: "#000", category: "onepiece",\n', encoding='utf-8')
            c.write_json(assets / 'one-piece.json', {'universe': {'id': 'one-piece', 'sections': [
                {'id': 'a', 'kind': 'video', 'entries': [{'id': 'tt1'}]},
                {'id': 'b', 'kind': 'book', 'entries': [{'id': 'book1'}]}]}})
            manifest = c.prepare(repo, assets, out)
            before = (out / 'manifest.json').read_bytes()
            c.prepare(repo, assets, out)
            self.assertEqual(before, (out / 'manifest.json').read_bytes())
            route = manifest['routes'][0]
            self.assertEqual(len(route['requested_units']), 2)
            self.assertEqual(len(route['available_units']), 3)
            ids = {u['id'] for u in manifest['units']}
            traces = json.loads((out / 'request-traces.json').read_bytes())
            for case in traces['cases']:
                self.assertTrue(set(case['resource_ids']) <= ids)
                self.assertIsNone(case['http_mapping'])
            for unit in manifest['units']:
                self.assertEqual(c.digest((out / unit['body_path']).read_bytes()), unit['sha256'])

    def test_cache_contracts_are_distinct_and_no_host_is_invented(self):
        self.assertEqual(len(c.CASES), 6)
        self.assertIn('304 zero body', c.CASES['warm-etag']['expect'])
        self.assertIn('retain last-good', c.CASES['unsupported-renderer']['expect'])

    def test_json_body_bound(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(ValueError):
                c.write_json(Path(root) / 'x.json', {'value': 'x' * c.MAX_BODY})

    def test_dimensions(self):
        self.assertEqual(c.image_info(b'\x89PNG\r\n\x1a\n' + b'\0' * 8 + struct.pack('>II', 640, 480), '.png'), ('image/png', [640, 480]))
        self.assertEqual(c.image_info(b'<svg viewBox="0 0 100 50"/>', '.svg'), ('image/svg+xml', [100.0, 50.0]))

    def test_hash_mismatch_and_path_escape_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root)
            (p / 'x.json').write_bytes(b'{}')
            item = {'id': 'x', 'body_path': 'x.json', 'bytes': 2, 'sha256': 'wrong'}
            c.write_json(p / 'manifest.json', {'units': [item], 'assets': []})
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                c.measure(p, 1)
            item['body_path'] = '../outside.json'
            c.write_json(p / 'manifest.json', {'units': [item], 'assets': []})
            with self.assertRaisesRegex(ValueError, 'escapes'):
                c.measure(p, 1)

if __name__ == '__main__':
    unittest.main()
