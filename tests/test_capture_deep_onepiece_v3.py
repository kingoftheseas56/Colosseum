import unittest
import capture_deep_onepiece_v3 as d
class Tests(unittest.TestCase):
 def test_exact_arcs(self):self.assertEqual([x[1] for x in d.SEAS],['romance','arabasta','dressrosa','egghead'])
 def test_loading(self):
  self.assertTrue(d.loading_done({'animeLoading':False,'liveLoading':False,'onePaceLoading':False}))
  self.assertFalse(d.loading_done({'animeLoading':False,'liveLoading':True,'onePaceLoading':False}));self.assertFalse(d.loading_done({}))
 def test_ancestry(self):
  rows=[{'handle':'card','class':'OnePieceVolumeCard_QMLTYPE_3'},{'handle':'wrap','parentHandle':'card','class':'QQuickRectangle'},{'handle':'img','parentHandle':'wrap','class':'QQuickImage'}]
  self.assertTrue(d.actual_card_image(rows[-1],rows));self.assertFalse(d.actual_card_image({'handle':'hero','class':'QQuickImage'},rows))
 def test_cycle(self):self.assertEqual(len(list(d.ancestors({'handle':'a','parentHandle':'a'},[{'handle':'a','parentHandle':'a'}]))),1)
 def test_wrapper_not_image(self):self.assertFalse(d.actual_card_image({'handle':'a','class':'RoundedPosterImage'},[]))
 def test_probe_is_default(self):self.assertTrue(d.DeepCapture.probing)
 def test_no_media_tabs_in_plan(self):self.assertEqual(len(d.SEAS),4)

from unittest.mock import Mock, patch
class ActionFailures(unittest.TestCase):
 def cap(self):
  c=object.__new__(d.DeepCapture);c.d=Mock();c.hold=Mock();c.event=Mock();c.dwell=Mock();return c
 def test_missed_arc_click(self):
  c=self.cap();c.d.props.return_value={'activeSeaId':'east-blue','arcPageOpen':False}
  with self.assertRaisesRegex(RuntimeError,'not open'):c.route('east-blue','Romance Dawn')
 def test_unchanged_vertical(self):
  c=self.cap();c.viewport=Mock(return_value='v');c.query=Mock(return_value={});c.d.props.return_value={'contentY':0}
  with self.assertRaisesRegex(RuntimeError,'did not move'):c.vertical_step('onePieceArcCatalogue')
 def test_missing_required_lane(self):
  c=self.cap();c.reveal_lane=Mock(return_value=None)
  with self.assertRaisesRegex(RuntimeError,'Required lane missing'):c.show_lane('Anime')
 def test_empty_required_lane(self):
  c=self.cap();c.reveal_lane=Mock(return_value=({},[]));c.lane_evidence=Mock(return_value={'cardCount':0})
  with self.assertRaisesRegex(RuntimeError,'No real cards'):c.show_lane('Anime')
 def test_unchanged_zoom(self):
  c=self.cap();c.tree=Mock(return_value=[{'handle':'atlas','class':'OnePieceEastBlueAtlas'}]);c.query=Mock(return_value={});c.d.props.return_value={'zoom':1};c.exact_click=Mock()
  with patch.object(d.base,'bounds',return_value=(0,0,1280,720)):
   with self.assertRaisesRegex(RuntimeError,'Zoom did not change'):c.map_zoom('eastBlue','east-blue')
 def test_unchanged_drag(self):
  c=self.cap();c.lane=Mock(return_value=({'handle':'lane'},[]));c.d.props.return_value={'contentX':0,'contentWidth':2000,'width':1000};c.query=Mock(return_value={})
  with patch.object(d.base,'bounds',return_value=(0,100,1000,400)),patch.object(d.subprocess,'run') as run:
   with self.assertRaisesRegex(RuntimeError,'Horizontal drag did not move'):c.drag_lane('Anime')
   self.assertIn(['xdotool','mouseup','1'],[x.args[0] for x in run.call_args_list])
 def test_release_even_mousedown_fails(self):
  c=self.cap();c.lane=Mock(return_value=({'handle':'lane'},[]));c.d.props.return_value={'contentX':0,'contentWidth':2000,'width':1000};c.query=Mock(return_value={})
  with patch.object(d.base,'bounds',return_value=(0,100,1000,400)),patch.object(d.subprocess,'run',side_effect=[RuntimeError('press failed'),None]) as run:
   with self.assertRaisesRegex(RuntimeError,'press failed'):c.drag_lane('Anime')
   self.assertEqual(run.call_args.args[0],['xdotool','mouseup','1'])
 def test_real_images_must_be_ready(self):
  self.assertFalse(d.image_readiness([{'source':'cover','status':2}]))
  self.assertFalse(d.image_readiness([{'source':'','status':0}]))
  self.assertTrue(d.image_readiness([{'source':'cover','status':1},{'source':'','status':0}]))
 def test_window_origin(self):
  with patch.object(d.subprocess,'run',side_effect=[Mock(stdout='17'),Mock(stdout='X=10\nY=0\nWIDTH=1280\nHEIGHT=720')]):
   with self.assertRaisesRegex(RuntimeError,'origin'):d.verify_window(123)

if __name__=='__main__':unittest.main()
