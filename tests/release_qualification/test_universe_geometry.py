import copy,unittest
from universe_journey import center_in_view,visible_bounds,scroll_toward

def query(x,y,w,h):return {'visible':True,'opacity':1,'enabled':True,'rect':{'x':x,'y':y,'width':w,'height':h},'rootWindow':{'width':1280,'height':720},'clipChain':[]}
class GeometryTests(unittest.TestCase):
 def test_visible_center(self):self.assertTrue(center_in_view(query(10,200,100,100),query(0,100,1280,620)))
 def test_partial_banner_center_outside_window(self):self.assertFalse(center_in_view(query(10,600,100,400),query(0,100,1280,620)))
 def test_center_outside_scroll_viewport(self):self.assertFalse(center_in_view(query(10,500,100,200),query(0,100,1280,450)))
 def test_nested_clip(self):
  q=query(10,200,100,100);q['clipChain']=[{'sceneRect':{'x':0,'y':0,'width':1280,'height':230}}]
  self.assertFalse(center_in_view(q,query(0,100,1280,620)))
 def test_horizontal_clip(self):self.assertFalse(center_in_view(query(1250,200,200,100),query(0,100,1280,620)))
 def test_zero_hidden_and_nan(self):
  for q in [query(0,0,0,100),query(0,float('nan'),100,100),dict(query(0,0,100,100),visible=False)]:self.assertIsNone(visible_bounds(q))
 def test_partial_center_visible_allowed(self):self.assertTrue(center_in_view(query(10,600,100,178),query(0,100,1280,620)))
 def test_wheel_direction_and_safe_point(self):
  class Driver:
   def call(self,cmd,payload):return query(0,100,1280,900) if cmd=='ui-query' else payload
  down=scroll_toward(Driver(),'scroll',query(10,800,100,100));up=scroll_toward(Driver(),'scroll',query(10,0,100,100))
  self.assertLess(down['dy'],0);self.assertGreater(up['dy'],0)
  self.assertGreater(down['yFraction'],0);self.assertLess(100+900*down['yFraction'],720)
if __name__=='__main__':unittest.main()
