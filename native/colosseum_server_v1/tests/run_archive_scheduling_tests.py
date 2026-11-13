"""Generate synthetic fixture bytes and pinned queue observations for loopback tests."""
import argparse
import gzip
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile
from run_archive_tests import generate_zip_fixtures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--oracle', required=True)
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='server1-archive-scheduling-') as temporary:
        root = Path(temporary)
        generate_zip_fixtures(root)
        for filename, method in [('multi.zip', zipfile.ZIP_STORED), ('multi-deflated.zip', zipfile.ZIP_DEFLATED)]:
            with zipfile.ZipFile(root / filename, 'w', compression=method) as archive:
                archive.writestr('first.mp4', b'A' * 200000)
                archive.writestr('second.mkv', b'B' * 210000)
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w', format=tarfile.USTAR_FORMAT) as archive:
            for name, data in [('first.mp4', b'A' * 200000), ('second.mkv', b'B' * 210000)]:
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
        (root / 'multi.tar').write_bytes(buffer.getvalue())
        (root / 'multi.tgz').write_bytes(gzip.compress(buffer.getvalue(), mtime=0))
        subprocess.run([args.node, str(scripts / 'run_rar_sevenzip_oracle.cjs'), args.oracle, str(root / 'stored.json')], check=True, timeout=60)
        rows = json.loads((root / 'stored.json').read_text())['rows']
        import base64
        for name, filename in [('rar4-first', 'multi.rar'), ('7zip-second-folder', 'multi.7z')]:
            row = next(row for row in rows if row['name'] == name)
            (root / filename).write_bytes(base64.b64decode(row['parts'][0]))
        subprocess.run([args.node, str(scripts / 'run_archive_scheduling_oracle.cjs'), args.oracle, str(root / 'queue.json')], check=True, timeout=60)
        subprocess.run([args.candidate, str(root)], check=True, timeout=120)


if __name__ == '__main__':
    main()
