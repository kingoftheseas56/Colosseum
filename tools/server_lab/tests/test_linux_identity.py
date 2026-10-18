"""Linux-only process identity parsing and conservative process-group handling."""
import sys
import json
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
from tools.server_lab import lab

@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux /proc contract')
class LinuxIdentityTests(unittest.TestCase):
    def test_comm_spaces_and_parentheses_do_not_shift_start_time(self):
        tail=['S']+['0']*18+['123456']+['0']*5
        stat='987 (a name ) with spaces) '+' '.join(tail)
        def read(path,**kwargs):
            return stat if str(path).endswith('/stat') else 'boot-fixture\n'
        with patch('pathlib.Path.read_text',read), patch('os.readlink',return_value='pid:[77]'):
            value=lab._native_process_creation(987)
        self.assertEqual(value,'linux:boot-fixture:pid:[77]:123456')

    def test_non_group_leader_never_kills_callers_group(self):
        with patch('os.getpgid',return_value=123), patch('os.killpg') as group, patch('os.kill') as one:
            lab._terminate_tree(987)
        group.assert_not_called()
        one.assert_called_once_with(987,lab.signal.SIGKILL)

    def test_zombie_is_not_a_live_process(self):
        with patch('pathlib.Path.read_text',return_value='987 (zombie) Z '+'0 '*25), patch('os.kill'):
            self.assertFalse(lab._pid_alive(987))

    def test_invalid_pid_cannot_signal_any_process(self):
        for pid in (0,-1):
            with self.subTest(pid=pid), patch('os.killpg') as group, patch('os.kill') as one:
                with self.assertRaises(ValueError):lab._terminate_tree(pid)
                group.assert_not_called();one.assert_not_called()

    def test_linux_image_comparison_is_case_sensitive(self):
        with tempfile.TemporaryDirectory() as root:
            run=Path(root)/'run';run.mkdir()
            (run/'ownership.json').write_text(json.dumps({'pid':987,'identity':{'creation':'same','image':'/tmp/Case'},'controller':{}}))
            with patch.object(lab,'_native_process_identity',return_value={'creation':'same','image':'/tmp/case'}), patch.object(lab,'_pid_alive',return_value=True), patch.object(lab,'_process_identity',return_value=None), patch.object(lab,'_terminate_tree') as kill, patch.object(lab,'_wait_for_release',return_value=[]):
                result=lab.cleanup_orphans(Path(root))
            kill.assert_not_called()
            self.assertIn(str(run),result['skipped_mismatches'])

    def test_changed_audit_creation_prevents_termination(self):
        with tempfile.TemporaryDirectory() as root:
            run=Path(root)/'run';run.mkdir()
            (run/'ownership.json').write_text(json.dumps({'pid':987,'identity':{'creation':'first','image':'/tmp/python','subject':'owned','mode':'test','token':'token'},'controller':{}}))
            with patch.object(lab,'_native_process_identity',return_value={'creation':'first','image':'/tmp/python'}), patch.object(lab,'_pid_alive',return_value=True), patch.object(lab,'_process_identity',return_value={'command':'owned test token','creation':'replacement'}), patch.object(lab,'_terminate_tree') as kill, patch.object(lab,'_wait_for_release',return_value=[]):
                result=lab.cleanup_orphans(Path(root))
            kill.assert_not_called()
            self.assertIn(str(run),result['skipped_mismatches'])

if __name__=='__main__':unittest.main()
