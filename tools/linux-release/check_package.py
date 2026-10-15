#!/usr/bin/env python3
"""Verify relocated ELF closure and source/QML identity before archive creation."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
from assemble import elf, sha, SYSTEM

def main():
    ap=argparse.ArgumentParser();ap.add_argument('stage',type=Path);ap.add_argument('--report',type=Path,required=True);a=ap.parse_args();root=a.stage.resolve()
    failures=[];files=[];required={'GLIBC':set(),'GLIBCXX':set(),'CXXABI':set()};external={}
    sentinels=['AppRun','usr/bin/colosseum','usr/bin/qml-build.manifest','usr/bin/qt.conf','qml/Main.qml','usr/lib/libQt6Core.so.6','usr/lib/libQt6WebEngineCore.so.6','usr/lib/libmpv.so.2','usr/libexec/QtWebEngineProcess','usr/plugins/platforms/libqxcb.so','usr/plugins/sqldrivers/libqsqlite.so','usr/resources/icudtl.dat','usr/resources/qtwebengine_resources.pak','licenses/LICENSE','licenses/THIRD_PARTY_NOTICES.md','BUILD-IDENTITY.json']
    for f in sentinels:
        if not (root/f).is_file():failures.append('Missing sentinel '+f)
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'usr/lib');env.pop('LD_PRELOAD',None)
    for p in sorted(root.rglob('*')):
        if p.is_symlink() and not p.resolve().is_relative_to(root):failures.append('External symlink '+str(p))
        if not p.is_file() or not elf(p):continue
        rel=str(p.relative_to(root));rec={'path':rel,'sha256':sha(p)}
        info=subprocess.run(['readelf','-d',str(p)],text=True,capture_output=True).stdout
        rpaths=re.findall(r'\((?:RPATH|RUNPATH)\).*?\[(.*?)\]',info)
        rec['rpaths']=rpaths
        for rp in rpaths:
            for element in rp.split(':'):
                if not element.startswith('$ORIGIN'):failures.append(f'Non-relative RPATH {rel}: {element}')
        deps=subprocess.run(['ldd',str(p)],env=env,text=True,capture_output=True)
        rec['ldd']=deps.stdout.strip()
        if 'not found' in deps.stdout:failures.append('Missing shared library '+rel)
        for line in deps.stdout.splitlines():
            m=re.match(r'\s*(\S+) => (/\S+) ',line)
            if not m:continue
            name,location=m.groups()
            if not Path(location).resolve().is_relative_to(root):
                external[name]=location
                if not SYSTEM.match(name):failures.append(f'Unbundled non-baseline library {rel}: {name} -> {location}')
        syms=subprocess.run(['readelf','--dyn-syms','--wide',str(p)],text=True,capture_output=True).stdout
        for line in syms.splitlines():
            if ' UND ' not in line:continue
            for family,version in re.findall(r'@+(GLIBCXX|GLIBC|CXXABI)_([0-9.]+)',line):required[family].add(version)
        files.append(rec)
    qml=root/'qml'; material=b''
    for p in sorted((x for x in qml.rglob('*') if x.is_file()),key=lambda p:p.relative_to(qml).as_posix()):material+=p.relative_to(qml).as_posix().encode()+b'\n'+sha(p).encode()+b'\n'
    fingerprint=hashlib.sha256(material).hexdigest();manifest=(root/'usr/bin/qml-build.manifest').read_text()
    if f'qmlTreeSha256={fingerprint}' not in manifest:failures.append('QML build manifest differs from packaged QML')
    meta=json.loads((root/'BUILD-IDENTITY.json').read_text())
    if sha(root/'usr/bin/colosseum')!=meta['binary_sha256']:failures.append('Binary identity mismatch')
    maximum={k:max(v,key=lambda x:tuple(map(int,x.split('.')))) if v else None for k,v in required.items()}
    report={'status':'FAIL' if failures else 'PASS','failures':failures,'source_commit':meta['source_commit'],'executable_sha256':meta['binary_sha256'],'qml_fingerprint':fingerprint,'elf_count':len(files),'required_symbol_versions_max':maximum,'external_system_libraries':external,'files':files}
    a.report.write_text(json.dumps(report,indent=2)+'\n')
    compact={k:v for k,v in report.items() if k!='files'}
    (root/'package-check.json').write_text(json.dumps(compact,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2));return bool(failures)
if __name__=='__main__':raise SystemExit(main())
