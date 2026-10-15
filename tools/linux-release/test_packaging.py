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
        (s/'qml/Main.qml').write_text('import QtQuick\nItem {}\n')
        for name in ('LICENSE','THIRD_PARTY_NOTICES.md'):(s/name).write_text('Test fixture only\n')
        (b/'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\n')
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
if __name__=='__main__':unittest.main(verbosity=2)
