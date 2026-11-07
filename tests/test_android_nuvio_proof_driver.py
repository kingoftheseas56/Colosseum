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
    result, current_fault, profile_count, profile_workers = [], "crash", 0, []
    for stage in proof.STAGES:
        record = {"stage": stage, "nonce": "fresh", "appPid": 100, "ok": True}
        if stage == "timeout_started":
            current_fault = "timeout"
        crash = current_fault == "crash"
        if stage == "started":
            record.update(tmdbAuthorizationSelfTest=True,
                          tmdbAuthorizationSelfTestKind="synthetic_no_network")
        if stage in ("stremio_fixture_installed", "direct_stream", "crash_recovered", "timeout_recovered", "stremio_requeried", "completed"):
            record.update(sourceKind="live_https_controlled_fixture")
        if stage in ("stremio_requeried", "completed"):
            record.update(stremioRequery=True, nuvioRequery=True)
        if stage in ("direct_stream", "crash_survived", "crash_recovered", "timeout_survived", "timeout_recovered", "stremio_requeried", "completed"):
            record.update(stremioIntact=True, stremioRows=2, stremioFingerprint="rows123",
                          registryFingerprint="registry123")
        if stage in ("direct_stream", "crash_recovered", "timeout_recovered"):
            record.update(streamUrl="https://archive.org/download/public-domain/film.mp4",
                          title="Night of the Living Dead (1968)")
        if stage == "worker_started":
            record.update(fault=current_fault, workerPid=200 if crash else 201,
                          syntheticFaultScript=True)
        if stage in ("crash_survived", "timeout_survived"):
            record.update(workerPid=200 if crash else 201, syntheticFaultScript=True,
                          fault=current_fault, result="crashed" if crash else "deadline",
                          elapsedMs=100 if crash else 20050)
        if stage in ("recovery_started", "recovery_worker_started", "crash_recovered", "timeout_recovered"):
            record.update(fault=current_fault, failedWorkerPid=200 if crash else 201)
            if stage != "recovery_started":
                record.update(workerPid=210 if crash else 211, slot=0, requestMonotonicMs=1234)
            if stage.endswith("_recovered"):
                record.update(result="ok", directRows=1)
        if stage.startswith("profile_"):
            if stage == "profile_baseline":
                profile_count = 1 if profile_count == 0 else 4
                profile_workers = []
                record.update(holdMs=4000, syntheticWorkload=True)
            record.update(concurrency=profile_count)
            if stage == "profile_started":
                record.update(holdMs=16000, syntheticWorkload=True, workload="idle_initialized_quickjs_timer")
            if stage == "profile_worker_started":
                slot = len(profile_workers)
                record.update(workerPid=300 + profile_count * 10 + slot, slot=slot, requestMonotonicMs=1234)
                profile_workers.append(record)
            if stage == "profile_finished":
                record.update(workerPids=[w["workerPid"] for w in profile_workers], slots=[w["slot"] for w in profile_workers],
                              startedCount=profile_count, finishedCount=profile_count, maxConcurrentWorkers=profile_count,
                              result="ok", idleHoldMs=4000, syntheticWorkload=True)
        if stage == "completed":
            record.update(profilesCompleted=[1, 4], recoveredFaults=["crash", "timeout"])
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
                            ("direct_stream", "crash_survived", "crash_recovered", "timeout_survived", "timeout_recovered", "stremio_requeried", "completed")):
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

    def test_native_tmdb_boundary_check_is_required(self):
        for field, value in (("tmdbAuthorizationSelfTest", False),
                              ("tmdbAuthorizationSelfTestKind", "unchecked")):
            data = records()
            data[0][field] = value
            with self.subTest(field=field), self.assertRaises(proof.ProofFailure):
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
                     patch.object(proof.time, "sleep"), \
                     patch.object(proof, "collect_resource_evidence"), \
                     patch.object(proof, "validate_resource_completion"):
                    code = proof.run_proof(args)
                result = json.loads((Path(temporary) / "proof-result.json").read_text())
                self.assertEqual(stage == "success", result["passed"])
                self.assertEqual(0 if stage == "success" else 1, code)


