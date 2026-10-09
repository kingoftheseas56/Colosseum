#!/usr/bin/env python3
"""Assemble an explicitly pinned Colosseum build. No downloads or uploads."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

SYSTEM = re.compile(r'^(ld-linux[^/]*|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so\..*|lib(GL|EGL|GLX|GLdispatch|OpenGL|drm[^/]*|gbm|vulkan|va(?!-wayland)[^/]*)\.so\..*)$')
def elf(p):
    try:
        with p.open('rb') as f:return f.read(4)==b'\x7fELF'
    except OSError:return False
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def run(cmd, **kw):return subprocess.run(cmd,text=True,capture_output=True,check=True,**kw).stdout

def main():
    ap=argparse.ArgumentParser()
    for n in ('source','build','qt','stage','commit','patchelf'):ap.add_argument('--'+n,required=True)
    ap.add_argument('--dep-lib',action='append',default=[])
    ap.add_argument('--dependency-notices',action='append',default=[])
    ap.add_argument('--exclude-qml-module',action='append',default=[],help='Unused QML module directory relative to qml/, recorded in identity')
    ap.add_argument('--exclude-plugin',action='append',default=[],help='Exact unused Qt plugin path relative to plugins/, recorded in identity')
    ap.add_argument('--plugin-override',action='append',default=[],help='Exact plugins-relative path=rebuilt matching-Qt plugin file')
    ap.add_argument('--runtime-lib',action='append',default=[],help='Explicit coherent runtime library family selected ahead of SDK copies')
    ap.add_argument('--binary',action='append',default=[],help='Additional runtime executable, e.g. ffmpeg/ffprobe')
    a=ap.parse_args(); src,build,qt,out=map(lambda n:Path(getattr(a,n)).resolve(),('source','build','qt','stage'))
    if not re.fullmatch('[0-9a-f]{40}',a.commit):raise SystemExit('Full source commit required')
    if out.exists():raise SystemExit('Stage must not exist; choose a fresh directory')
    if not (build/'colosseum').is_file():raise SystemExit('Compiled application missing')
    cache=(build/'CMakeCache.txt').read_text()
    if not re.search(r'^CMAKE_BUILD_TYPE:STRING=Release$',cache,re.M):raise SystemExit('Not Release')
    if re.search(r'^COLOSSEUM_UPDATE_TESTING:BOOL=ON$',cache,re.M):raise SystemExit('Testing updater trust forbidden')
    out.mkdir(parents=True); bind=out/'usr/bin'; lib=out/'usr/lib'; bind.mkdir(parents=True);lib.mkdir()
    shutil.copy2(build/'colosseum',bind/'colosseum');shutil.copy2(build/'qml-build.manifest',bind/'qml-build.manifest')
    for name in ('qml','assets','resources','data'):shutil.copytree(src/name,out/name,symlinks=False)
    for name in ('qml','plugins','resources','translations'):
        if (qt/name).exists():shutil.copytree(qt/name,out/'usr'/name,symlinks=False,ignore=shutil.ignore_patterns('*.o','*.obj','objects-*','*.a','*.debug','*.prl','*.la','*.cmake','*.pdb'))
    (out/'usr/libexec').mkdir();shutil.copy2(qt/'libexec/QtWebEngineProcess',out/'usr/libexec/QtWebEngineProcess')
    plugin_changes={'excluded':[],'overridden':[],'excluded_qml_modules':[]}
    for relative in a.exclude_qml_module:
        path=Path(relative)
        if path.is_absolute() or '..' in path.parts:raise SystemExit('Unsafe QML module path: '+relative)
        target=out/'usr/qml'/path
        if not target.is_dir():raise SystemExit('Excluded QML module does not exist: '+relative)
        shutil.rmtree(target);plugin_changes['excluded_qml_modules'].append(relative)
    for relative in a.exclude_plugin:
        path=Path(relative)
        if path.is_absolute() or '..' in path.parts:raise SystemExit('Unsafe plugin path: '+relative)
        target=out/'usr/plugins'/path
        if not target.is_file():raise SystemExit('Excluded plugin does not exist: '+relative)
        target.unlink();plugin_changes['excluded'].append(relative)
    for spec in a.plugin_override:
        relative,origin=spec.split('=',1);path=Path(relative)
        if path.is_absolute() or '..' in path.parts:raise SystemExit('Unsafe plugin override path: '+relative)
        target=out/'usr/plugins'/path
        if not target.is_file():raise SystemExit('Override target does not exist: '+relative)
        shutil.copy2(origin,target);plugin_changes['overridden'].append({'path':relative,'origin':str(Path(origin).resolve()),'sha256_before_relocation':sha(target)})
    explicit={}
    for name in a.runtime_lib:
        p=Path(name).resolve(); soname=re.search(r'\(SONAME\).*?\[(.*?)\]',run(['readelf','-d',str(p)]))
        if not soname:raise SystemExit('Runtime library has no SONAME: '+str(p))
        target=lib/soname.group(1);shutil.copy2(p,target);explicit[target.name]={'origin':str(p),'sha256_before_relocation':sha(target)}
    for p in a.binary:shutil.copy2(p,bind/Path(p).name)
    (bind/'qt.conf').write_text('[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n')
    (out/'usr/libexec/qt.conf').write_text('[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nData=.\nTranslations=translations\n')
    env=os.environ.copy();env['LD_LIBRARY_PATH']=':'.join([str(lib),str(qt/'lib')]+a.dep_lib)
    origins=explicit; external={};seen=set(); todo=[p for p in out.rglob('*') if p.is_file() and elf(p)]
    while todo:
        p=todo.pop()
        if p in seen:continue
        seen.add(p)
        proc=subprocess.run(['ldd',str(p)],env=env,text=True,capture_output=True)
        if 'not found' in proc.stdout:raise SystemExit(f'Unresolved dependency {p}:\n{proc.stdout}')
        for line in proc.stdout.splitlines():
            m=re.match(r'\s*(\S+) => (/\S+) ',line)
            if not m:continue
            name,where=m.groups(); origin=Path(where)
            if SYSTEM.match(name):external[name]=where;continue
            target=lib/name
            if not target.exists():shutil.copy2(origin,target);origins[name]={'origin':str(origin.resolve()),'sha256_before_relocation':sha(target)};todo.append(target)
    for p in sorted(seen):
        relative=os.path.relpath(lib,p.parent)
        run([a.patchelf,'--set-rpath','$ORIGIN/'+relative,str(p)])
    notices=out/'licenses';notices.mkdir()
    for name in ('LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(src/name,notices/name)
    for i,d in enumerate(a.dependency_notices):
        d=Path(d)
        if d.is_dir():shutil.copytree(d,notices/f'dependency-{i}-{d.name}',symlinks=False)
        else:shutil.copy2(d,notices/f'dependency-{i}-{d.name}')
    if (qt/'sbom').is_dir():shutil.copytree(qt/'sbom',notices/'Qt-SBOM')
    if Path('/usr/share/common-licenses').is_dir():shutil.copytree('/usr/share/common-licenses',notices/'Common-License-Texts')
    for d in ('licenses','Licenses'):
        if (qt/d).is_dir():shutil.copytree(qt/d,notices/'Qt',dirs_exist_ok=True)
    launcher=Path(__file__).with_name('AppRun');shutil.copy2(launcher,out/'AppRun');(out/'AppRun').chmod(0o755)
    metadata={'source_commit':a.commit,'original_source_commit':'15fd79af6331f2b4726ab2350a69a0c896cdbecd','version':'1.1.8','architecture':'x86_64','qt_expected':'6.11.1','build_type':'Release','unrelocated_build_binary_sha256':sha(build/'colosseum'),'binary_sha256':sha(bind/'colosseum'),'external_system_dependencies':external,'bundled_dependency_origins':origins,'qt_plugin_selection':plugin_changes}
    (out/'BUILD-IDENTITY.json').write_text(json.dumps(metadata,indent=2)+'\n')
    (out/'README-LINUX.txt').write_text('Colosseum 1.1.8 Linux candidate\nRun ./AppRun from this directory. No installation is required.\nThis is a review candidate, not a stable release.\nSystem requirements: Linux x86_64 with glibc as recorded by package-check.json; an X11 or Wayland desktop, fontconfig/fonts, desktop audio server, and compatible graphics drivers.\nGPU/GL/EGL/DRM/Vulkan drivers and glibc remain system supplied.\nAppRun does not disable WebEngine sandboxing. Running as root is not supported.\nProfiles use the normal XDG user directories; set XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME to disposable paths for tests.\nNo private accounts, catalog databases, Widevine CDM or provider credentials are bundled.\nRefer to the accompanying delivery record for tested capabilities and known limitations.\n')
    print(json.dumps({'stage':str(out),'elf_files':len(seen),'bundled_dependencies':len(origins),'binary_sha256':metadata['binary_sha256']},indent=2))
if __name__=='__main__':main()
