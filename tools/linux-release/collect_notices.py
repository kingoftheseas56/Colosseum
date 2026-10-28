#!/usr/bin/env python3
"""Collect available unchanged notices; does not certify redistribution compliance."""
import argparse, json, shutil
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--output',required=True,type=Path);ap.add_argument('--dep-root',required=True,type=Path);ap.add_argument('--mpvqt-source',required=True,type=Path);a=ap.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
manifest=[]
for label,root in [('cloud-host',Path('/usr/share/doc')),('prepared-dependencies',a.dep_root/'usr/share/doc')]:
    for p in sorted(root.rglob('copyright')):
        if not p.is_file():continue
        rel=p.relative_to(root);dst=a.output/label/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
        manifest.append({'notice':str(dst.relative_to(a.output)),'source':str(p)})
shutil.copytree(a.mpvqt_source/'LICENSES',a.output/'MpvQt-LICENSES',dirs_exist_ok=True)
(a.output/'NOTICE-CANDIDATE.txt').write_text('This private Linux review candidate dynamically links third-party libraries with their own licenses. Colosseum original source remains MIT; this does not describe the entire binary distribution.\nThe bundled Debian libmpv build is GPL-3+ (its Debian copyright attributes this to linking libsmbclient). The Debian FFmpeg binaries include GPL components. Qt and MpvQt include LGPL and other third-party terms. Exact unchanged available notices and Qt SDK SBOM are retained.\nThese materials are not a certification of stable public redistribution compliance. The host notice collection is deliberately broader than the runtime closure because this cloud image does not expose a usable dpkg file-ownership database. BUILD-IDENTITY.json and package-check.json identify actual bundled ELF files.\nExact Colosseum candidate source and build scripts are identified in the delivery record. Qt source: official Qt 6.11.1 release/source repositories; MpvQt source: upstream 1.2.0; Debian dependency versions/source package provenance: dependency manifest. Before public stable distribution, verify corresponding-source availability/delivery and all license-specific requirements for the exact shipped dependency combination, including Chromium third-party notices.\n')
(a.output/'notice-collection.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(len(manifest),'unchanged copyright files collected')
