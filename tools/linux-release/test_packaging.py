#!/usr/bin/env python3
"""Adverse-state tests use a tiny native ELF fixture, not Colosseum qualification."""
import json, os, shutil, subprocess, tempfile, unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
PATCH=os.environ.get('PATCHELF') or shutil.which('patchelf')
if not PATCH:raise SystemExit('patchelf is required for packaging tests')
class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
        s=self.p/'source';b=self.p/'build';q=self.p/'qt';d=self.p/'deps'
        for p in (s,b,q,d):p.mkdir()
        for name in ('qml','assets','resources','data'):(s/name).mkdir()
        (s/'native').mkdir();(s/'native/CMakeLists.txt').write_text('project(colosseum VERSION 1.1.9 LANGUAGES C CXX)\n')
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
        self.assertEqual(self.check()[0],0)
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
        extracted=self.p/'deb-extracted';subprocess.run(['dpkg-deb','--extract',str(out),str(extracted)],check=True)
        self.assertEqual((extracted/'opt/colosseum/usr/bin/colosseum').read_bytes(),(self.stage/'usr/bin/colosseum').read_bytes())
        self.assertTrue((extracted/'usr/share/applications/colosseum.desktop').is_file())
        self.assertTrue(os.access(extracted/'usr/bin/colosseum',os.X_OK))
        self.assertIn('/opt/colosseum/AppRun',(extracted/'usr/bin/colosseum').read_text())
    def test_deb_rejects_qml_drift_before_creating_output(self):
        (self.stage/'qml/Main.qml').write_text('changed')
        out=self.p/'bad.deb'
        proc=subprocess.run(['python3',str(HERE/'package_deb.py'),'--stage',str(self.stage),'--output',str(out)],text=True,capture_output=True)
        self.assertNotEqual(proc.returncode,0);self.assertFalse(out.exists())
if __name__=='__main__':unittest.main(verbosity=2)
