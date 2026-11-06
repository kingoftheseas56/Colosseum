#!/usr/bin/env python3
"""Adverse-state tests use a tiny native ELF fixture, not Colosseum qualification."""
import json, os, shutil, subprocess, tempfile, unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
PATCH=os.environ.get('PATCHELF') or shutil.which('patchelf')
if os.name!='nt' and not PATCH:raise SystemExit('patchelf is required for Linux packaging tests')
BASH=shutil.which('bash')
if os.name=='nt' and Path('C:/Program Files/Git/bin/bash.exe').is_file():BASH='C:/Program Files/Git/bin/bash.exe'

@unittest.skipUnless(PATCH and os.name!='nt','Linux ELF tools and patchelf required')
class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
        s=self.p/'source';b=self.p/'build';q=self.p/'qt';d=self.p/'deps'
        for p in (s,b,q,d):p.mkdir()
        for name in ('qml','assets','resources','data'):(s/name).mkdir()
        (s/'native').mkdir();(s/'native/CMakeLists.txt').write_text('project(colosseum VERSION 1.1.9 LANGUAGES C CXX)\n')
        quickjs_license=s/'native/nuvio/vendor/quickjs/LICENSE';quickjs_license.parent.mkdir(parents=True);quickjs_license.write_text('QuickJS license fixture\n')
        js_licenses=s/'native/nuvio/vendor/js/licenses';js_licenses.mkdir(parents=True);(js_licenses/'NOTICE.txt').write_text('JavaScript runtime notice fixture\n')
        (s/'scripts').mkdir();shutil.copy2(HERE.parents[1]/'scripts/stage_api_content.py',s/'scripts/stage_api_content.py')
        starter=s/'assets/content-starter/bundle.json';starter.parent.mkdir()
        starter.write_text(json.dumps({'manifest':{'documents':{str(i):{} for i in range(10)},'assets':{}}}))
        icon=s/'assets/icons/colosseum.svg';icon.parent.mkdir();icon.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"/>')
        (s/'qml/Main.qml').write_text('import QtQuick\nItem {}\n')
        for name in ('LICENSE','THIRD_PARTY_NOTICES.md'):(s/name).write_text('Test fixture only\n')
        (b/'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\nCOLOSSEUM_NO_ACCOUNT_EDITION:BOOL=OFF\nCOLOSSEUM_CONTENT_API_URL:STRING=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev\n')
        import hashlib
        content=(s/'qml/Main.qml').read_bytes();h=hashlib.sha256(b'Main.qml\n'+hashlib.sha256(content).hexdigest().encode()+b'\n').hexdigest()
        (b/'qml-build.manifest').write_text('schema=1\nqmlTreeSha256='+h+'\n')
        (self.p/'dep.c').write_text('int answer(void) {return 42;}')
        (self.p/'app.c').write_text('extern int answer(void); int main(void){return answer()!=42;}')
        subprocess.run(['cc','-shared','-fPIC',str(self.p/'dep.c'),'-Wl,-soname,libfixture.so.1','-o',str(d/'libfixture.so.1')],check=True)
        subprocess.run(['cc',str(self.p/'app.c'),'-L'+str(d),'-l:libfixture.so.1','-Wl,-rpath,'+str(d),'-o',str(b/'colosseum')],check=True)
        subprocess.run(['cc',str(self.p/'app.c'),'-L'+str(d),'-l:libfixture.so.1','-Wl,-rpath,'+str(d),'-o',str(b/'colosseum-nuvio-host')],check=True)
        for name in ('lib','qml','plugins','libexec','resources','translations'):(q/name).mkdir()
        shutil.copy2('/bin/true',q/'libexec/QtWebEngineProcess')
        self.stage=self.p/'candidate'
        subprocess.run(['python3',str(HERE/'assemble.py'),'--source',str(s),'--build',str(b),'--qt',str(q),'--stage',str(self.stage),'--commit','a'*40,'--patchelf',PATCH,'--dep-lib',str(d)],check=True,capture_output=True)
        # Sentinel placeholders allow focused checker tests; not runtime qualification.
        for name in ('usr/lib/libQt6Core.so.6','usr/lib/libQt6WebEngineCore.so.6','usr/lib/libmpv.so.2','usr/plugins/platforms/libqxcb.so','usr/plugins/sqldrivers/libqsqlite.so','usr/resources/icudtl.dat','usr/resources/qtwebengine_resources.pak'):
            p=self.stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture')
    def tearDown(self):self.tmp.cleanup()
    def check(self):
        out=self.p/'report.json';r=subprocess.run(['python3',str(HERE/'check_package.py'),str(self.stage),'--report',str(out)],capture_output=True,text=True);return r.returncode,json.loads(out.read_text())
    def test_relocated_native_fixture(self):
        shutil.rmtree(self.p/'deps')
        self.assertEqual(subprocess.run([str(self.stage/'usr/bin/colosseum')]).returncode,0)
        self.assertEqual(subprocess.run([str(self.stage/'usr/bin/colosseum-nuvio-host')]).returncode,0)
        self.assertEqual((self.stage/'licenses/Nuvio/QuickJS/LICENSE').read_text(),'QuickJS license fixture\n')
        self.assertEqual((self.stage/'licenses/Nuvio/JS/NOTICE.txt').read_text(),'JavaScript runtime notice fixture\n')
        self.assertEqual(self.check()[0],0)
    def test_missing_nuvio_helper_rejected(self):
        (self.stage/'usr/bin/colosseum-nuvio-host').unlink()
        self.assertIn('Missing sentinel usr/bin/colosseum-nuvio-host',self.check()[1]['failures'])
    def test_missing_dependency_rejected(self):
        (self.stage/'usr/lib/libfixture.so.1').unlink();self.assertNotEqual(self.check()[0],0)
    def test_qml_drift_rejected(self):
        (self.stage/'qml/Main.qml').write_text('changed');self.assertIn('QML build manifest differs from packaged QML',self.check()[1]['failures'])
    def test_missing_resource_rejected(self):
        (self.stage/'usr/resources/icudtl.dat').unlink();self.assertNotEqual(self.check()[0],0)
    def test_absolute_rpath_rejected(self):
        subprocess.run([PATCH,'--set-rpath','/tmp/nonportable',str(self.stage/'usr/libexec/QtWebEngineProcess')],check=True);self.assertNotEqual(self.check()[0],0)
    def reassemble(self, extra):
        return subprocess.run(['python3',str(HERE/'assemble.py'),'--source',str(self.p/'source'),'--build',str(self.p/'build'),'--qt',str(self.p/'qt'),'--stage',str(self.p/'second'),'--commit','a'*40,'--patchelf',PATCH,'--dep-lib',str(self.p/'deps')]+extra,capture_output=True,text=True)
    def positioning_fixture(self):
        qt=self.p/'qt';plugin=qt/'plugins/position/libqtposition_nmea.so'
        plugin.parent.mkdir()
        dependency=qt/'lib/libQt6SerialPort.so.6'
        subprocess.run(['cc','-shared','-fPIC',str(self.p/'dep.c'),'-Wl,-soname,libQt6SerialPort.so.6','-o',str(dependency)],check=True)
        subprocess.run(['cc','-shared','-fPIC',str(self.p/'app.c'),'-L'+str(qt/'lib'),'-l:libQt6SerialPort.so.6','-o',str(plugin)],check=True)
        return dependency
    def test_positioning_dependency_bundled_from_sdk(self):
        dependency=self.positioning_fixture()
        r=self.reassemble([]);self.assertEqual(r.returncode,0,r.stderr)
        stage=self.p/'second';bundled=stage/'usr/lib/libQt6SerialPort.so.6'
        self.assertTrue(bundled.is_file())
        identity=json.loads((stage/'BUILD-IDENTITY.json').read_text())
        self.assertEqual(identity['bundled_dependency_origins'][bundled.name]['origin'],str(dependency))
        self.assertEqual(identity['qt_plugin_selection']['excluded'],[])
        shutil.rmtree(self.p/'qt')
        loaded=subprocess.run(['ldd',str(stage/'usr/plugins/position/libqtposition_nmea.so')],capture_output=True,text=True)
        self.assertEqual(loaded.returncode,0,loaded.stderr)
        resolved=next(line.split('=>',1)[1].split()[0] for line in loaded.stdout.splitlines() if 'libQt6SerialPort.so.6 =>' in line)
        self.assertEqual(Path(resolved).resolve(),bundled);self.assertNotIn('not found',loaded.stdout)
    def test_positioning_missing_dependency_still_rejected(self):
        self.positioning_fixture().unlink()
        r=self.reassemble([]);self.assertNotEqual(r.returncode,0)
        self.assertIn('Unresolved dependency',r.stderr)
        self.assertIn('libQt6SerialPort.so.6 => not found',r.stderr)
    def test_unused_plugin_exclusion_recorded(self):
        shutil.copy2('/bin/true',self.p/'qt/plugins/unused.so')
        r=self.reassemble(['--exclude-plugin','unused.so']);self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((self.p/'second/usr/plugins/unused.so').exists())
        self.assertEqual(json.loads((self.p/'second/BUILD-IDENTITY.json').read_text())['qt_plugin_selection']['excluded'],['unused.so'])
    def test_plugin_override_recorded(self):
        shutil.copy2('/bin/false',self.p/'qt/plugins/rebuilt.so')
        r=self.reassemble(['--plugin-override','rebuilt.so=/bin/true']);self.assertEqual(r.returncode,0,r.stderr)
        m=json.loads((self.p/'second/BUILD-IDENTITY.json').read_text());self.assertEqual(m['qt_plugin_selection']['overridden'][0]['path'],'rebuilt.so')
        self.assertEqual(subprocess.run([str(self.p/'second/usr/plugins/rebuilt.so')]).returncode,0)
    def test_plugin_escape_rejected(self):
        r=self.reassemble(['--exclude-plugin','../escape']);self.assertNotEqual(r.returncode,0);self.assertIn('Unsafe plugin path',r.stderr)
    def test_unused_qml_module_exclusion_recorded(self):
        module=self.p/'qt/qml/Unused';module.mkdir();(module/'qmldir').write_text('module Unused')
        r=self.reassemble(['--exclude-qml-module','Unused']);self.assertEqual(r.returncode,0,r.stderr)
        self.assertFalse((self.p/'second/usr/qml/Unused').exists())
        self.assertEqual(json.loads((self.p/'second/BUILD-IDENTITY.json').read_text())['qt_plugin_selection']['excluded_qml_modules'],['Unused'])
    def test_sdk_objects_are_not_packaged(self):
        module=self.p/'qt/qml/objects-RelWithDebInfo';module.mkdir();shutil.copy2('/bin/true',module/'artifact.o')
        r=self.reassemble([]);self.assertEqual(r.returncode,0,r.stderr);self.assertFalse((self.p/'second/usr/qml/objects-RelWithDebInfo').exists())
    def test_unneeded_qt_library_is_not_packaged(self):
        shutil.copy2('/bin/true',self.p/'qt/lib/libQt6Unused.so.6')
        r=self.reassemble([]);self.assertEqual(r.returncode,0,r.stderr);self.assertFalse((self.p/'second/usr/lib/libQt6Unused.so.6').exists())
    def pixbuf_fixture(self):
        loaders=self.p/'pixbuf loaders';loaders.mkdir()
        shutil.copy2(self.p/'deps/libfixture.so.1',loaders/'libpixbufloader-svg.so')
        source=self.p/'query.c';source.write_text('#include <stdio.h>\nint main(int argc,char **argv){for(int i=1;i<argc;i++)printf("\\\"%s\\\"\\n",argv[i]);puts("\\\"svg\\\" 5");return 0;}\n')
        tool=self.p/'query';subprocess.run(['cc',str(source),'-o',str(tool)],check=True)
        return loaders,tool
    def test_pixbuf_pair_and_svg_required(self):
        loaders,tool=self.pixbuf_fixture()
        for args in (['--gdk-pixbuf-loaders-dir',str(loaders)],['--gdk-pixbuf-query-loaders',str(tool)]):
            r=self.reassemble(args);self.assertNotEqual(r.returncode,0);self.assertIn('supplied together',r.stderr)
        (loaders/'libpixbufloader-svg.so').unlink()
        r=self.reassemble(['--gdk-pixbuf-loaders-dir',str(loaders),'--gdk-pixbuf-query-loaders',str(tool)])
        self.assertNotEqual(r.returncode,0);self.assertIn('SVG loader missing',r.stderr)
    def test_bundled_pixbuf_requires_explicit_matching_modules(self):
        library=self.p/'deps/libgdk_pixbuf-2.0.so.0'
        subprocess.run(['cc','-shared','-fPIC',str(self.p/'dep.c'),'-Wl,-soname,libgdk_pixbuf-2.0.so.0','-o',str(library)],check=True)
        r=self.reassemble(['--runtime-lib',str(library)])
        self.assertNotEqual(r.returncode,0);self.assertIn('requires matching explicit loaders',r.stderr)
    def test_pixbuf_staged_relocated_and_checked(self):
        loaders,tool=self.pixbuf_fixture()
        r=self.reassemble(['--gdk-pixbuf-loaders-dir',str(loaders),'--gdk-pixbuf-query-loaders',str(tool)])
        self.assertEqual(r.returncode,0,r.stderr)
        stage=self.p/'second';m=json.loads((stage/'BUILD-IDENTITY.json').read_text())['gdk_pixbuf']
        self.assertEqual(m['files'][0]['origin'],str(tool))
        self.assertIn('usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so',m['cache_validation']['modules'])
        # Use the original fixture's sentinels/QML identity with the newly bundled closure.
        shutil.copytree(stage/'usr/lib/gdk-pixbuf-loaders',self.stage/'usr/lib/gdk-pixbuf-loaders')
        shutil.copy2(stage/'usr/bin/gdk-pixbuf-query-loaders',self.stage/'usr/bin/gdk-pixbuf-query-loaders')
        identity=json.loads((self.stage/'BUILD-IDENTITY.json').read_text());identity['gdk_pixbuf']=m
        (self.stage/'BUILD-IDENTITY.json').write_text(json.dumps(identity))
        self.assertEqual(self.check()[0],0)
        (self.stage/'usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so').unlink()
        self.assertNotEqual(self.check()[0],0)
    def test_pixbuf_unresolved_module_symbols_rejected(self):
        loaders,tool=self.pixbuf_fixture()
        source=self.p/'unresolved.c';source.write_text('extern int missing_svg_api(void); int fixture(void){return missing_svg_api();}')
        subprocess.run(['cc','-shared','-fPIC',str(source),'-o',str(loaders/'libpixbufloader-svg.so')],check=True)
        r=self.reassemble(['--gdk-pixbuf-loaders-dir',str(loaders),'--gdk-pixbuf-query-loaders',str(tool)])
        self.assertEqual(r.returncode,0,r.stderr)
        shutil.copytree(self.p/'second/usr/lib/gdk-pixbuf-loaders',self.stage/'usr/lib/gdk-pixbuf-loaders')
        code,report=self.check();self.assertNotEqual(code,0)
        self.assertIn('Unresolved GdkPixbuf module symbols usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so',report['failures'])
    def test_archive_missing_stage_rejected(self):
        out=self.p/'bad.tar.gz'
        r=subprocess.run([str(HERE/'archive.sh'),str(self.p/'absent'),str(out)],capture_output=True)
        self.assertNotEqual(r.returncode,0);self.assertFalse(out.exists());self.assertFalse(Path(str(out)+'.sha256').exists())
    def test_archive_tar_failure_rejected(self):
        fake=self.p/'fake-tools';fake.mkdir();tar=fake/'tar';tar.write_text('#!/bin/sh\nexit 42\n');tar.chmod(0o755)
        out=self.p/'failed.tar.gz';env=os.environ.copy();env['PATH']=str(fake)+':'+env['PATH']
        r=subprocess.run([str(HERE/'archive.sh'),str(self.stage),str(out)],capture_output=True,env=env)
        self.assertNotEqual(r.returncode,0);self.assertFalse(out.exists());self.assertFalse(Path(str(out)+'.sha256').exists())
    def test_archive_valid_candidate(self):
        out=self.p/'good.tar.gz';r=subprocess.run([str(HERE/'archive.sh'),str(self.stage),str(out)],capture_output=True)
        self.assertEqual(r.returncode,0,r.stderr.decode());self.assertTrue(out.exists());self.assertTrue(Path(str(out)+'.sha256').exists())
    def test_outside_symlink_rejected(self):
        (self.stage/'external').symlink_to('/etc/hosts');self.assertNotEqual(self.check()[0],0)
    def test_deb_contains_the_qualified_payload_and_launcher(self):
        out=self.p/'colosseum.deb'
        proc=subprocess.run(['python3',str(HERE/'package_deb.py'),'--stage',str(self.stage),'--output',str(out)],text=True,capture_output=True)
        self.assertEqual(proc.returncode,0,proc.stdout+proc.stderr)
        info=subprocess.check_output(['dpkg-deb','--field',str(out),'Package','Version','Architecture'],text=True)
        self.assertIn('Package: colosseum',info);self.assertIn('Version: 1.1.9',info);self.assertIn('Architecture: amd64',info)
        # Both control and payload must use XZ; a command-line assertion alone
        # would miss dpkg environment/default changes or a later repack.
        members=subprocess.check_output(['ar','t',str(out)],text=True).splitlines()
        self.assertEqual(members,['debian-binary','control.tar.xz','data.tar.xz'])
        depends=subprocess.check_output(['dpkg-deb','--field',str(out),'Depends'],text=True)
        self.assertIn('libopengl0',[entry.strip() for entry in depends.split(',')])
        self.assertIn('ca-certificates',[entry.strip() for entry in depends.split(',')])
        self.assertIn('libgtk-3-common',[entry.strip() for entry in depends.split(',')])
        extracted=self.p/'deb-extracted';subprocess.run(['dpkg-deb','--extract',str(out),str(extracted)],check=True)
        self.assertEqual((extracted/'opt/colosseum/usr/bin/colosseum').read_bytes(),(self.stage/'usr/bin/colosseum').read_bytes())
        self.assertEqual((extracted/'opt/colosseum/usr/bin/colosseum-nuvio-host').read_bytes(),(self.stage/'usr/bin/colosseum-nuvio-host').read_bytes())
        self.assertTrue((extracted/'opt/colosseum/licenses/Nuvio/QuickJS/LICENSE').is_file())
        self.assertTrue((extracted/'opt/colosseum/licenses/Nuvio/JS/NOTICE.txt').is_file())
        self.assertTrue((extracted/'usr/share/applications/colosseum.desktop').is_file())
        self.assertTrue(os.access(extracted/'usr/bin/colosseum',os.X_OK))
        self.assertIn('/opt/colosseum/AppRun',(extracted/'usr/bin/colosseum').read_text())
    def test_deb_rejects_qml_drift_before_creating_output(self):
        (self.stage/'qml/Main.qml').write_text('changed')
        out=self.p/'bad.deb'
        proc=subprocess.run(['python3',str(HERE/'package_deb.py'),'--stage',str(self.stage),'--output',str(out)],text=True,capture_output=True)
        self.assertNotEqual(proc.returncode,0);self.assertFalse(out.exists())
