#!/usr/bin/env python3
"""Exercise the actual AppImage runtime using the existing GUI/player proofs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import shlex
import shutil
import subprocess
import sys
import time

import smoke_installed_player as player
import smoke_installed_runtime as runtime
from package_appimage import assert_audit

TEMP = Path('/tmp/colosseum-appimage-130')
MOUNT_ALIAS = Path('/opt/colosseum')
LAUNCHER = Path('/usr/bin/colosseum')


def cache_path(image):
    # The pinned type2 runtime uses MD5 solely to name its extraction cache.
    # Security/identity checks below always use SHA256.
    with image.open('rb') as stream:
        key = hashlib.file_digest(stream, 'md5').hexdigest()
    return TEMP / ('appimage_extracted_' + key)


def prepare(image, output):
    if os.getuid() != 0:
        raise RuntimeError('Preparation requires the isolated container root')
    account = pwd.getpwnam('appimage-test')
    TEMP.mkdir(mode=0o700)
    os.chown(TEMP, account.pw_uid, account.pw_gid)
    output.mkdir(parents=True)
    os.chown(output, account.pw_uid, account.pw_gid)
    if os.path.lexists(MOUNT_ALIAS) or os.path.lexists(LAUNCHER):
        raise RuntimeError('A system Colosseum payload/launcher already exists')
    MOUNT_ALIAS.parent.mkdir(exist_ok=True)
    MOUNT_ALIAS.symlink_to(cache_path(image), target_is_directory=True)
    LAUNCHER.write_text('#!/bin/sh\nexport TMPDIR=' + shlex.quote(str(TEMP)) + '\nexport NO_CLEANUP=1\nexec ' + shlex.quote(str(image)) + ' --appimage-extract-and-run "$@"\n')
    LAUNCHER.chmod(0o755)
    image.chmod(0o755)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', type=Path, required=True)
    ap.add_argument('--identity', type=Path, required=True)
    ap.add_argument('--baseline-report', type=Path, required=True)
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--prepare', action='store_true')
    args = ap.parse_args()
    image, output = args.image.resolve(), args.output.resolve()
    identity = json.loads(args.identity.read_text())
    if player.sha256_file(image) != identity['appimage_sha256']:
        raise RuntimeError('AppImage checksum mismatch')
    if args.prepare:
        prepare(image, output)
        return 0
    if os.getuid() == 0:
        raise RuntimeError('AppImage GUI qualification must run as a normal user')
    if os.environ.get('TMPDIR') != str(TEMP):
        raise RuntimeError('Isolated extraction/temp root missing')
    receipt = {'status': 'FAIL', 'image_identity': identity,
               'os_release': Path('/etc/os-release').read_text(),
               'uid': os.getuid(), 'display': ':97', 'launch_mode': '--appimage-extract-and-run',
               'scope': 'Actual AppImage frontend and original installed-QML/player drivers through an alias to its own extraction root; software GL. No SDK, rebuilt application, sandbox override or DEB install.'}
    xvfb = None
    try:
        os.environ['DISPLAY'] = ':97'
        with (output / 'xvfb.log').open('w') as log:
            xvfb = subprocess.Popen(['Xvfb', ':97', '-screen', '0', '1920x1080x24', '-nolisten', 'tcp', '-ac'], stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 15
            while not Path('/tmp/.X11-unix/X97').exists():
                if xvfb.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Xvfb did not start')
                time.sleep(.1)
            default_env = os.environ.copy()
            default_env.pop('APPIMAGE_EXTRACT_AND_RUN', None)
            normal = subprocess.run([str(image), '--version'], env=default_env, capture_output=True, text=True, timeout=45)
            normal_text = normal.stdout + normal.stderr
            (output / 'default-launch.log').write_text(normal_text)
            receipt['default_launch'] = {'returncode': normal.returncode, 'fuse_device_present': Path('/dev/fuse').exists()}
            if normal.returncode and 'fuse' not in normal_text.lower():
                raise RuntimeError('Default AppImage launch failed for a non-FUSE reason')
            if normal.returncode == 0 and normal.stdout.strip() != 'Colosseum 1.3.0':
                raise RuntimeError('Default AppImage version mismatch')
            version = subprocess.check_output([str(LAUNCHER), '--version'], text=True, timeout=90).strip()
            if version != 'Colosseum 1.3.0':
                raise RuntimeError('Extract-and-run version mismatch')
            extracted = cache_path(image)
            if MOUNT_ALIAS.resolve() != extracted.resolve():
                raise RuntimeError('Harness alias does not refer to actual AppImage extraction root')
            original_audit = (extracted / 'package-check.json').read_bytes()
            try:
                subprocess.run([sys.executable, str(Path(__file__).with_name('check_package.py')), str(extracted), '--report', str(output / 'appimage-package-check.json')], check=True)
                report = json.loads((output / 'appimage-package-check.json').read_text())
                assert_audit(report, json.loads(args.baseline_report.read_text()))
            finally:
                (extracted / 'package-check.json').write_bytes(original_audit)

            def image_identity():
                meta = json.loads((MOUNT_ALIAS / 'BUILD-IDENTITY.json').read_text())
                binary_sha = player.sha256_file(MOUNT_ALIAS / 'usr/bin/colosseum')
                if meta['source_commit'] != identity['application_source_commit'] or binary_sha != identity['executable_sha256']:
                    raise RuntimeError('Actual AppImage payload source/binary mismatch')
                return {'format': 'AppImage', 'version': meta['version'], 'architecture': meta['architecture'],
                        'source_commit': meta['source_commit'], 'binary_sha256': binary_sha,
                        'appimage_sha256': identity['appimage_sha256'], 'payload_root': str(extracted)}

            player.installed_identity = image_identity
            runtime.installed_identity = image_identity
            original_run_phase = runtime.run_phase

            def run_phase(phase, *arguments):
                if phase == 'restart':
                    # Recreate the payload, retaining only the real XDG profile.
                    # This prevents NO_CLEANUP from making profile persistence pass.
                    if extracted.parent != TEMP or not extracted.name.startswith('appimage_extracted_'):
                        raise RuntimeError('Unsafe test-cache cleanup path')
                    shutil.rmtree(extracted)
                return original_run_phase(phase, *arguments)

            runtime.run_phase = run_phase
            original_args = sys.argv
            try:
                sys.argv = ['player-proof', '--fixture', str(args.fixture.resolve()), '--output', str(output)]
                if player.main() != 0:
                    raise RuntimeError('AppImage local mpv playback proof failed')
                sys.argv = ['runtime-proof', '--output', str(output)]
                if runtime.main() != 0:
                    raise RuntimeError('AppImage Home/WebEngine/profile restart proof failed')
            finally:
                sys.argv = original_args
            receipt.update(status='PASS', payload_reextracted_before_restart=True,
                           default_fuse_launch='PASS' if normal.returncode == 0 else 'UNAVAILABLE: see default-launch.log',
                           extract_and_run='PASS', final_identity=image_identity())
            if player.sha256_file(image) != identity['appimage_sha256']:
                raise RuntimeError('Test changed the AppImage file')
    except Exception as error:
        receipt.update(status='FAIL', error=str(error))
    finally:
        if xvfb is not None and xvfb.poll() is None:
            xvfb.terminate()
            xvfb.wait(timeout=10)
        (output / 'appimage-runtime-proof.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt, indent=2))
    return 0 if receipt['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
