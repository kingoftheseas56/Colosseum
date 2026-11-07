#!/usr/bin/env python3
"""Bounded cleanup tests; never send signals to real processes."""
import importlib.util
from pathlib import Path
import signal
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('proof',Path(__file__).with_name('nuvio_runtime_proof.py'))
proof=importlib.util.module_from_spec(spec);spec.loader.exec_module(proof)

class OwnedHelperCleanupTests(unittest.TestCase):
    exe='/opt/colosseum/usr/bin/colosseum-nuvio-host'
    def record(self): return {'pid':1234,'startTicks':'7654','exe':self.exe}

    def test_matching_identity_uses_pinned_pidfd(self):
        with patch.object(proof.os,'pidfd_open',return_value=41) as opened, \
             patch.object(proof,'process_identity',return_value=self.record()), \
             patch.object(proof.signal,'pidfd_send_signal') as sent, \
             patch.object(proof.os,'close') as close:
            self.assertEqual(proof.kill_verified_helpers([self.record()],self.exe)[0]['outcome'],'SIGKILL')
            opened.assert_called_once_with(1234)
            sent.assert_called_once_with(41,signal.SIGKILL)
            close.assert_called_once_with(41)

    def test_reused_pid_is_not_signalled(self):
        changed=self.record();changed['startTicks']='9999'
        with patch.object(proof.os,'pidfd_open',return_value=41), \
             patch.object(proof,'process_identity',return_value=changed), \
             patch.object(proof.signal,'pidfd_send_signal') as sent, \
             patch.object(proof.os,'close'):
            self.assertEqual(proof.kill_verified_helpers([self.record()],self.exe)[0]['outcome'],'identity_changed_not_signalled')
            sent.assert_not_called()

    def test_wrong_executable_not_opened(self):
        changed=self.record();changed['exe']='/other/helper'
        with patch.object(proof.os,'pidfd_open') as opened:
            self.assertEqual(proof.kill_verified_helpers([changed],self.exe),[])
            opened.assert_not_called()

    def test_already_exited_is_safe(self):
        with patch.object(proof.os,'pidfd_open',side_effect=ProcessLookupError):
            self.assertEqual(proof.kill_verified_helpers([self.record()],self.exe)[0]['outcome'],'already_exited')

    def test_collects_owned_children_before_shutdown_and_rechecks_after(self):
        process=Mock(pid=77);process.poll.return_value=None
        calls=[]
        def kill(records,exe):
            calls.append(('kill',list(records),exe));return []
        def stop(app): calls.append(('stop',app))
        with patch.object(proof,'helper_children',return_value=[self.record()]) as children, \
             patch.object(proof,'kill_verified_helpers',side_effect=kill):
            proof.cleanup_owned_processes(process,[],self.exe,stop)
            children.assert_called_once_with(77)
        self.assertEqual([call[0] for call in calls],['kill','stop','kill'])
        self.assertEqual(calls[0][1],calls[2][1])

    def test_exited_app_does_not_adopt_unrelated_helpers(self):
        process=Mock(pid=77);process.poll.return_value=1
        with patch.object(proof,'helper_children') as children, \
             patch.object(proof,'kill_verified_helpers',return_value=[]):
            proof.cleanup_owned_processes(process,[self.record()],self.exe,Mock())
            children.assert_not_called()

if __name__=='__main__': unittest.main()
