#!/usr/bin/env python3
"""Receive one pinned compiled alpha; produce matching icon-equipped ZIP + DMG.
No compilation, credential logging, release upload, or notarization.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = 'kingoftheseas56/Colosseum'
RUN = 41285194815
ARTIFACT = 13075027592
SOURCE = 'a9a5df10f97fb5e22d35a502a5e6a17731c093f0'
OUTER_SIZE = 480793393
OUTER_SHA = 'b853883d1c0b1e3479ae1336fc4495043ead75af64a0808aa69313c951139b77'
INNER_SIZE = 474662571
INNER_SHA = 'e55f6b338985ff14e08395f0b22c242a39adb0734097515ca38842d98e5e1f86'
SVG_SHA = '04fe4d6dbd31d0f1d3d34e35a01e67d0f63a4f80c8196236d19c06afedffe13c'
PNG_SHA = '8335a88ee4c16662e95228300794ac575c0ab9f6441526deaeb78049508e1c6b'
ZIP_NAME = 'Colosseum-1.2.0-macOS-arm64-preview.zip'
BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('preview', BASE.parent / 'macos-build/package-preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2))


def check_file(path, size, digest):
    if path.stat().st_size != size or preview.sha256_file(path) != digest:
        raise RuntimeError('Pinned file size/SHA256 mismatch: ' + path.name)


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if redirected is not None:
            redirected.remove_header('Authorization')
        return redirected


def github(path, destination=None):
    request = urllib.request.Request('https://api.github.com/repos/' + REPO + path,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.build_opener(SafeRedirect).open(request, timeout=120) as response:
        if destination:
            with destination.open('xb') as target:
                shutil.copyfileobj(response, target, 1024 * 1024)
        else:
            return json.load(response)


def validate_metadata(run, artifact):
    if (run.get('id') != RUN or run.get('head_sha') != SOURCE
            or run.get('conclusion') != 'success'
            or run.get('repository', {}).get('full_name') != REPO
            or artifact.get('id') != ARTIFACT or artifact.get('expired') is not False
            or artifact.get('name') != 'Colosseum-1.2.0-macOS-arm64-preview'
            or artifact.get('size_in_bytes') != OUTER_SIZE
            or artifact.get('digest') != 'sha256:' + OUTER_SHA
            or artifact.get('workflow_run', {}).get('id') != RUN
            or artifact.get('workflow_run', {}).get('head_sha') != SOURCE):
        raise RuntimeError('Pinned source/run/artifact identity mismatch')


def extract_outer(archive, output):
    with zipfile.ZipFile(archive) as z:
        names = set()
        for member in z.infolist():
            name = member.filename
            if (name in names or '/' in name or '\\' in name or name in ('', '.', '..')
                    or stat.S_ISLNK(member.external_attr >> 16) or member.is_dir()
                    or member.file_size > OUTER_SIZE):
                raise RuntimeError('Unsafe outer artifact entry')
            names.add(name)
        if sum(m.file_size for m in z.infolist()) > 2 * OUTER_SIZE or z.testzip():
            raise RuntimeError('Outer artifact CRC/size failure')
        z.extractall(output)


def tree_manifest(root):
    manifest = {}
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            if not preview.within(path, root):
                raise RuntimeError('External application symlink: ' + relative)
            manifest[relative] = ['link', os.readlink(path)]
        elif path.is_file():
            manifest[relative] = ['file', preview.sha256_file(path), stat.S_IMODE(path.stat().st_mode)]
        elif path.is_dir():
            manifest[relative] = ['directory']
        else:
            raise RuntimeError('Unsupported application entry')
    return manifest


def verify_changes(before, after, work):
    old, new = tree_manifest(before), tree_manifest(after)
    icon = 'Contents/Resources/Colosseum.icns'
    if set(new) - set(old) != {icon} or set(old) - set(new):
        raise RuntimeError('Unexpected icon packaging file additions/removals')
    a = plistlib.loads((before / 'Contents/Info.plist').read_bytes())
    b = plistlib.loads((after / 'Contents/Info.plist').read_bytes())
    a['CFBundleIconFile'] = 'Colosseum.icns'
    if a != b:
        raise RuntimeError('Info.plist changed beyond icon declaration')
    changes = []
    for name in old:
        if old[name] == new[name]:
            continue
        changes.append(dict(path=name, original=old[name], final=new[name]))
        if name == 'Contents/Info.plist' or name.endswith('/_CodeSignature/CodeResources'):
            continue
        first, second = before / name, after / name
        if (old[name][0] != 'file' or new[name][0] != 'file'
                or old[name][2] != new[name][2]
                or first.read_bytes()[:4] not in preview.MACH_MAGICS):
            raise RuntimeError('Non-signature resource changed: ' + name)
        # Signing can change LC_CODE_SIGNATURE and embedded signature bytes.
        # Strip signatures only on disposable copies; require all remaining
        # compiled bytes identical, never modify the deliverable for comparison.
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            copies = []
            for index, source in enumerate((first, second)):
                copy = Path(tmp) / str(index)
                shutil.copy2(source, copy)
                preview.run('codesign', '--remove-signature', copy)
                copies.append(preview.sha256_file(copy))
            if copies[0] != copies[1]:
                raise RuntimeError('Compiled Mach-O bytes changed: ' + name)
    return dict(status='PASS', source=SOURCE, added=[icon], changed=changes,
                policy='Only icon, Info.plist icon key and signature metadata may differ; unsigned Mach-O bytes must match')


def install_icon(app, source, work):
    svg = source / 'assets/icons/colosseum.svg'
    png = BASE / 'assets/colosseum-icon-1024.png'
    if preview.sha256_file(svg) != SVG_SHA or preview.sha256_file(png) != PNG_SHA:
        raise RuntimeError('Icon source/render hash mismatch')
    iconset = work / 'Colosseum.iconset'
    iconset.mkdir()
    for points in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            pixels = points * scale
            suffix = '@2x' if scale == 2 else ''
            preview.run('sips', '-z', pixels, pixels, png, '--out', iconset / f'icon_{points}x{points}{suffix}.png')
    icon = app / 'Contents/Resources/Colosseum.icns'
    preview.run('iconutil', '-c', 'icns', iconset, '-o', icon)
    if icon.read_bytes()[:4] != b'icns':
        raise RuntimeError('Invalid generated ICNS')
    path = app / 'Contents/Info.plist'
    info = plistlib.loads(path.read_bytes())
    info['CFBundleIconFile'] = icon.name
    path.write_bytes(plistlib.dumps(info))
    return dict(svg_sha256=SVG_SHA, png_sha256=PNG_SHA, icns_sha256=preview.sha256_file(icon),
                render='Exact existing SVG rendered transparently at1024px using Inkscape1.4; native sips/iconutil sizes')


def main():
    output = Path(sys.argv[1]).resolve()
    output.mkdir(exist_ok=False)
    report = dict(status='FAIL', compiled_source=SOURCE,
                  packaging_commit=os.environ['GITHUB_SHA'], notarized=False, signing='ad-hoc')
    try:
        run = github(f'/actions/runs/{RUN}')
        artifact = github(f'/actions/artifacts/{ARTIFACT}')
        validate_metadata(run, artifact)
        write_json(output / 'input-provenance.json', dict(run_id=RUN, artifact=artifact, compiled_source=SOURCE))
        with tempfile.TemporaryDirectory(prefix='colosseum-distribution-') as tmp:
            work = Path(tmp)
            outer = work / 'input.zip'
            github(f'/actions/artifacts/{ARTIFACT}/zip', outer)
            check_file(outer, OUTER_SIZE, OUTER_SHA)
            evidence = work / 'received'
            extract_outer(outer, evidence)
            original_zip = evidence / ZIP_NAME
            check_file(original_zip, INNER_SIZE, INNER_SHA)
            preview.verify_archive(original_zip)
            original_root = work / 'original'
            preview.run('ditto', '-x', '-k', original_zip, original_root)
            original = original_root / 'Colosseum.app'
            identity = json.loads((original / 'Contents/Resources/BUILD-IDENTITY.json').read_text())
            if identity['source_sha'] != SOURCE:
                raise RuntimeError('Compiled bundle source mismatch')
            preview.run('codesign', '--verify', '--deep', '--strict', '--verbose=2', original)
            stage = work / 'stage'
            stage.mkdir()
            app = stage / 'Colosseum.app'
            preview.run('ditto', original, app)
            report['icon'] = install_icon(app, Path(os.environ['GITHUB_WORKSPACE']), work)
            preview.run('codesign', '--force', '--deep', '--sign', '-', '--preserve-metadata=identifier,entitlements', app)
            preview.run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
            report['immutable_content'] = verify_changes(original, app, work)
            write_json(output / 'dependency-audit.json', preview.audit(app))
            preview.verify_webengine_entitlements(app, output / 'signing-entitlements.json')
            archive = output / ZIP_NAME
            preview.run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
            write_json(output / 'archive-audit.json', preview.verify_archive(archive))
            with tempfile.TemporaryDirectory(dir=work) as zip_check:
                preview.run('ditto', '-x', '-k', archive, zip_check)
                zip_manifest = tree_manifest(Path(zip_check) / 'Colosseum.app')
                if zip_manifest != tree_manifest(app):
                    raise RuntimeError('Fresh ZIP extraction differs from signed staged app')
            report['app_tree_sha256'] = hashlib.sha256(json.dumps(zip_manifest, sort_keys=True).encode()).hexdigest()
            archive.with_suffix('.zip.sha256').write_text(preview.sha256_file(archive) + '  ' + archive.name + '\n')
            (stage / 'Applications').symlink_to('/Applications')
            dmg = output / 'Colosseum-1.2.0-macOS-arm64-preview.dmg'
            preview.run('hdiutil', 'create', '-volname', 'Colosseum 1.2.0', '-srcfolder', stage,
                        '-format', 'UDZO', '-fs', 'HFS+', dmg)
            preview.run('hdiutil', 'verify', dmg)
            mount = work / 'mounted'
            mount.mkdir()
            mounted = False
            try:
                preview.run('hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, dmg)
                mounted = True
                if not (mount / 'Applications').is_symlink() or os.readlink(mount / 'Applications') != '/Applications':
                    raise RuntimeError('DMG Applications shortcut mismatch')
                report['dmg_root_entries'] = sorted(p.name for p in mount.iterdir())
                if any(not name.startswith('.') and name not in ('Colosseum.app', 'Applications')
                       for name in report['dmg_root_entries']):
                    raise RuntimeError('Unexpected DMG visible root entry')
                if zip_manifest != tree_manifest(mount / 'Colosseum.app'):
                    raise RuntimeError('Mounted DMG app differs from ZIP source app')
                preview.run('codesign', '--verify', '--deep', '--strict', '--verbose=2', mount / 'Colosseum.app')
                preview.verify_webengine_entitlements(mount / 'Colosseum.app', output / 'dmg-signing-entitlements.json')
                report['dmg_mount'] = 'PASS: read-only mount, exact app tree and Applications shortcut'
            finally:
                if mounted:
                    preview.run('hdiutil', 'detach', mount)
            dmg.with_suffix('.dmg.sha256').write_text(preview.sha256_file(dmg) + '  ' + dmg.name + '\n')
            preview.smoke(output)
            report['files'] = {p.name: dict(bytes=p.stat().st_size, sha256=preview.sha256_file(p)) for p in (archive, dmg)}
            report['status'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        write_json(output / 'distribution-report.json', report)


if __name__ == '__main__':
    main()
