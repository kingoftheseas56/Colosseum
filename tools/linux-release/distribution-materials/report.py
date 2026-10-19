#!/usr/bin/env python3
"""Build review materials from observations; unresolved inputs remain explicit."""
import argparse
import json
from pathlib import Path
from inventory import digest, write_json


def generate(out):
    def read(name, default):
        path = out / name
        return json.loads(path.read_text()) if path.exists() else default
    payload = read('payload-inventory.json', [])
    build = read('retained/payload/opt/colosseum/BUILD-IDENTITY.json', {})
    proofs = read('ubuntu-file-proofs.json', [])
    proof_by_file = {p['payload']: p for p in proofs}
    sbom_dir = out / 'retained/payload/opt/colosseum/licenses/Qt-SBOM'
    sbom_paths = {}
    module_packages = []
    for file in sorted(sbom_dir.glob('*.spdx.json')):
        doc = json.loads(file.read_text())
        module_packages.extend({'sbom': file.name, **p} for p in doc.get('packages', []))
        for item in doc.get('files', []):
            path = item.get('fileName', '').removeprefix('./')
            sbom_paths.setdefault(path, []).append({'document': file.name, **item})
    write_json(out / 'qt-sbom-package-declarations.json', module_packages)
    rows = []
    overrides = {p['path'] for p in build.get('qt_plugin_selection', {}).get('overridden', [])}
    for item in payload:
        if not item.get('elf'):
            continue
        path = item['path']
        row = {'path': path, 'shipped_sha256': item['sha256'], 'status': 'unresolved'}
        if path in proof_by_file:
            row.update(family='ubuntu', status='verified-file-and-package', proof=proof_by_file[path])
        elif path == 'opt/colosseum/usr/bin/colosseum':
            row.update(family='colosseum', status='recorded-build-identity', source_commit=build['source_commit'])
        elif path.startswith('opt/colosseum/usr/plugins/') and path.split('/usr/plugins/', 1)[1] in overrides:
            row.update(family='rebuilt-QTiffPlugin', status='recorded-build-identity',
                       source_commit='77f35e5694885844c3eb3a737769b5ed290b8ccc')
        else:
            origin = build.get('bundled_dependency_origins', {}).get(Path(path).name, {})
            if 'mpvqt-install/' in origin.get('origin', ''):
                row.update(family='MpvQt', status='recorded-build-identity', origin=origin)
            else:
                sdk_path = path.removeprefix('opt/colosseum/usr/')
                if '/Qt/6.11.1/gcc_64/' in origin.get('origin', ''):
                    sdk_path = origin['origin'].split('/Qt/6.11.1/gcc_64/', 1)[1]
                declarations = sbom_paths.get(sdk_path, [])
                if declarations:
                    row.update(family='Qt SDK', status='retained-SBOM-path-attribution',
                               sdk_path=sdk_path, declarations=declarations, origin=origin)
                elif '/Qt/' in origin.get('origin', ''):
                    row.update(family='Qt SDK', origin=origin,
                               reason='SDK origin recorded; no exact retained SPDX file path declaration')
        rows.append(row)
    write_json(out / 'elf-provenance-inventory.json', rows)
    upstream = read('upstream-source-index.json', [])
    sources = read('ubuntu-source-index.json', [])
    unresolved = [{'path': r['path'], 'reason': r.get('reason', 'No verified file mapping')} for r in rows if r['status'] == 'unresolved']
    unresolved += read('ubuntu-unresolved.json', [])
    unresolved += read('upstream-unresolved.json', [])
    unresolved += [r for r in sources + upstream if r['status'] == 'unresolved']
    # Whole SDK proof and license-specific delivery review must not be inferred
    # from a library path/SBOM version match or from successful downloads.
    unresolved += [
        {'component': 'Qt SDK file verification', 'reason': 'SBOM path attribution is retained publisher metadata, not a replayed SDK build or byte verification of every original SDK file.'},
        {'component': 'Public source delivery and notices acceptance', 'reason': 'Artifacts are CI review artifacts with finite retention. Durable hosting, complete source/notice review and release authorization remain required.'},
        {'component': 'Relink/build materials review', 'reason': 'Exact application/SDK dependency build recipes and dynamic-link configuration retained; sufficient license-specific relinking/build delivery is not certified by this collector.'}]
    write_json(out / 'unresolved.json', unresolved)
    summary = {'status': 'OPEN-RECEIVING-REVIEW-REQUIRED', 'package': read('package-identity.json', {}),
               'source_commit': build.get('source_commit'), 'elf_files': len(rows),
               'elf_status_counts': {key: sum(r['status'] == key for r in rows) for key in sorted({r['status'] for r in rows})},
               'ubuntu': read('ubuntu-summary.json', {}), 'upstream_components': len(upstream),
               'unresolved_records': len(unresolved)}
    write_json(out / 'SUMMARY.json', summary)
    notices = [
        'Colosseum 1.1.9 Linux distribution materials - receiving review draft',
        'Exact package SHA256: ' + summary['package'].get('sha256', 'UNKNOWN'),
        'This index records technical provenance and preserves upstream notices.',
        'It does not make a source offer or certify redistribution compliance.',
        '', 'Original unchanged notices, common license texts, Qt SPDX/CycloneDX SBOMs:',
        '  retained/payload/opt/colosseum/licenses/',
        'These retained host notices include build-host packages not bundled in the application.',
        'Actual shipped ELF inventory: elf-provenance-inventory.json.',
        'Verified Ubuntu package/source index: ubuntu-file-proofs.json and ubuntu-source-index.json.',
        'Exact Ubuntu binary copyright texts: binary-verification/<binary-package>/copyrights/.',
        'Qt and other acquired source inputs: upstream-source-index.json; unchanged source notices under upstream/*/notices/.',
        'Qt license fields are publisher declarations retained verbatim in qt-sbom-package-declarations.json.',
        'NOASSERTION and unknown entries remain unknown; no default license classification is applied.',
        'Build/configuration recipes and exact accepted workflow: recipes/.',
        'Unresolved technical/delivery items: unresolved.json.',
        '', 'Verified Ubuntu file ownership (binary package/version -> source package/version):']
    for row in sorted({(r['binary_package'], r['binary_version'], r['source_package'], r['source_version']) for r in proofs}):
        notices.append('  ' + row[0] + ' ' + row[1] + ' -> ' + row[2] + ' ' + row[3])
    (out / 'DISTRIBUTION-NOTICES-REVIEW.txt').write_text('\n'.join(notices) + '\n', encoding='utf-8')
    # Hash the deliverable set; this index intentionally excludes itself.
    manifest = [{'path': p.relative_to(out).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p)}
                for p in sorted(out.rglob('*')) if p.is_file() and '.git' not in p.parts
                and 'git' not in p.relative_to(out).parts and p.name != 'SHA256-INDEX.json']
    write_json(out / 'SHA256-INDEX.json', manifest)
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    generate(parser.parse_args().output.resolve())
