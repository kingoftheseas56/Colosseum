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
    def test_compiler_cache_preserves_failed_build_work(self):
        workflow = (ROOT / '.github/workflows/linux-117-package.yml').read_text()
        build = (ROOT / 'scripts/linux_release/build.sh').read_text()
        self.assertIn('CCACHE_DIR:', workflow)
        self.assertIn('CCACHE_COMPILERCHECK: content', workflow)
        self.assertIn('actions/cache/restore@caa296126883cff596d87d8935842f9db880ef25', workflow)
        self.assertIn('actions/cache/save@caa296126883cff596d87d8935842f9db880ef25', workflow)
        self.assertIn("always() && steps.ccache-key.outcome == 'success'", workflow)
        self.assertIn('linux117-ubuntu24.04-x86_64-qt6.11.1-gcc', workflow)
        self.assertIn('$(c++ -dumpfullversion)', workflow)
        self.assertIn('steps.ccache-restore.outputs.cache-primary-key', workflow)
        self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=ccache', build)
        self.assertIn('-DCMAKE_C_COMPILER_LAUNCHER=ccache', build)
        self.assertIn('ccache --show-stats', workflow)

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
            self.assertEqual(len(info['source_patches']), 2)
            cmake = (source / 'native/CMakeLists.txt').read_text()
            block = cmake.split('add_executable(reader2_profile_runtime_harness', 1)[1].split('# Task 12:', 1)[0]
            for unit in ['RatingsReviewsStore', 'TrackerDeliveryRuntime', 'TrackerScrobbleRuntime', 'TrackerSyncCenterModel', 'SimklApiClient', 'SimklConnectionController', 'SimklSyncRuntime']:
                self.assertIn(unit + '.cpp', block)
            self.assertIn('Qt6::Gui', block)
            helper = (source / 'tests/CMakeLists.txt').read_text().split('function(colosseum_add_tracker_delivery_runtime_sources target)', 1)[1].split('endfunction()', 1)[0]
            for unit in re.findall(r'trackers/[^"\n]+', helper):
                self.assertIn(unit, block)
            # Audit every effective target that compiles the three shared owners,
            # including late target_sources and the tracker helper's foreach.
            test_cmake = (source / 'tests/CMakeLists.txt').read_text()
            targets = {}
            for content in [cmake, test_cmake]:
                content = re.sub(r'#[^\n]*', '', content)
                for match in re.finditer(r'(?:add_executable|target_sources)\(\s*(\w+)\s+([^)]*)\)', content, re.S):
                    target, body = match.groups()
                    targets.setdefault(target, set()).update(re.findall(r'(?:account|trackers)/[\w]+\.(?:cpp|h)', body))
            helper_units = set(re.findall(r'trackers/[\w]+\.cpp', helper))
            tracker_targets = test_cmake.split('foreach(_tracker_profile_target IN ITEMS', 1)[1].split(')', 1)[0].split()
            for target in tracker_targets:
                targets[target].update(helper_units)
            audited = set()
            for target, units in targets.items():
                if 'account/ProfileStoreRuntime.cpp' in units:
                    audited.add(target)
                    self.assertTrue(helper_units <= units, (target, sorted(helper_units - units)))
                    self.assertIn('account/RatingsReviewsStore.cpp', units, target)
                if units & {'account/AccountRuntime.cpp', 'account/FirstAccountProfileCoordinator.cpp'}:
                    audited.add(target)
                    for unit in ['RatingsReviewsDelivery', 'RatingsReviewsDeliveryOutbox', 'RatingsReviewsDeliveryReceiptStore', 'RatingsReviewsDeliveryTypes', 'RatingsReviewsProviderMappingStore']:
                        for extension in ['cpp']:
                            self.assertIn('account/' + unit + '.' + extension, units, target)
            self.assertEqual(audited, {'colosseum', 'reader2_profile_runtime_harness', 'account_first_light', 'tst_privacy_policy', 'tst_account_attachment_runtime', 'tst_account_adoption', 'tst_account_shared_pc', 'tst_profile_activity_isolation', 'tst_ratings_reviews_journey'})
            recorded = json.loads((evidence / 'source-provenance.json').read_text())
            self.assertEqual(recorded, info)
            self.assertEqual(hashlib.sha256((evidence / 'source-applied.diff').read_bytes()).hexdigest(), info['source_diff_sha256'])
            archive = subprocess.check_output(['git', '-C', str(source), 'archive', info['source_tree_sha']])
            with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
                self.assertEqual(stream.extractfile('native/CMakeLists.txt').read().decode(), cmake)
                self.assertEqual(stream.extractfile('tests/CMakeLists.txt').read().decode(), test_cmake)
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
