import unittest, tempfile
from pathlib import Path
import capture_native_promos_v2 as m
class Checks(unittest.TestCase):
 def q(self):return {'visible':True,'rect':{'x':0,'y':0,'width':100,'height':100},'rootWindow':{'width':1280,'height':720}}
 def test_geometry(self):self.assertTrue(m.center_visible(self.q()))
 def test_clipped_center(self):
  q=self.q();q['clipChain']=[{'sceneRect':{'x':0,'y':0,'width':20,'height':100}}];self.assertFalse(m.center_visible(q))
 def test_hidden(self):
  q=self.q();q['visible']=False;self.assertIsNone(m.bounds(q))
 def test_art(self):
  self.assertFalse(m.art_ok([{'source':'wallpaper.jpg','area':921600,'status':1}]));self.assertFalse(m.art_ok([]));self.assertFalse(m.art_ok([{'source':'/icons/icon.svg','area':10000,'status':1}]))
  self.assertTrue(m.art_ok([{'source':'art.jpg','area':10000,'status':1,'substantive':True}]))
  self.assertFalse(m.art_ok([{'source':'art.jpg','area':10000,'status':3,'substantive':True}]))
 def test_hashed_backdrop_and_unloaded_card(self):
  self.assertFalse(m.art_ok([{'source':'/assets/content/abc.webp','area':900000,'status':1,'substantive':False},{'source':'card.webp','area':20000,'status':2,'substantive':True}]))
 def test_descendants(self):
  card={'handle':'a','class':'UniverseGalleryCard'};image={'handle':'b','parentHandle':'a','class':'QQuickImage'}
  self.assertTrue(m.substantive(image,{'a':card,'b':image}))
  self.assertFalse(m.substantive({'handle':'c','class':'QQuickImage'},{}))
 def test_banner_art_identity(self):
  self.assertTrue(m.substantive({'handle':'a','objectName':'universeBannerArt'},{}))
  self.assertFalse(m.substantive({'handle':'b','objectName':'universeBannerMark'},{}))
  self.assertTrue(m.art_ok([{'name':'universeBannerArt','substantive':True,'source':'/assets/content/abc.webp','status':1,'area':50000}]))
 def test_image_wrapper_excluded(self):
  self.assertTrue(m.is_image_class('QQuickImage'))
  self.assertTrue(m.is_image_class('QQuickImage_QML_123'))
  self.assertFalse(m.is_image_class('RoundedPosterImage_QMLTYPE_123'))
  self.assertFalse(m.is_image_class('ShaderEffectSource'))
 def test_scoped_back_uses_click(self):
  calls=[]
  class D:
   def call(self,*args):calls.append(args)
  c=m.Capture.__new__(m.Capture);c.d=D();c.hold=lambda n:None
  c.tree=lambda root:[{'class':'BackAction_QMLTYPE_42','handle':'back'}]
  c.query=lambda h:self.q()
  c.page_back('galaxyUniversePage')
  self.assertEqual(calls,[('ui-click',{'target':'back'})])
 def test_world_closes_overlay_before_world_click(self):
  calls=[];active=[True,False]
  class D:
   def props(self,*args):return {'active':active.pop(0)}
   def ensure_world(self,name):calls.append('world')
  c=m.Capture.__new__(m.Capture);c.d=D();c.hold=lambda n:None;c.page_back=lambda root:calls.append('back')
  c.world();self.assertEqual(calls,['back','world'])
 def test_original_run_hash_preserved(self):
  import hashlib
  p=Path(__file__).parent/'capture_native_promos.py'
  if p.exists():self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),'3882504eb5cf7d6f6372ac3f256bc92f49343b3257d377832416340e032fa498')
 def test_allowed(self):
  class Base:
   def call(self,*a):return a
  d=m.make_driver(Base)();self.assertEqual(d.call('ui-keypress',{'key':'Escape'})[0],'ui-keypress')
  for cmd in ['qml-set','qml-eval','invoke','emit','install']:
   with self.assertRaises(RuntimeError):d.call(cmd)
 def test_output_contract(self):
  with tempfile.TemporaryDirectory() as tmp:
   clip=Path(tmp)/'onepiece';clip.mkdir();(clip/'ipc').mkdir()
   m.save(clip/'ipc'/'0001-get-state.json',{'ok':True})
   self.assertTrue((clip/'ipc'/'0001-get-state.json').is_file())
 def test_inventory(self):self.assertEqual(len(m.NAMES),20);self.assertEqual(len(set(m.NAMES)),20)
if __name__=='__main__':unittest.main()
