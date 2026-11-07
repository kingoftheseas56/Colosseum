"""Fail-closed unit checks for the actual Android proof driver and APK report.

These are harness checks, not a substitute for the emulator proof itself.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/android" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


proof = load_module("nuvio_emulator_proof")
sizes = load_module("report_nuvio_apk_size")


def records():
    result = []
    for index, stage in enumerate(proof.STAGES):
        record = {"stage": stage, "nonce": "fresh", "appPid": 100, "ok": True}
        if stage in ("stremio_fixture_installed", "direct_stream", "stremio_requeried", "completed"):
            record.update(sourceKind="live_https_controlled_fixture")
        if stage in ("stremio_requeried", "completed"):
            record.update(stremioRequery=True, nuvioRequery=True)
        if stage in ("direct_stream", "crash_survived", "timeout_survived", "stremio_requeried", "completed"):
            record.update(stremioIntact=True, stremioRows=2, stremioFingerprint="rows123",
                          registryFingerprint="registry123")
        if stage == "direct_stream":
            record.update(streamUrl="https://archive.org/download/public-domain/film.mp4",
                          title="Night of the Living Dead (1968)")
        if stage == "worker_started":
            crash = index == proof.STAGES.index("worker_started")
            record.update(fault="crash" if crash else "timeout", workerPid=200 if crash else 201,
                          syntheticFaultScript=True)
        if stage in ("crash_survived", "timeout_survived"):
            crash = stage == "crash_survived"
            record.update(workerPid=200 if crash else 201, syntheticFaultScript=True,
                          fault="crash" if crash else "timeout",
                          result="crashed" if crash else "deadline",
                          elapsedMs=100 if crash else 20050)
        result.append(record)
    return result


def logcat(data, pid=100):
    return "\n".join(f"11-07 10:00:00.000 {pid} 101 I Qt: {proof.MARKER}" + json.dumps(r)
                     for r in data)


CRASH_HOOK = "11-07 10:00:00.000 200 201 E ColosseumNuvioProof: native_abort_requested"
CRASH_TOMBSTONE = ("11-07 10:00:00.000 300 300 F DEBUG: pid: 200, tid: 201, name: nuvio0\n"
                   "11-07 10:00:00.000 300 300 F DEBUG: signal 6 (SIGABRT), code -1\n")


class ProofContractTests(unittest.TestCase):
    def test_complete_native_proof(self):
        data = proof.proof_records(logcat(records()), "fresh", 100)
        proof.validate_stages(data, 100, complete=True)

    def test_other_process_and_previous_launch_cannot_pass(self):
        self.assertEqual([], proof.proof_records(logcat(records(), 999), "fresh", 100))
        self.assertEqual([], proof.proof_records(logcat(records()), "new", 100))

    def test_failed_or_malformed_app_record_cannot_pass(self):
        data = records()
        data[0]["ok"] = False
        with self.assertRaises(proof.ProofFailure):
            proof.proof_records(logcat(data), "fresh", 100)
        with self.assertRaises(proof.ProofFailure):
            proof.proof_records("11-07 10:00:00.000 100 101 I Qt: " + proof.MARKER + "{bad}", "fresh", 100)

    def test_duplicate_out_of_order_and_incomplete_cannot_pass(self):
        for data in (records() + records()[-1:], records()[1:], records()[:-1]):
            with self.subTest(data=data), self.assertRaises(proof.ProofFailure):
                proof.validate_stages(data, 100, complete=True)

    def test_app_process_cannot_count_as_isolated_crash(self):
        data = records()
        data[proof.STAGES.index("crash_survived")]["workerPid"] = 100
        with self.assertRaises(proof.ProofFailure):
            proof.validate_stages(data, 100, complete=True)

    def test_each_stremio_invariant_is_checked(self):
        for stage_index in (proof.STAGES.index(stage) for stage in
                            ("direct_stream", "crash_survived", "timeout_survived", "stremio_requeried", "completed")):
            for field, value in (("stremioRows", 0), ("stremioIntact", False),
                                 ("stremioFingerprint", ""), ("registryFingerprint", "")):
                data = records()
                data[stage_index][field] = value
                with self.subTest(stage=stage_index, field=field), self.assertRaises(proof.ProofFailure):
                    proof.validate_stages(data, 100, complete=True)
        data = records()
        data[proof.STAGES.index("crash_survived")]["stremioFingerprint"] = "different rows"
        with self.assertRaises(proof.ProofFailure):
            proof.validate_stages(data, 100, complete=True)

    def test_caught_exception_or_short_timeout_does_not_prove_fault(self):
        for index, field, value in ((proof.STAGES.index("crash_survived"), "result", "exception"),
                                    (proof.STAGES.index("timeout_survived"), "elapsedMs", 100),
                                    (proof.STAGES.index("crash_survived"), "syntheticFaultScript", False)):
            data = records()
            data[index][field] = value
            with self.subTest(field=field), self.assertRaises(proof.ProofFailure):
                proof.validate_stages(data, 100, complete=True)

    def test_post_fault_query_and_fixture_provenance_are_explicit(self):
        for stage, field, value in (("direct_stream", "sourceKind", "silent fixture"),
                                     ("completed", "stremioRequery", False),
                                     ("completed", "nuvioRequery", False)):
            data = records()
            data[proof.STAGES.index(stage)][field] = value
            with self.subTest(stage=stage, field=field), self.assertRaises(proof.ProofFailure):
                proof.validate_stages(data, 100, complete=True)

    def test_no_physical_device_is_ever_mutated(self):
        completed = subprocess.CompletedProcess([], 0, "List of devices attached\nPHONE123\tdevice\n", "")
        with patch.object(proof.subprocess, "run", return_value=completed) as run:
            with self.assertRaises(proof.ProofFailure):
                proof.choose_emulator("adb", None)
            self.assertEqual(1, run.call_count)
            self.assertEqual(["adb", "devices"], run.call_args.args[0])

    def test_ambiguous_device_selection_fails(self):
        completed = subprocess.CompletedProcess([], 0,
            "List of devices attached\nemulator-5554\tdevice\nemulator-5556\tdevice\n", "")
        with patch.object(proof.subprocess, "run", return_value=completed):
            with self.assertRaises(proof.ProofFailure):
                proof.choose_emulator("adb", None)

    def test_remote_shell_arguments_are_quoted(self):
        completed = subprocess.CompletedProcess([], 0, "", "")
        with patch.object(proof.subprocess, "run", return_value=completed) as run:
            proof.Adb("adb", "emulator-5554").run("shell", "echo", "x&danger")
            self.assertEqual(run.call_args.args[0][-1], "echo 'x&danger'")

    def test_native_abort_needs_independent_matching_crash_evidence(self):
        proof.validate_native_crash(records(), CRASH_HOOK, CRASH_TOMBSTONE)
        for hook, tombstone in (("", CRASH_TOMBSTONE), (CRASH_HOOK, ""),
                                (CRASH_HOOK, CRASH_TOMBSTONE.replace("pid: 200", "pid: 999")),
                                (CRASH_HOOK.replace(" 200 ", " 999 "), CRASH_TOMBSTONE),
                                (CRASH_HOOK, CRASH_TOMBSTONE.replace("6 (SIGABRT)", "11 (SIGSEGV)"))):
            with self.subTest(hook=hook, tombstone=tombstone), self.assertRaises(proof.ProofFailure):
                proof.validate_native_crash(records(), hook, tombstone)

    def test_same_app_must_survive_post_completion_and_artifact_collection(self):
        class FakeAdb:
            command = ["adb", "-s", "emulator-5554"]

            def run(self, *args, binary=False, **kwargs):
                if args[:3] == ("shell", "am", "start"):
                    return "Status: ok\n"
                if "crash" in args:
                    return CRASH_TOMBSTONE
                if args[0] == "logcat":
                    return logcat(records()) + "\n" + CRASH_HOOK
                return b"screenshot" if binary else ""

        for stage, observed_pids in (
            ("during-proof", [100, None]),
            ("post-completion", [100, 100, 101]),
            ("artifact-collection", [100, 100] + [100] * 10 + [None]),
            ("success", [100] * 13),
        ):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as temporary:
                args = argparse.Namespace(artifacts=Path(temporary), adb="adb", serial=None,
                                          package="org.qtproject.example", activity="QtActivity",
                                          repository="https://example.org/repo", plugin="public-domain",
                                          media="10331", commit="abc123", timeout=240)
                with patch.object(proof, "choose_emulator", return_value=FakeAdb()), \
                     patch.object(proof, "current_pid", side_effect=observed_pids), \
                     patch.object(proof.secrets, "token_hex", return_value="fresh"), \
                     patch.object(proof.time, "sleep"):
                    code = proof.run_proof(args)
                result = json.loads((Path(temporary) / "proof-result.json").read_text())
                self.assertEqual(stage == "success", result["passed"])
                self.assertEqual(0 if stage == "success" else 1, code)


class ApkSizeTests(unittest.TestCase):
    def test_bytes_are_measured_from_real_zip_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            before, after = Path(temporary) / "before.apk", Path(temporary) / "after.apk"
            for path, payload in ((before, b"old"), (after, b"new-with-helper")):
                with zipfile.ZipFile(path, "w") as archive:
                    archive.writestr("lib/x86_64/libcolosseum.so", payload)
            row = sizes.compare(before, after, "x86_64")
            self.assertEqual(after.stat().st_size - before.stat().st_size, row["apk_delta_bytes"])
            self.assertEqual(12, row["native_delta_bytes"])
            self.assertNotEqual(row["baseline"]["sha256"], row["nuvio"]["sha256"])
            with self.assertRaises(ValueError):
                sizes.inspect_apk(before, "arm64-v8a")


if __name__ == "__main__":
    unittest.main()
