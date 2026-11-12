#!/usr/bin/env python3
"""Wrap the checksum-pinned, checked 1.3.0 AppDir without rebuilding its payload."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen

HERE = Path(__file__).resolve().parent
PINS = json.loads((HERE / 'appimage-pins.json').read_text())


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            if not path.resolve().is_relative_to(root.resolve()):
                raise RuntimeError('External symlink: ' + name)
            result[name] = {'symlink': os.readlink(path)}
        elif path.is_file():
            result[name] = {'sha256': sha(path), 'mode': stat.S_IMODE(path.stat().st_mode)}
    return result


def assert_unchanged(original, final):
    for name, record in original.items():
        if final.get(name) != record:
            raise RuntimeError('Original AppDir payload changed: ' + name)
    allowed = {'colosseum.desktop', 'colosseum.svg', '.DirIcon', 'APPIMAGE-PACKAGING.json', 'APPIMAGE-RUNTIME-LICENSE.txt'}
    if set(final) - set(original) - allowed:
        raise RuntimeError('Unexpected AppImage payload additions')


def assert_audit(report, baseline):
    for name in ['status', 'source_commit', 'executable_sha256', 'qml_fingerprint', 'elf_count', 'required_symbol_versions_max']:
        expected = 'PASS' if name == 'status' else baseline[name]
        if report.get(name) != expected:
            raise RuntimeError('AppImage audit differs: ' + name)


def audit(root, report_path, baseline):
    saved = (root / 'package-check.json').read_bytes() if (root / 'package-check.json').exists() else None
    try:
        subprocess.run([sys.executable, str(HERE / 'check_package.py'), str(root), '--report', str(report_path)], check=True)
        report = json.loads(report_path.read_text())
        assert_audit(report, baseline)
        return report
    finally:
        if saved is not None:
            (root / 'package-check.json').write_bytes(saved)


def download(pin, target):
    headers = {'User-Agent': 'Colosseum-AppImage-packager/1.3.0', 'Accept': 'application/octet-stream'}
    if pin['url'].startswith('https://api.github.com/') and os.environ.get('GH_TOKEN'):
        headers['Authorization'] = 'Bearer ' + os.environ['GH_TOKEN']
    with urlopen(Request(pin['url'], headers=headers), timeout=60) as response, target.open('wb') as output:
        shutil.copyfileobj(response, output)
    if sha(target) != pin['sha256']:
        raise RuntimeError('Toolchain checksum mismatch: ' + target.name)
    target.chmod(0o755)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--baseline-report', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if sha(args.archive) != PINS['appdir_sha256']:
        raise RuntimeError('Checked AppDir archive checksum mismatch')
    baseline = json.loads(args.baseline_report.read_text())
    if baseline['status'] != 'PASS' or baseline['source_commit'] != PINS['application_source_commit']:
        raise RuntimeError('Original checked AppDir source/audit mismatch')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='colosseum-appimage-package-') as directory:
        work = Path(directory)
        listing = subprocess.check_output(['tar', '-tf', str(args.archive)], text=True).splitlines()
        if any(PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts or PurePosixPath(name).parts[0] != 'AppDir' for name in listing):
            raise RuntimeError('Unsafe AppDir archive member')
        subprocess.run(['tar', '-xf', str(args.archive), '-C', str(work)], check=True)
        root = work / 'AppDir'
        identity = json.loads((root / 'BUILD-IDENTITY.json').read_text())
        if identity['source_commit'] != PINS['application_source_commit'] or identity['version'] != '1.3.0':
            raise RuntimeError('AppDir application identity mismatch')
        original = inventory(root)
        audit(root, output / 'appdir-before-package-check.json', baseline)
        desktop = '[Desktop Entry]\nType=Application\nName=Colosseum\nComment=Read and watch your media\nExec=AppRun\nIcon=colosseum\nTerminal=false\nCategories=AudioVideo;Graphics;\n'
        (root / 'colosseum.desktop').write_text(desktop)
        shutil.copy2(root / 'assets/icons/colosseum.svg', root / 'colosseum.svg')
        subprocess.run(['desktop-file-validate', str(root / 'colosseum.desktop')], check=True)
        runtime_license = HERE / 'appimage-runtime-LICENSE.txt'
        if sha(runtime_license) != PINS['runtime']['license_sha256']:
            raise RuntimeError('Runtime license identity mismatch')
        shutil.copy2(runtime_license, root / 'APPIMAGE-RUNTIME-LICENSE.txt')
        packaged = dict(PINS, packaging_commit=os.environ.get('GITHUB_SHA'), required_symbol_versions_max=baseline['required_symbol_versions_max'], auto_update=False, zsync=False)
        (root / 'APPIMAGE-PACKAGING.json').write_text(json.dumps(packaged, indent=2) + '\n')
        tool = output / 'appimagetool-1.9.1-x86_64.AppImage'
        runtime = output / 'runtime-x86_64'
        download(PINS['appimagetool'], tool)
        download(PINS['runtime'], runtime)
        tool_dir = work / 'tool'
        tool_dir.mkdir()
        with (output / 'tool-extraction.log').open('w') as log:
            subprocess.run([str(tool), '--appimage-extract'], cwd=tool_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
        command = tool_dir / 'squashfs-root/AppRun'
        tool_version = subprocess.check_output([str(command), '--version'], stderr=subprocess.STDOUT, text=True).strip()
        image = output / 'Colosseum-1.3.0-x86_64.AppImage'
        env = os.environ.copy()
        env.update(ARCH='x86_64', VERSION='1.3.0')
        with (output / 'appimagetool.log').open('w') as log:
            subprocess.run([str(command), '--no-appstream', '--comp', 'zstd', '--runtime-file', str(runtime), str(root), str(image)], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        image.chmod(0o755)
        extracted = work / 'verify'
        extracted.mkdir()
        with (output / 'image-extraction.log').open('w') as log:
            subprocess.run([str(image), '--appimage-extract'], cwd=extracted, stdout=log, stderr=subprocess.STDOUT, check=True)
        final_root = extracted / 'squashfs-root'
        assert_unchanged(original, inventory(final_root))
        report = audit(final_root, output / 'appimage-package-check.json', baseline)
        update_info = subprocess.check_output([str(image), '--appimage-updateinformation'], text=True)
        if update_info.strip() or list(output.glob('*.zsync')):
            raise RuntimeError('Unexpected update metadata or zsync file')
        runtime_version = subprocess.check_output([str(image), '--appimage-version'], text=True, stderr=subprocess.STDOUT).strip()
        receipt = dict(packaged, appimagetool_reported_version=tool_version, runtime_reported_version=runtime_version,
                       appimage_sha256=sha(image), appimage_bytes=image.stat().st_size,
                       executable_sha256=report['executable_sha256'], qml_fingerprint=report['qml_fingerprint'],
                       elf_count=report['elf_count'], original_payload_unchanged=True)
        (output / 'AppImage-identity.json').write_text(json.dumps(receipt, indent=2) + '\n')
        (output / (image.name + '.sha256')).write_text(receipt['appimage_sha256'] + '  ' + image.name + '\n')
        (output / 'original-appdir-files.json').write_text(json.dumps(original, indent=2) + '\n')
        print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
