import hashlib,json,tempfile,unittest
from pathlib import Path
from verify_packet import verify
class Tests(unittest.TestCase):
 def fixture(self,p):
  (p/'file').write_bytes(b'abc');(p/'SHA256-INDEX.json').write_text(json.dumps([{'path':'file','bytes':3,'sha256':hashlib.sha256(b'abc').hexdigest()}]));return p/'SHA256-INDEX.json'
 def test_good(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);self.assertEqual(verify(p,self.fixture(p)),1)
 def test_corrupt(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);i=self.fixture(p);(p/'file').write_bytes(b'xyz')
   with self.assertRaises(ValueError):verify(p,i)
 def test_traversal(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);i=self.fixture(p);r=json.loads(i.read_text());r[0]['path']='../file';i.write_text(json.dumps(r))
   with self.assertRaises(ValueError):verify(p,i)
 def test_duplicate(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);i=self.fixture(p);r=json.loads(i.read_text());i.write_text(json.dumps(r+r))
   with self.assertRaises(ValueError):verify(p,i)
if __name__=='__main__':unittest.main()
