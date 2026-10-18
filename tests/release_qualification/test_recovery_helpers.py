"""No network or application startup. Run in any Python 3.10+ environment."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).parent

def load(filename):
    spec=importlib.util.spec_from_file_location(filename.replace('.','_'),ROOT/filename)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

fixture=load('b1_catalogue_fault_fixture.py')
controller=load('native_recovery_controller.py' if (ROOT/'native_recovery_controller.py').exists() else 'b1_native_recovery_controller.py')

class RecoveryHelpers(unittest.TestCase):
    def test_response_modes(self):
        raw=b'{"items":[1,2,3]}'
        self.assertEqual(fixture.response(raw,'healthy'),(200,raw,len(raw)))
        self.assertEqual(fixture.response(raw,'zero'),(200,b'',0))
        truncated=fixture.response(raw,'truncated-json')
        self.assertEqual(truncated[2],len(truncated[1]));self.assertLess(truncated[2],len(raw))
        transfer=fixture.response(raw,'short-transfer')
        self.assertEqual(transfer[2],len(raw));self.assertLess(len(transfer[1]),transfer[2])
        self.assertEqual(fixture.response(raw,'unavailable'),(503,b'',0))
        with self.assertRaises(ValueError): fixture.response(raw,'unknown')
    def test_exact_identity(self):
        props={'currentType':'comics','automationItemCount':1,'pageFailed':False,'loading':False}
        cards=[{'objectName':'discoverCard_137131','properties':{'capText':'The Amazing Spider-Man'}}]
        expected={'cardIds':['discoverCard_137131'],'titles':['The Amazing Spider-Man'],'itemCount':1}
        self.assertTrue(controller.healthy_identity(props,cards,expected))
        self.assertFalse(controller.healthy_identity(props,[],expected))
        self.assertFalse(controller.healthy_identity(dict(props,pageFailed=True),cards,expected))
        self.assertFalse(controller.healthy_identity(dict(props,automationItemCount=2),cards,expected))
        before={'cards':cards,'properties':props}
        self.assertTrue(controller.same_cards(before,before))
        self.assertFalse(controller.same_cards(before,{'cards':[],'properties':props}))
    def test_cache_comparison(self):
        name='cache/'+('a'*64)+'.json'; value={'sha256':'abc','bytes':3}
        self.assertTrue(controller.compare_immutable_cache({name:value},{name:value})['passed'])
        self.assertFalse(controller.compare_immutable_cache({name:value},{})['passed'])
        self.assertFalse(controller.compare_immutable_cache({name:value},{name:{'sha256':'xyz','bytes':3}})['passed'])
        self.assertFalse(controller.compare_immutable_cache({}, {})['passed'])
    def test_retry_selector_parse(self):
        self.assertTrue(controller.retry_candidate({'accessibleName':'Try again','enabled':True}))
        self.assertFalse(controller.retry_candidate({'accessibleName':'Try again','enabled':False}))
        self.assertFalse(controller.retry_candidate({'accessibleName':'Retry Read','enabled':True}))
        self.assertFalse(controller.retry_candidate({}))

    def test_last_good_pair_validation(self):
        import hashlib,json
        origin='http://127.0.0.1:1234'
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); wrapper=root/'last-good.json'
            def prepare(index_revision='a'*64):
                index={'schemaVersion':1,'version':1,'revision':index_revision,'sourceRevision':'b'*64,'lists':{},'fixed':{},'search':{}}
                raw=json.dumps(index).encode(); digest=hashlib.sha256(raw).hexdigest()
                ref={'path':'/catalogue/'+('a'*64)+'/index-'+digest+'.json','sha256':digest,'bytes':len(raw)}
                entry={'schemaVersion':1,'revision':'a'*64,'sourceRevision':'b'*64,'index':ref}
                wrapper.write_text(json.dumps({'endpoint':origin,'entry':entry,'validatedAt':'2026-10-18T00:00:00.000Z'}))
                path=root/(digest+'.json');path.write_bytes(raw)
                return path
            self.assertFalse(controller.last_good_pairs(root,origin)['passed'])
            path=prepare(); baseline=controller.last_good_pairs(root,origin)
            self.assertTrue(baseline['passed'])
            changed=json.loads(wrapper.read_text());changed['validatedAt']='2026-10-18T00:01:00.000Z';wrapper.write_text(json.dumps(changed))
            self.assertTrue(controller.compare_last_good(baseline,controller.last_good_pairs(root,origin))['passed'])
            self.assertFalse(controller.last_good_pairs(root,'http://127.0.0.1:5678')['passed'])
            path.unlink();self.assertFalse(controller.last_good_pairs(root,origin)['passed'])
            path=prepare();path.write_bytes(b'{}');self.assertFalse(controller.last_good_pairs(root,origin)['passed'])
            prepare('c'*64);self.assertFalse(controller.last_good_pairs(root,origin)['passed'])
            prepare();wrapper.write_text('{}');self.assertFalse(controller.last_good_pairs(root,origin)['passed'])
    def test_request_receipt_acceptance(self):
        import hashlib
        raw=b'{"items":[1,2,3]}';path='/catalogue/current.json'
        for mode in ('healthy','zero','truncated-json'):
            status,body,length=fixture.response(raw,mode)
            row={'path':path,'mode':mode,'writeCompleted':True,'status':status,'bytes':len(body),
                 'advertisedBytes':length,'bodySha256':hashlib.sha256(body).hexdigest()}
            self.assertTrue(controller.request_matches(row,path,mode,raw))
            for key,value in [('writeCompleted',False),('status',503),('bytes',999),('advertisedBytes',999),('bodySha256','0'*64),('mode','unavailable')]:
                self.assertFalse(controller.request_matches(dict(row,**{key:value}),path,mode,raw),(mode,key))
            self.assertFalse(controller.request_matches(row,'/wrong',mode,raw))

    def test_atomic_control(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'control.json'
            controller.atomic_json(path,{'target':'index','mode':'zero','publication':'A'})
            self.assertEqual(json.loads(path.read_text())['mode'],'zero')
            controller.atomic_json(path,{'target':'index','mode':'healthy','publication':'B'})
            self.assertEqual(json.loads(path.read_text())['publication'],'B')
            self.assertFalse(path.with_suffix('.tmp').exists())

if __name__=='__main__': unittest.main()
