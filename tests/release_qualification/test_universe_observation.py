"""QA-only bridge regression. No native app, network, or model injection."""
import json,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
import universe_journey as u

# Exact props from retained run39138795905 universe-before/ipc/0028-qml-get.json.
RECORDED_PROPS={'roster':None,'shown':None,'universeContentGeneration':2}

def query():return {'visible':True,'opacity':1,'enabled':True,'rect':{'x':50,'y':200,'width':300,'height':178},'rootWindow':{'width':800,'height':800},'clipChain':[]}
class Driver:
 def __init__(self,names):self.names=names;self.clicked=[];self.reads=[];self.grabs=[]
 def ensure_world(self,world):assert world=='Universes'
 def props(self,target,keys):
  self.reads+=keys
  assert not set(keys)&{'roster','shown','entry','runs','item','uni'},'No unsupported QML var reads'
  if target=='universesWorld':return {'universeContentGeneration':2,'facet':''}
  if target=='universesCount':return {'text':str(len(self.names))+' universes'}
  if target.startswith('title-'):return {'text':self.names[int(target.split('-')[1])]}
  if target=='universeLayer':return {'active':True,'universeName':u.NAME,'templatePage':'shelves','status':1}
  if target=='shelvesMedium_qa-films':return {'title':'QA Films','kind':'movie','key':'qa-films','total':1,'visible':True}
  if target=='cover':return {'source':'http://127.0.0.1/assets/content/'+'a'*64+'.webp','status':1,'visible':True,'width':164,'height':246}
  if target=='caption':return {'text':u.TITLE,'visible':True}
  raise AssertionError(target)
 def tree(self,root):
  if root=='universesLedger':
   return [r for i in range(len(self.names)) for r in [{'class':'UniverseBanner_QMLTYPE_1','handle':f'banner-{i}'},{'class':'QQuickColumn','objectName':'universeBannerCopy','handle':f'copy-{i}','parentHandle':f'banner-{i}'},{'class':'QQuickText','handle':f'title-{i}','parentHandle':f'copy-{i}'}]]
  if root=='universeLayer':return [{'class':'QQuickImage','objectName':'shelvesCover_qa-films:0','handle':'cover'}]
  if root=='shelvesMedium_qa-films':return [{'class':'QQuickText','handle':'caption'}]
  raise AssertionError(root)
 def call(self,cmd,payload):
  if cmd=='ui-query':return query()
  if cmd=='ui-click':self.clicked.append(payload['target']);return {'clicked':payload['target']}
  raise AssertionError(cmd)
 def grab(self,name,target):self.grabs.append(name)
class ObservationTests(unittest.TestCase):
 def test_recorded_reply_reproduces_original_failure(self):
  # Preserve the original failing expression; never treat its null as []/absence.
  with self.assertRaisesRegex(TypeError,"NoneType.*has no len"):
   x=RECORDED_PROPS
   x if x.get('universeContentGeneration',0)>0 and len(x.get('roster',[]))>0 else None
 def test_before_exhaustive_scalar_proof(self):
  d=Driver(['Attack on Titan','One Piece'])
  with tempfile.TemporaryDirectory() as t:
   r=u.run_before(d,None,t);saved=json.loads((Path(t)/'universe-A-roster.json').read_text())
  self.assertEqual(r['status'],'PASS');self.assertEqual(saved['declaredCount'],len(saved['entries']));self.assertEqual(saved['bannerCount'],2)
 def test_after_scalar_path(self):
  d=Driver(['Attack on Titan',u.NAME,'One Piece'])
  with tempfile.TemporaryDirectory() as t:r=u.run_after(d,None,t)
  self.assertEqual(r['status'],'PASS');self.assertEqual(d.clicked,['banner-1']);self.assertEqual(r['renderedTitle']['text'],u.TITLE);self.assertIn('not read as native QML var',r['identityScope'])
 def test_null_or_non_array_tree_is_not_empty_proof(self):
  for bad in [None,{},'not-an-array']:
   d=Driver(['One Piece']);d.tree=lambda root:bad
   with self.assertRaises(u.UniverseObservationError):u.observe_ledger(d)
 def test_null_scalar_title_rejected(self):
  d=Driver([None])
  with self.assertRaisesRegex(u.UniverseObservationError,'nonempty scalar'):u.observe_ledger(d)
 def test_incomplete_tree_count_rejected(self):
  d=Driver(['A','B']);orig=d.tree;d.tree=lambda root:orig(root)[:3]
  with self.assertRaisesRegex(u.UniverseObservationError,'completeness'):u.observe_ledger(d)
 def test_duplicate_names_rejected(self):
  with self.assertRaisesRegex(u.UniverseObservationError,'completeness'):u.observe_ledger(Driver(['A','A']))
 def test_before_cannot_pass_if_test_universe_present(self):
  with tempfile.TemporaryDirectory() as t:
   with self.assertRaisesRegex(AssertionError,'unexpectedly'):u.run_before(Driver([u.NAME]),None,t)
 def test_failure_retains_structured_observation(self):
  d=Driver(['A']);d.tree=lambda root:None
  with tempfile.TemporaryDirectory() as t,patch.object(u,'until',lambda fn:fn()):
   with self.assertRaises(u.UniverseObservationError):u.ledger_when(d,t,'bad',lambda x:True)
   r=json.loads((Path(t)/'bad-last-observation.json').read_text());self.assertIsNone(r['observation']['treeRows']);self.assertIn('Non-array',r['error'])
if __name__=='__main__':unittest.main()
