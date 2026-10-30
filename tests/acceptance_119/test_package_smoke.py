"""Modeled normal-AppRun wrapper contracts; actual desktop execution is separate."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest

class PackageSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.api = None
        path = Path(__file__).with_name('package_smoke.py')
        if path.exists():
            sys.path.insert(0, str(path.parent))
            spec = importlib.util.spec_from_file_location('package_smoke_test', path)
            cls.api = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.api)
    def setUp(self):
        self.assertIsNotNone(self.api, 'Normal-AppRun helper is not implemented')
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
    def owned(self):
        tag='119-package-fixture'; pipe='ColosseumLanista-'+tag
        env=self.api.isolated_env(self.root,tag,pipe,{'HOME':'/unowned','COLOSSEUM_QML':'/unowned.qml','LD_LIBRARY_PATH':'/sdk','QTWEBENGINE_DISABLE_SANDBOX':'1','DISPLAY':':1'})
        state={'type':'reply','appDataRoot':str(Path(env['XDG_DATA_HOME'])/'Brotherhood'/('Colosseum-dltest-'+tag)), 'cacheRoot':str(Path(env['XDG_CACHE_HOME'])/'Brotherhood'/('Colosseum-dltest-'+tag)), 'windows':[{'visible':True,'width':1024,'height':700}]}
        ping={'type':'reply','schema':'colosseum.dev.v1.0','pid':123,'pipe':pipe,'gates':{'drive':True,'write':False}}
        return tag,pipe,env,ping,state
    def test_environment_cannot_inherit_qml_or_development_paths(self):
        tag,pipe,env,ping,state=self.owned()
        self.assertNotIn('COLOSSEUM_QML',env);self.assertNotIn('LD_LIBRARY_PATH',env)
        self.assertNotIn('QTWEBENGINE_DISABLE_SANDBOX',env)
        self.assertEqual(env['HOME'],str(self.root/'profile/home'));self.assertEqual(env['DISPLAY'],':1')
        self.assertEqual(env['COLOSSEUM_LANISTA_PIPE'],pipe)
    def test_real_pid_profile_and_dimensions_are_required(self):
        tag,pipe,env,ping,state=self.owned()
        self.assertEqual(self.api.ownership_failures(ping,state,123,tag,pipe,env,1024,700),[])
        ping['pid']=999
        self.assertIn('Owned ping identity mismatch',self.api.ownership_failures(ping,state,123,tag,pipe,env,1024,700))
        ping['pid']=123;state['appDataRoot']='/unowned/Colosseum-dltest-'+tag
        self.assertTrue(self.api.ownership_failures(ping,state,123,tag,pipe,env,1024,700))
    def test_unverified_or_wrong_live_identity_cannot_drive_failure_cleanup(self):
        self.assertTrue(hasattr(self.api, 'cleanup_bridge_allowed'), 'Failure bridge calls need an explicit verified-ownership gate')
        binary=Path(sys.executable).resolve(); digest=self.api.acceptance.sha256(binary)
        self.assertFalse(self.api.cleanup_bridge_allowed(False,os.getpid(),binary,digest))
        self.assertFalse(self.api.cleanup_bridge_allowed(True,os.getpid(),self.root/'foreign',digest))
        self.assertFalse(self.api.cleanup_bridge_allowed(True,os.getpid(),binary,'0'*64))
        self.assertTrue(self.api.cleanup_bridge_allowed(True,os.getpid(),binary,digest))
    def test_wrong_actual_dimensions_fail(self):
        tag,pipe,env,ping,state=self.owned();state['windows'][0]['width']=1280
        self.assertIn('Actual window dimensions differ from requested viewport',self.api.ownership_failures(ping,state,123,tag,pipe,env,1024,700))
    def test_narrow_settings_use_verified_linux_keys(self):
        path=self.api.write_window_settings(self.root,'119-package-fixture',1024,700)
        self.assertEqual(path,self.root/'Brotherhood/Colosseum-dltest-119-package-fixture.conf')
        text=path.read_text()
        for expected in ('[window]','baseMode=windowed','fillPolicy=2','maximized=false','normalGeometry=@Rect(40 40 1024 700)'):self.assertIn(expected,text)
        with self.assertRaises(ValueError):self.api.write_window_settings(self.root,'x',900,600)
    def test_failure_and_forced_cleanup_cannot_pass(self):
        self.assertEqual(self.api.verdict(False,[]),'BLOCKED')
        self.assertEqual(self.api.verdict(True,[]),'PASS')
        self.assertEqual(self.api.verdict(True,['Forced recovery']),'FAIL')
    def test_keep_going_only_forwards_native_flag_and_does_not_green_failures(self):
        self.assertTrue(hasattr(self.api, 'scenario_command'), 'Native command must support explicit keep-going')
        default=self.api.scenario_command('/package/lanista','owned-pipe','/owned/scenario.json')
        continued=self.api.scenario_command('/package/lanista','owned-pipe','/owned/scenario.json',True)
        self.assertNotIn('--keep-going',default)
        self.assertEqual(continued,default+['--keep-going'])
        self.assertEqual(self.api.verdict(True,['Native scenario exit 1']),'FAIL')
    def test_prepared_scenario_reuses_normal_quit_and_dimension_assertions(self):
        scenario={'steps':[{'label':'original','cmd':'ping','expect':[{'path':'type','op':'==','value':'reply'}]}]}
        got=self.api.prepare(scenario,1024,700)
        dimension=next(s for s in got['steps'] if s['label']=='package-actual-viewport')
        self.assertIn({'path':'windows.0.width','op':'==','value':1024},dimension['expect'])
        self.assertEqual(got['steps'][-2]['payload'],{'key':'Ctrl+Q'})

if __name__=='__main__':unittest.main(verbosity=2)
