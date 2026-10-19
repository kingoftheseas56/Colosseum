import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from distro import paragraphs, source_identity, source_acquire, binary_files
from inventory import collect, safe_name
from upstream import acquire, compare_archive
from verify_cache import verify
from inventory import PACKAGE_SHA256, write_json


class CollectorTests(unittest.TestCase):
    def test_rejects_wrong_package_before_extracting(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = root / 'wrong.deb'
            package.write_bytes(b'not accepted')
            with self.assertRaisesRegex(ValueError, 'exact accepted'):
                collect(package, root / 'out')
            self.assertFalse((root / 'out').exists())

    def test_rejects_archive_path_escape(self):
        for name in ('../escape', '/absolute', './x/../../escape', 'a\\escape'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_name(name)

    def test_source_control_defaults_and_explicit_version(self):
        self.assertEqual(source_identity({'Package': 'liba', 'Version': '1:2-3'}), ('liba', '1:2-3'))
        self.assertEqual(source_identity({'Package': 'liba', 'Version': '2-3+b1', 'Source': 'a (2-3)'}), ('a', '2-3'))

    def test_deb822_multiline_and_epoch(self):
        rows = list(paragraphs('Package: a\nVersion: 1:2\nChecksums-Sha256:\n abc 3 a.tar.xz\n\nPackage: b\nVersion: 4\n'))
        self.assertEqual(len(rows), 2)
        self.assertIn('abc 3 a.tar.xz', rows[0][1]['Checksums-Sha256'])

    def test_missing_exact_source_is_unresolved(self):
        with tempfile.TemporaryDirectory() as tmp:
            commands = unittest.mock.Mock()
            commands.run.return_value = 'Package: a\nVersion: 999\n'
            with self.assertRaisesRegex(ValueError, 'Exact authenticated Sources stanza unavailable'):
                source_acquire('a', '1', Path(tmp), commands)
            self.assertEqual(commands.run.call_count, 1)

    def test_source_checksum_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            commands = unittest.mock.Mock()
            root = Path(tmp)
            directory = root / 'sources/a_1'
            def run(argv, cwd=None):
                if argv[0] == 'apt-cache':
                    return 'Package: a\nVersion: 1\nChecksums-Sha256:\n ' + '0' * 64 + ' 3 a.dsc\n'
                if '--print-uris' in argv:
                    return "'https://archive.ubuntu.com/pool/a.dsc' a.dsc 3 SHA256:bad\n"
                (directory / 'a.dsc').write_bytes(b'bad')
                return ''
            commands.run.side_effect = run
            with self.assertRaisesRegex(ValueError, 'checksum/size mismatch'):
                source_acquire('a', '1', root, commands)

    def test_upstream_wrong_checksum_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            def fake_download(url, target):
                target.write_bytes(b'wrong')
                return url
            with patch('upstream.download', side_effect=fake_download), self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                acquire('https://example.invalid/a.tar.xz', Path(tmp), '0' * 64)

    def test_archive_git_blob_comparison_exposes_missing_and_changed(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'source.tar.gz'
            with tarfile.open(archive, 'w:gz') as tar:
                for name, data in [('same', b'abc'), ('changed', b'new')]:
                    info = tarfile.TarInfo('root/' + name)
                    info.size = len(data)
                    tar.addfile(info, io.BytesIO(data))
            blob = hashlib.sha1(b'blob 3\0abc').hexdigest()
            result = compare_archive(archive, {'same': blob, 'changed': blob, 'absent': blob})
            self.assertEqual(result['matched_git_blobs'], 1)
            self.assertEqual(result['different_git_blobs'], ['changed'])
            self.assertEqual(result['absent_git_paths'], ['absent'])

    def test_archive_doc_symlink_retains_exact_original_host_copyright(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = root / 'host/usr/share/doc/base/copyright'
            original.parent.mkdir(parents=True)
            original.write_bytes(b'original license text')
            payload = io.BytesIO()
            with tarfile.open(fileobj=payload, mode='w') as tar:
                link = tarfile.TarInfo('./usr/share/doc/liba')
                link.type = tarfile.SYMTYPE
                link.linkname = 'base'
                tar.addfile(link)
            payload.seek(0)
            process = unittest.mock.Mock(stdout=payload)
            process.wait.return_value = 0
            with patch('distro.subprocess.Popen', return_value=process):
                binary_files(root / 'a.deb', set(), root / 'binary/copyrights', root / 'host')
            copied = root / 'binary/copyrights/retained-original-host/usr/share/doc/base/copyright'
            self.assertEqual(copied.read_bytes(), original.read_bytes())

    def test_archive_doc_symlink_escape_cannot_import_other_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = io.BytesIO()
            with tarfile.open(fileobj=payload, mode='w') as tar:
                link = tarfile.TarInfo('./usr/share/doc/liba')
                link.type = tarfile.SYMTYPE
                link.linkname = '../../../../private'
                tar.addfile(link)
            payload.seek(0)
            process = unittest.mock.Mock(stdout=payload)
            process.wait.return_value = 0
            with patch('distro.subprocess.Popen', return_value=process):
                binary_files(root / 'a.deb', set(), root / 'binary/copyrights', root / 'host')
            self.assertFalse((root / 'binary/copyrights').exists())

    def test_cached_input_hash_mismatch_is_fatal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_json(root / 'package-identity.json', {'sha256': PACKAGE_SHA256})
            (root / 'source').write_bytes(b'changed')
            write_json(root / 'SHA256-INDEX.json', [{'path': 'source', 'bytes': 7, 'sha256': '0' * 64}])
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                verify(root)

    def test_command_receipts_resume_without_overwriting(self):
        from distro import Commands
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'commands').mkdir()
            (root / 'commands/0012.txt').write_text('original evidence')
            self.assertEqual(Commands(root).sequence, 12)


if __name__ == '__main__':
    unittest.main()
