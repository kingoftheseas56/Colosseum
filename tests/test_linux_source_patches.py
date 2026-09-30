import importlib.util
import hashlib
import io
import json
import re
import sys
import tarfile
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SourcePatchContract(unittest.TestCase):
    def test_patch_is_exact_and_detects_extra_edits(self):
        path = ROOT / 'scripts/linux_release/source_patches.py'
        self.assertTrue(path.is_file(), 'explicit patch provenance helper missing')
        spec = importlib.util.spec_from_file_location('source_patches', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'source'
            subprocess.run(['git', 'clone', '--quiet', '--shared', str(ROOT), str(source)], check=True)
            subprocess.run(['git', '-C', str(source), 'checkout', '--quiet', module.BASE], check=True)
            with self.assertRaises(RuntimeError):
                module.verify(source)
            evidence = pathlib.Path(directory) / 'evidence'
            subprocess.run([sys.executable, str(path), '--source', str(source), '--evidence', str(evidence)], check=True)
            info = module.verify(source)
            self.assertEqual(info['source_base_sha'], module.BASE)
            self.assertEqual(len(info['source_patches']), 1)
            cmake = (source / 'native/CMakeLists.txt').read_text()
            block = cmake.split('add_executable(reader2_profile_runtime_harness', 1)[1].split('# Task 12:', 1)[0]
            for unit in ['RatingsReviewsStore', 'TrackerDeliveryRuntime', 'TrackerScrobbleRuntime', 'TrackerSyncCenterModel', 'SimklApiClient', 'SimklConnectionController', 'SimklSyncRuntime']:
                self.assertIn(unit + '.cpp', block)
            self.assertIn('Qt6::Gui', block)
            helper = (source / 'tests/CMakeLists.txt').read_text().split('function(colosseum_add_tracker_delivery_runtime_sources target)', 1)[1].split('endfunction()', 1)[0]
            for unit in re.findall(r'trackers/[^"\n]+', helper):
                self.assertIn(unit, block)
            recorded = json.loads((evidence / 'source-provenance.json').read_text())
            self.assertEqual(recorded, info)
            self.assertEqual(hashlib.sha256((evidence / 'source-applied.diff').read_bytes()).hexdigest(), info['source_diff_sha256'])
            archive = subprocess.check_output(['git', '-C', str(source), 'archive', info['source_tree_sha']])
            with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
                self.assertEqual(stream.extractfile('native/CMakeLists.txt').read().decode(), cmake)
            extra = source / 'untracked-source.txt'
            extra.write_text('unexpected')
            with self.assertRaises(RuntimeError):
                module.verify(source)
            extra.unlink()

            with self.assertRaises(RuntimeError):
                module.apply(source)
            with (source / 'native/CMakeLists.txt').open('a') as stream:
                stream.write('\n# unrelated edit\n')
            with self.assertRaises(RuntimeError):
                module.verify(source)


if __name__ == '__main__':
    unittest.main()