@unittest.skipUnless(BASH,'Bash required for launcher fixtures')
class LauncherTests(unittest.TestCase):
    def shell_path(self,path):
        if os.name=='nt':return subprocess.check_output([BASH,'-c','cygpath -u "$1"','--',str(path)],text=True).strip()
        return str(path)
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='colosseum-launcher-');self.p=Path(self.tmp.name)
        self.stage=self.p/'candidate with spaces'
        for relative in ('usr/bin','usr/lib/gdk-pixbuf-loaders'):(self.stage/relative).mkdir(parents=True)
        shutil.copy2(HERE/'AppRun',self.stage/'AppRun')
        (self.stage/'usr/lib/libgdk_pixbuf-2.0.so.0').write_text('launcher fixture')
        (self.stage/'usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so').write_text('loader fixture')
        self.query=self.stage/'usr/bin/gdk-pixbuf-query-loaders'
        self.query.write_text('#!/bin/sh\n[ -z "${GDK_PIXBUF_MODULE_FILE+x}" ] && [ -z "${GDK_PIXBUF_MODULEDIR+x}" ] || exit 31\n[ "$LD_LIBRARY_PATH" = "$(dirname "$0")/../lib" ] || [ -d "$LD_LIBRARY_PATH/gdk-pixbuf-loaders" ] || exit 32\nfor module do printf \'"%s"\\n\' "$module"; done\nprintf \'"svg" 5\\n\'\n')
        self.query.chmod(0o755)
        app=self.stage/'usr/bin/colosseum'
        app.write_text('#!/bin/sh\nprintf \'%s\\n\' "$GDK_PIXBUF_MODULE_FILE" "$GDK_PIXBUF_MODULEDIR" "$@" > "$REPORT"\ncat "$GDK_PIXBUF_MODULE_FILE" >> "$REPORT"\n')
        app.chmod(0o755)
        self.env=dict(os.environ,XDG_CACHE_HOME=self.shell_path(self.p/'cache with spaces'),REPORT=self.shell_path(self.p/'report'),GDK_PIXBUF_MODULE_FILE='/malicious/host.cache',GDK_PIXBUF_MODULEDIR='/malicious/host/loaders')
    def tearDown(self):self.tmp.cleanup()
    def launch(self):return subprocess.run([BASH,str(self.stage/'AppRun'),'argument with spaces'],env=self.env,text=True,capture_output=True)
    def test_matching_modules_ignore_inherited_cache_and_relocate(self):
        result=self.launch();self.assertEqual(result.returncode,0,result.stderr)
        report=(self.p/'report').read_text();self.assertNotIn('/malicious',report)
        self.assertIn('argument with spaces',report);self.assertIn('candidate with spaces/usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so',report)
        self.assertEqual(len(list((self.p/'cache with spaces').rglob('loaders.cache'))),1)
        self.assertFalse(list((self.p/'cache with spaces').rglob('loaders.cache.*')))
        previous=self.stage;self.stage=self.p/'relocated package with spaces';previous.rename(self.stage)
        result=self.launch();self.assertEqual(result.returncode,0,result.stderr)
        report=(self.p/'report').read_text();self.assertIn('relocated package with spaces/usr/lib/gdk-pixbuf-loaders',report)
        self.assertNotIn('candidate with spaces',report)
    def test_missing_svg_fails_before_app(self):
        (self.stage/'usr/lib/gdk-pixbuf-loaders/libpixbufloader-svg.so').unlink()
        result=self.launch();self.assertNotEqual(result.returncode,0);self.assertFalse((self.p/'report').exists())
    def test_missing_query_fails_before_app(self):
        self.query.unlink();result=self.launch();self.assertNotEqual(result.returncode,0);self.assertFalse((self.p/'report').exists())
    def test_query_failure_preserves_cache_without_temporary_leaks(self):
        self.assertEqual(self.launch().returncode,0)
        cache=next((self.p/'cache with spaces').rglob('loaders.cache'));original=cache.read_bytes()
        self.query.write_text('#!/bin/sh\necho partial\nexit 42\n')
        result=self.launch();self.assertNotEqual(result.returncode,0)
        self.assertEqual(cache.read_bytes(),original);self.assertFalse(list(cache.parent.glob('loaders.cache.*')))
        self.assertFalse(list(cache.parent.glob('query-errors.*')))
    def test_zero_exit_incomplete_or_diagnostic_query_rejected(self):
        for query in ('#!/bin/sh\necho "# header only"\n',
                      '#!/bin/sh\nfor module do printf \'"%s"\\n\' "$module"; done\n',
                      '#!/bin/sh\nfor module do printf \'"%s"\\n\' "$module"; done\nprintf \'"svg" 5\\n\'\necho loader-error >&2\n'):
            self.query.write_text(query)
            result=self.launch();self.assertNotEqual(result.returncode,0);self.assertFalse((self.p/'report').exists())
            self.assertFalse(list((self.p/'cache with spaces').rglob('loaders.cache.*')))
            self.assertFalse(list((self.p/'cache with spaces').rglob('query-errors.*')))

if __name__=='__main__':unittest.main(verbosity=2)
