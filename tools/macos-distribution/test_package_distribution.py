import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import urllib.request
import zipfile

spec = importlib.util.spec_from_file_location('distribution', Path(__file__).with_name('package-distribution.py'))
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)


class DistributionTests(unittest.TestCase):
    def metadata(self):
        run = dict(id=d.RUN, head_sha=d.SOURCE, conclusion='success', repository=dict(full_name=d.REPO))
        artifact = dict(id=d.ARTIFACT, expired=False, name='Colosseum-1.2.0-macOS-arm64-preview',
                        size_in_bytes=d.OUTER_SIZE, digest='sha256:' + d.OUTER_SHA,
                        workflow_run=dict(id=d.RUN, head_sha=d.SOURCE))
        return run, artifact

    def test_pinned_metadata_and_mismatches(self):
        run, artifact = self.metadata()
        d.validate_metadata(run, artifact)
        for key, wrong in [('id', 1), ('head_sha', 'wrong'), ('conclusion', 'failure')]:
            changed = dict(run, **{key: wrong})
            with self.assertRaises(RuntimeError): d.validate_metadata(changed, artifact)
        for key, wrong in [('id', 1), ('expired', True), ('size_in_bytes', 1), ('digest', 'bad')]:
            with self.assertRaises(RuntimeError): d.validate_metadata(run, dict(artifact, **{key: wrong}))

    def test_redirect_does_not_forward_token(self):
        request = urllib.request.Request('https://api.github.com/example', headers={'Authorization': 'Bearer test'})
        redirected = d.SafeRedirect().redirect_request(request, None, 302, 'Found', {}, 'https://blob.example/file')
        self.assertIsNone(redirected.get_header('Authorization'))

    def test_outer_rejects_traversal_and_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for index, names in enumerate([['../escape'], ['a', 'a'], ['/absolute']]):
                archive = root / f'{index}.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    for name in names: z.writestr(name, 'x')
                with self.assertRaises(RuntimeError): d.extract_outer(archive, root / 'out')

    def fixtures(self, root):
        before, after = root / 'before', root / 'after'
        for app in (before, after):
            (app / 'Contents/Resources').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIconFile='', CFBundleVersion='1.2.0')))
            (app / 'Contents/Resources/Main.qml').write_text('original qml')
        (after / 'Contents/Resources/Colosseum.icns').write_bytes(b'icns')
        (after / 'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIconFile='Colosseum.icns', CFBundleVersion='1.2.0')))
        return before, after

    def test_only_icon_changes_are_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            before, after = self.fixtures(Path(tmp))
            self.assertEqual(d.verify_changes(before, after, Path(tmp))['status'], 'PASS')
            (after / 'Contents/Resources/Main.qml').write_text('changed qml')
            with self.assertRaises(RuntimeError): d.verify_changes(before, after, Path(tmp))

    def test_unrelated_plist_changes_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            before, after = self.fixtures(Path(tmp))
            p = after / 'Contents/Info.plist'; info = plistlib.loads(p.read_bytes());info['CFBundleVersion'] = '9';p.write_bytes(plistlib.dumps(info))
            with self.assertRaises(RuntimeError): d.verify_changes(before, after, Path(tmp))

    def test_external_app_link_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'external').symlink_to('/Applications')
            with self.assertRaises(RuntimeError): d.tree_manifest(root)

    def test_changed_macho_requires_unsigned_byte_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); before, after = self.fixtures(root)
            for app, signature in [(before, b'old'), (after, b'new')]:
                (app / 'Contents/binary').write_bytes(b'\xcf\xfa\xed\xfeCODE' + signature)
            def strip(*args):
                p = Path(args[-1]);p.write_bytes(p.read_bytes()[:-3])
            with patch.object(d.preview, 'run', side_effect=strip):
                self.assertEqual(d.verify_changes(before, after, root)['status'], 'PASS')
                (after / 'Contents/binary').write_bytes(b'\xcf\xfa\xed\xfeEVILnew')
                with self.assertRaises(RuntimeError): d.verify_changes(before, after, root)


if __name__ == '__main__': unittest.main()