class ApkLaunchIdentityTests(unittest.TestCase):
    def test_reads_actual_qt_package_and_activity_from_apk(self):
        text = "package: name='org.qtproject.example.colosseum' versionCode='1'\nlaunchable-activity: name='org.qtproject.qt.android.bindings.QtActivity' label=''\n"
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "proof.apk"
            apk.write_bytes(b"fixture")
            completed = subprocess.CompletedProcess([], 0, text, "")
            with patch.object(proof.subprocess, "run", return_value=completed) as run:
                self.assertEqual(("org.qtproject.example.colosseum", "org.qtproject.qt.android.bindings.QtActivity"),
                                 proof.apk_launch_component(apk, "/sdk/aapt"))
                self.assertEqual(["/sdk/aapt", "dump", "badging", str(apk)], run.call_args.args[0])

    def test_missing_ambiguous_or_invalid_components_fail_closed(self):
        examples = ("package: name='org.example.app'\n",
                    "package: name='org.example.app'\npackage: name='org.other.app'\nlaunchable-activity: name='.Main'\n",
                    "package: name='../../invalid'\nlaunchable-activity: name='.Main'\n")
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "proof.apk"
            apk.write_bytes(b"fixture")
            for text in examples:
                with self.subTest(text=text), patch.object(proof.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, text, "")), self.assertRaises(proof.ProofFailure):
                    proof.apk_launch_component(apk, "/sdk/aapt")
            with patch.object(proof.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "", "invalid APK")), self.assertRaises(proof.ProofFailure):
                proof.apk_launch_component(apk, "/sdk/aapt")

    def test_relative_activity_is_qualified_with_the_actual_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "proof.apk"
            apk.write_bytes(b"fixture")
            text = "package: name='org.example.app'\nlaunchable-activity: name='.Main'\n"
            with patch.object(proof.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, text, "")):
                self.assertEqual(("org.example.app", "org.example.app.Main"), proof.apk_launch_component(apk, "/sdk/aapt"))

    def test_workflow_supplies_the_same_checksum_verified_apk_for_identity(self):
        workflow = (ROOT / ".github/workflows/android.yml").read_text()
        self.assertIn("nuvio_emulator_proof.py --apk apk/colosseum-x86_64-debug.apk", workflow)
        self.assertIn("cd apk && sha256sum -c colosseum-x86_64-debug.apk.sha256", workflow)


