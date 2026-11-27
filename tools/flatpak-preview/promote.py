#!/usr/bin/env python3
"""Add distribution metadata to the verified 1.5.0 candidate without rebuilding it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import stat
import subprocess

APP_ID = 'io.github.kingoftheseas56.Colosseum'
BRANCH = 'preview-1.5.0'
SOURCE = '0f6c3c9c2d799b1ac2889806a238fa6bea3a41ce'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def elf_inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as stream:
            is_elf = stream.read(4) == b'\x7fELF'
        if is_elf:
            result[path.relative_to(root).as_posix()] = digest(path)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path, help='Installed candidate deployment directory')
    parser.add_argument('material', type=Path, help='Collected license/source distribution material')
    parser.add_argument('output', type=Path, help='New task-owned output directory')
    args = parser.parse_args()
    candidate, material, output = (p.resolve() for p in (args.candidate,args.material,args.output))
    if output.exists():
        raise SystemExit('Output must be a new directory')
    original = candidate/'files'
    if (original/'share/colosseum/SOURCE-COMMIT').read_text().strip() != SOURCE:
        raise SystemExit('Wrong application source snapshot')
    output.mkdir(parents=True)
    build = output/'build'
    shutil.copytree(original, build/'files', symlinks=True)
    shutil.copyfile(candidate/'metadata',build/'metadata')
    for path in [build,*(p for p in build.rglob('*') if p.is_dir() and not p.is_symlink())]:
        path.chmod(path.stat().st_mode | stat.S_IWUSR)
    files = build/'files'
    proofs = files/'share/colosseum/proofs'
    if proofs.exists():
        shutil.copytree(proofs,output/'qualification-drivers',symlinks=True)
        shutil.rmtree(proofs)

    def write(relative,text):
        path = files/relative
        path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists() or path.is_symlink():
            path.unlink()
        path.write_text(text,encoding='utf-8')

    write(f'share/applications/{APP_ID}.desktop',f'''[Desktop Entry]
Type=Application
Name=Colosseum (Flatpak Preview)
Comment=Read, watch and listen with Colosseum
Exec=colosseum
Icon={APP_ID}
Terminal=false
StartupNotify=true
Categories=AudioVideo;
''')
    icon = files/f'share/icons/hicolor/scalable/apps/{APP_ID}.svg'
    icon.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(material/'colosseum.svg',icon)
    write(f'share/metainfo/{APP_ID}.metainfo.xml',f'''<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>{APP_ID}</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MIT</project_license>
  <name>Colosseum Flatpak Preview</name>
  <summary>Read, watch and listen across your media library</summary>
  <description><p>Colosseum brings comics, books, audiobooks and video together in a native desktop application.</p><p>This is the independently distributed Linux Flatpak preview. DLNA sharing is unavailable in this package.</p></description>
  <launchable type="desktop-id">{APP_ID}.desktop</launchable>
  <url type="homepage">https://getcolosseum.world/</url>
  <url type="bugtracker">https://github.com/kingoftheseas56/Colosseum/issues</url>
  <developer id="io.github.kingoftheseas56"><name>Hemanth Ganneni</name></developer>
  <releases><release version="1.5.0-preview.1" date="2026-11-28" type="development"/></releases>
</component>
''')
    licenses = files/f'share/licenses/{APP_ID}/distribution'
    shutil.copytree(material/'license-material',licenses,dirs_exist_ok=True)
    shutil.copyfile(material/'source-inventory.json',licenses/'source-inventory.json')
    shutil.copyfile(material/'SOURCE-LOCATIONS.md',licenses/'SOURCE-LOCATIONS.md')
    shutil.copyfile(material/'DISTRIBUTION-NOTICES.md',licenses/'DISTRIBUTION-NOTICES.md')
    provenance = {'application_source':SOURCE,'version':'1.5.0','channel':'Flatpak Preview',
                  'branch':BRANCH,'original_candidate_commit':'46f17a43b8892057ad15f9d31d92f2d3308c8d0f640597fee33282ac875b8c2a',
                  'application_rebuilt':False,'runtime_repository':'https://dl.flathub.org/repo/flathub.flatpakrepo',
                  'flathub_application_listing':False,'dlna_sharing':False}
    write('share/colosseum/FLATPAK-PREVIEW-PROVENANCE.json',json.dumps(provenance,indent=2)+'\n')
    original_elf, promoted_elf = elf_inventory(original),elf_inventory(files)
    if not original_elf or original_elf != promoted_elf:
        raise SystemExit('ELF payload changed during promotion')
    qml_root = Path('share/colosseum/qml')
    qml_files = {p.relative_to(original).as_posix():digest(p) for p in (original/qml_root).rglob('*') if p.is_file()}
    if any(digest(files/name) != value for name,value in qml_files.items()):
        raise SystemExit('Application QML payload changed during promotion')
    (output/'unchanged-payload.json').write_text(json.dumps({'elf_count':len(original_elf),
        'qml_file_count':len(qml_files),'elf_sha256':original_elf,'qml_sha256':qml_files},indent=2)+'\n')
    repo = output/'repo'
    subprocess.run(['flatpak','build-finish','--command=colosseum',str(build)],check=True)
    subprocess.run(['flatpak','build-export','--subject=Colosseum 1.5.0 Flatpak Preview',str(repo),str(build),BRANCH],check=True)
    bundle = output/'Colosseum-1.5.0-x86_64-preview.flatpak'
    subprocess.run(['flatpak','build-bundle','--runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo',str(repo),str(bundle),APP_ID,BRANCH],check=True)
    (output/'SHA256SUMS-Flatpak').write_text(digest(bundle)+'  '+bundle.name+'\n')
    print(json.dumps({'bundle':bundle.name,'sha256':digest(bundle),'unchanged_elf_count':len(original_elf),
                      'unchanged_qml_files':len(qml_files)},indent=2))

if __name__ == '__main__':
    main()
