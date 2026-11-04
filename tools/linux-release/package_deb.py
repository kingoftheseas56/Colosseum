#!/usr/bin/env python3
"""Wrap a checked, relocated AppDir in an Ubuntu24.04/Mint22 amd64 package."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def deb_build_command(root, output):
    # Restore XZ for both archive members and select level 6 explicitly;
    # dpkg defaults (or a host's compression environment) must not grow assets.
    return ['dpkg-deb', '--root-owner-group', '-Zxz', '-z6',
            '--uniform-compression', '--build', str(root), str(output)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    stage, output = args.stage.resolve(), args.output.resolve()
    if output.exists() or Path(str(output) + '.sha256').exists():
        parser.error('Refusing to overwrite a package or checksum')
    if stage == output or stage in output.parents:
        parser.error('Package output must be outside its input AppDir')
    if not shutil.which('dpkg-deb'):
        parser.error('dpkg-deb is required on the Linux packaging host')
    identity = json.loads((stage / 'BUILD-IDENTITY.json').read_text())
    version = identity['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        parser.error('Expected a numeric release version')
    if identity.get('architecture') != 'x86_64':
        parser.error('Only the qualified amd64 target is supported')
    icon = stage / 'assets/icons/colosseum.svg'
    if not icon.is_file():
        parser.error('Application icon missing')
    with tempfile.TemporaryDirectory(prefix='colosseum-deb-') as directory:
        temporary = Path(directory)
        report = temporary / 'package-check.json'
        subprocess.run([sys.executable, str(Path(__file__).with_name('check_package.py')),
                        str(stage), '--report', str(report)], check=True)
        verified = json.loads(report.read_text())
        if verified['status'] != 'PASS':
            parser.error('AppDir verification failed')
        # Ubuntu24.04 supplies glibc2.39. A newer build must not masquerade as
        # an Ubuntu24.04 package merely because dpkg can wrap its files.
        glibc = verified['required_symbol_versions_max'].get('GLIBC')
        if glibc and tuple(map(int, glibc.split('.'))) > (2, 39):
            parser.error('Build requires newer glibc than Ubuntu24.04')
        root = temporary / 'root'
        payload = root / 'opt/colosseum'
        shutil.copytree(stage, payload, symlinks=True)
        launcher = root / 'usr/bin/colosseum'
        launcher.parent.mkdir(parents=True)
        launcher.write_text('#!/bin/sh\nexec /opt/colosseum/AppRun "$@"\n')
        launcher.chmod(0o755)
        desktop = root / 'usr/share/applications/colosseum.desktop'
        desktop.parent.mkdir(parents=True)
        desktop.write_text('[Desktop Entry]\nType=Application\nName=Colosseum\n'
                           'Comment=Read and watch your media\nExec=colosseum\n'
                           'Icon=colosseum\nTerminal=false\nCategories=AudioVideo;Graphics;\n')
        # Keep app-menu launches argument-free: --collection-url is exclusive
        # and requires a URI. Desktop MIME dispatch uses this hidden entry.
        handler = desktop.with_name('colosseum-collection-url.desktop')
        handler.write_text('[Desktop Entry]\nType=Application\nName=Colosseum Collection Link\n'
                           'Exec=colosseum --collection-url %u\nIcon=colosseum\n'
                           'Terminal=false\nNoDisplay=true\nMimeType=x-scheme-handler/colosseum;\n')
        installed_icon = root / 'usr/share/icons/hicolor/scalable/apps/colosseum.svg'
        installed_icon.parent.mkdir(parents=True)
        shutil.copy2(icon, installed_icon)
        control = root / 'DEBIAN/control'
        control.parent.mkdir()
        # Refresh the system MIME cache after configure/removal when the host
        # supplies the desktop database utility. Never set a user's defaults.
        for name, action in (('postinst', 'configure'), ('postrm', 'remove|purge')):
            hook = control.parent / name
            hook.write_text('#!/bin/sh\nset -e\ncase \"$1\" in\n'
                            f'  {action})\n'
                            '    if command -v update-desktop-database >/dev/null 2>&1; then\n'
                            '      update-desktop-database /usr/share/applications\n'
                            '    fi\n    ;;\nesac\nexit 0\n')
            hook.chmod(0o755)
        size = sum(p.stat().st_size for p in root.rglob('*') if p.is_file())
        # Qt Network (OpenSSL) and WebEngine verify HTTPS against the distro CA
        # store, which only ca-certificates provides. A hard dependency, never a
        # recommendation and never a bundled copy: distro trust-root updates must
        # keep reaching installed systems.
        # GTK's native file chooser also needs the distro's GSettings schema data,
        # supplied by libgtk-3-common.
        control.write_text(f'Package: colosseum\nVersion: {version}\nArchitecture: amd64\n'
                           'Maintainer: Colosseum Project\nSection: video\nPriority: optional\n'
                           f'Installed-Size: {(size + 1023) // 1024}\n'
                           'Depends: libc6 (>= 2.39), libgl1, libegl1, libglx0, libglvnd0, libopengl0, '
                           'libdrm2, libgbm1, libvulkan1, libva2, libva-drm2, libva-x11-2, '
                           'libasound2-data, fontconfig, fonts-dejavu-core, xdg-utils, ca-certificates, '
                           'libgtk-3-common\n'
                           'Homepage: https://getcolosseum.world\n'
                           'Description: Colosseum native media application\n'
                           ' Manga, comics, books and Theatre with optional accounts.\n'
                           ' Vinyl and Feria remain Coming Soon. Linux updates are manual.\n')
        output.parent.mkdir(parents=True, exist_ok=True)
        intermediate = temporary / 'candidate.deb'
        subprocess.run(deb_build_command(root, intermediate), check=True)
        shutil.copyfile(intermediate, output)
    with output.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    Path(str(output) + '.sha256').write_text(digest + '  ' + output.name + '\n')
    print(json.dumps({'package': str(output), 'version': version, 'architecture': 'amd64',
                      'source_commit': identity['source_commit'], 'sha256': digest,
                      'bytes': output.stat().st_size}))


if __name__ == '__main__':
    main()