class ResourceEvidenceTests(unittest.TestCase):
    package = "org.qtproject.example"

    def meminfo(self, pid, name, pss=12345, rss=23456):
        return f"** MEMINFO in pid {pid} [{name}] **\n App Summary\n TOTAL PSS: {pss} TOTAL RSS: {rss} TOTAL SWAP PSS: 0\n"

    def ready_log(self, pid, milliseconds=1500):
        return f'11-07 10:00:00.000 {pid} 11 I ColosseumNuvioProof: NUVIO_ANDROID_RUNTIME_READY={{"monotonicMs":{milliseconds},"workerPid":{pid}}}'

    def test_meminfo_identity_and_positive_pss_rss_required(self):
        text = self.meminfo(310, self.package + ":nuvio0")
        self.assertEqual(12345, proof.parse_meminfo(text, 310, self.package + ":nuvio0")["pssKiB"])
        for modified in (text.replace("pid 310", "pid 311"), text.replace("nuvio0", "nuvio1"),
                         text.replace("TOTAL RSS:", "NO RSS:"), self.meminfo(310, self.package + ":nuvio0", 0),
                         self.meminfo(310, self.package + ":nuvio0", 200, 0)):
            with self.subTest(text=modified), self.assertRaises(proof.ProofFailure):
                proof.parse_meminfo(modified, 310, self.package + ":nuvio0")

    def test_ready_timestamp_is_bound_to_worker_log_pid(self):
        self.assertEqual({310: 1500}, proof.runtime_ready_times(self.ready_log(310)))
        self.assertEqual({}, proof.runtime_ready_times(self.ready_log(310).replace("ColosseumNuvioProof", "UntrustedTag")))
        for text in (self.ready_log(310) + "\n" + self.ready_log(310), self.ready_log(310, -1),
                     self.ready_log(310).replace("1500", "true"), self.ready_log(310).replace('"workerPid":310', '"workerPid":311'), self.ready_log(310).replace("1500", "broken")):
            with self.subTest(text=text), self.assertRaises(proof.ProofFailure):
                proof.runtime_ready_times(text)

    def test_process_list_requires_exact_service_names(self):
        text = "PID NAME\n100 org.qtproject.example\n310 org.qtproject.example:nuvio0\n320 foreign:nuvio1\n"
        self.assertEqual({0: 310}, proof.parse_service_processes(text, self.package))
        with self.assertRaises(proof.ProofFailure):
            proof.parse_service_processes(text + "311 org.qtproject.example:nuvio0\n", self.package)

    def test_active_memory_sample_confirms_all_workers_before_and_after(self):
        owner = self
        workers = [{"workerPid": 310, "slot": 0, "requestMonotonicMs": 1200}]
        class FakeAdb:
            def __init__(self, remove=False):
                self.process_calls, self.remove = 0, remove
            def run(self, *args):
                if args[1] == "ps":
                    self.process_calls += 1
                    if self.remove and self.process_calls > 1:
                        return "PID NAME\n"
                    return "PID NAME\n310 org.qtproject.example:nuvio0\n"
                pid = int(args[-1])
                return owner.meminfo(pid, owner.package + (":nuvio0" if pid == 310 else ""))
        for remove in (False, True):
            with self.subTest(remove=remove), tempfile.TemporaryDirectory() as tmp:
                if remove:
                    with self.assertRaises(proof.ProofFailure):
                        proof.sample_active_memory(FakeAdb(remove), Path(tmp), self.package, 100, workers, {310: 1500})
                else:
                    result = proof.sample_active_memory(FakeAdb(), Path(tmp), self.package, 100, workers, {310: 1500})
                    self.assertEqual(300, result["workers"][0]["coldStartMs"])
                    self.assertEqual(12345, result["totalWorkerPssKiB"])
                    self.assertTrue((Path(tmp) / "slot-0-meminfo.txt").is_file())

    def test_wrong_or_missing_ready_clock_cannot_measure_startup(self):
        workers = [{"workerPid": 310, "slot": 0, "requestMonotonicMs": 1200}]
        class FakeAdb:
            def run(self, *args):
                return "PID NAME\n310 org.qtproject.example:nuvio0\n"
        for ready in ({}, {310: 1199}, {310: 999999}):
            with self.subTest(ready=ready), tempfile.TemporaryDirectory() as tmp, self.assertRaises(proof.ProofFailure):
                proof.sample_active_memory(FakeAdb(), Path(tmp), self.package, 100, workers, ready)

    def test_idle_snapshot_cannot_overlap_lazy_launch(self):
        owner = self
        class FakeAdb:
            calls = 0
            def run(self, *args):
                if args[1] == "ps":
                    self.calls += 1
                    return "PID NAME\n" if self.calls == 1 else "PID NAME\n310 org.qtproject.example:nuvio0\n"
                return owner.meminfo(100, owner.package)
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(proof.ProofFailure):
            proof.sample_idle_memory(FakeAdb(), Path(tmp), self.package, 100)

    def test_lingering_idle_service_is_a_failure(self):
        class FakeAdb:
            def run(self, *args):
                return "PID NAME\n310 org.qtproject.example:nuvio0\n"
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(proof.ProofFailure):
            proof.sample_idle_memory(FakeAdb(), Path(tmp), self.package, 100)
        with tempfile.TemporaryDirectory() as tmp, patch.object(proof, "current_pid", return_value=100), \
             patch.object(proof.time, "monotonic", side_effect=[0, 4]), self.assertRaises(proof.ProofFailure):
            proof.wait_for_disposal(FakeAdb(), Path(tmp), self.package, 100)

    def test_incremental_profiles_capture_raw_evidence_and_human_report(self):
        owner = self
        class FakeAdb:
            active = {}
            def run(self, *args):
                if args[1] == "ps":
                    return "PID NAME\n" + "".join(f"{pid} {owner.package}:nuvio{slot}\n" for slot, pid in self.active.items())
                pid = int(args[-1])
                name = owner.package
                if pid != 100:
                    name += ":nuvio" + str(next(slot for slot, worker in self.active.items() if worker == pid))
                return owner.meminfo(pid, name)
        data, evidence, adb = records(), {}, FakeAdb()
        with tempfile.TemporaryDirectory() as tmp, patch.object(proof, "current_pid", return_value=100):
            artifact = Path(tmp)
            for count in (1, 4):
                indices = [i for i, r in enumerate(data) if r.get("concurrency") == count]
                baseline, finished = indices[0], indices[-1]
                workers = [r for r in data[baseline:finished] if r["stage"] == "profile_worker_started"]
                proof.collect_resource_evidence(adb, artifact, self.package, 100, data[:baseline + 1], "", evidence)
                adb.active = {r["slot"]: r["workerPid"] for r in workers}
                ready = "\n".join(self.ready_log(r["workerPid"]) for r in workers)
                proof.collect_resource_evidence(adb, artifact, self.package, 100, data[:finished], ready, evidence)
                adb.active = {}
                proof.collect_resource_evidence(adb, artifact, self.package, 100, data[:finished + 1], ready, evidence)
            proof.validate_resource_completion(evidence)
            self.assertEqual(4 * 12345, evidence["4"]["active"]["totalWorkerPssKiB"])
            self.assertEqual(266, evidence["4"]["active"]["workers"][0]["coldStartMs"])
            self.assertIn("double-counts", (artifact / "resource-profile.md").read_text())
            self.assertEqual(2, len(json.loads((artifact / "resource-measurements.json").read_text())["profiles"]))

    def test_missing_independent_measurements_cannot_complete(self):
        for evidence in ({}, {"1": {"baseline": {}, "active": {}, "after": {}}},
                         {"1": {"baseline": {}, "active": {}, "after": {}}, "4": {"baseline": {}, "active": {}}}):
            with self.subTest(evidence=evidence), self.assertRaises(proof.ProofFailure):
                proof.validate_resource_completion(evidence)

    def test_missed_active_measurement_is_not_reconstructed_from_receipt(self):
        data = records()
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(proof.ProofFailure):
            proof.collect_resource_evidence(None, Path(tmp), self.package, 100, data, "", {"1": {"baseline": {}}})

    def test_recovery_requires_new_pid_matching_successful_http_result(self):
        for stage, field, value in (("recovery_worker_started", "workerPid", 200),
                                     ("crash_recovered", "workerPid", 999),
                                     ("crash_recovered", "directRows", 0),
                                     ("crash_recovered", "streamUrl", "magnet:invalid"),
                                     ("crash_recovered", "streamUrl", "https://example.org/other.mp4"),
                                     ("timeout_recovered", "result", "deadline"),
                                     ("completed", "recoveredFaults", ["timeout"])):
            data = records()
            data[proof.STAGES.index(stage)][field] = value
            with self.subTest(stage=stage, field=field), self.assertRaises(proof.ProofFailure):
                proof.validate_stages(data, 100, complete=True)

    def test_profile_requires_concurrent_distinct_slots_and_exact_completions(self):
        for stage, field, value in (("profile_started", "holdMs", 1000),
                                     ("profile_started", "syntheticWorkload", False),
                                     ("profile_worker_started", "workerPid", 100),
                                     ("profile_worker_started", "requestMonotonicMs", -1),
                                     ("profile_finished", "maxConcurrentWorkers", 0),
                                     ("profile_finished", "workerPids", [999]),
                                     ("completed", "profilesCompleted", [1])):
            data = records()
            data[proof.STAGES.index(stage)][field] = value
            with self.subTest(stage=stage, field=field), self.assertRaises(proof.ProofFailure):
                proof.validate_stages(data, 100, complete=True)
        data = records()
        workers = [r for r in data if r["stage"] == "profile_worker_started" and r["concurrency"] == 4]
        workers[-1]["slot"] = workers[0]["slot"]
        with self.assertRaises(proof.ProofFailure):
            proof.validate_stages(data, 100, complete=True)


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
