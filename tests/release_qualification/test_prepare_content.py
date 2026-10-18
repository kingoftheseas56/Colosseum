"""No network/publisher execution: existing output must fail before source reads."""
import hashlib,subprocess,sys,tempfile,unittest
from pathlib import Path
class ExistingOutputTests(unittest.TestCase):
 def test_existing_output_untouched_even_if_source_missing(self):
  script=Path(__file__).with_name('prepare_content.py')
  with tempfile.TemporaryDirectory() as temp:
   output=Path(temp)/'existing';output.mkdir()
   sentinel=output/'content-A-report.json';sentinel.write_bytes(b'old unverified receipt must never be reused\n')
   before=hashlib.sha256(sentinel.read_bytes()).hexdigest()
   result=subprocess.run([sys.executable,str(script),'--source',str(Path(temp)/'missing-source'),'--output',str(output)],capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0);self.assertIn('Fresh output directory required',result.stderr)
   self.assertEqual(list(output.iterdir()),[sentinel]);self.assertEqual(hashlib.sha256(sentinel.read_bytes()).hexdigest(),before)
 def test_empty_existing_output_also_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   output=Path(temp)/'existing';output.mkdir()
   result=subprocess.run([sys.executable,str(Path(__file__).with_name('prepare_content.py')),'--source',str(Path(temp)/'missing-source'),'--output',str(output)],capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0);self.assertIn('Fresh output directory required',result.stderr);self.assertEqual(list(output.iterdir()),[])
if __name__=='__main__':unittest.main()
