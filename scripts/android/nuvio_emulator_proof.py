#!/usr/bin/env python3
"""Run the opt-in Android Nuvio proof on an already installed emulator APK.

Refuses physical devices. The production smoke runs first to handle real local
onboarding. This driver then performs one intentional new launch and requires
the same application PID throughout repository, stream and isolated-fault work.
Every native proof record is bound to that PID and a fresh random launch nonce.
No credentials are supplied to the intent, JavaScript, or driver.
"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import time
from urllib.parse import urlsplit

MARKER = "NUVIO_ANDROID_PROOF="
STAGES = ("started", "repo_added", "plugin_enabled", "stremio_fixture_installed", "direct_stream",
          "crash_started", "worker_started", "crash_survived",
          "recovery_started", "recovery_worker_started", "crash_recovered",
          "timeout_started", "worker_started", "timeout_survived",
          "recovery_started", "recovery_worker_started", "timeout_recovered",
          "stremio_requery_started", "stremio_requeried",
          "profile_baseline", "profile_started", "profile_worker_started", "profile_finished",
          "profile_baseline", "profile_started", "profile_worker_started", "profile_worker_started",
          "profile_worker_started", "profile_worker_started", "profile_finished", "completed")

# adb logcat -v threadtime: month-day time PID TID priority tag: message
LOG_LINE = re.compile(r"^\s*\d\d-\d\d\s+\S+\s+(\d+)\s+\d+\s+[VDIWEFA]\s+.*?" +
                      re.escape(MARKER) + r"(\{.*\})\s*$")


class ProofFailure(RuntimeError):
    pass


def proof_records(logcat: str, nonce: str, app_pid: int) -> list[dict]:
    """Only the continuously observed application can attest to its proof."""
    result = []
    for line in logcat.splitlines():
        if MARKER not in line:
            continue
        match = LOG_LINE.match(line)
        if not match or int(match[1]) != app_pid:
            continue
        try:
            record = json.loads(match[2])
        except json.JSONDecodeError as exc:
            raise ProofFailure("Malformed proof JSON from application") from exc
        if record.get("nonce") != nonce:
            continue
        if record.get("appPid") != app_pid:
            raise ProofFailure("Proof record appPid differs from logcat PID")
        if record.get("ok") is not True:
            raise ProofFailure(f"Native proof failed at {record.get('stage', 'unknown')}: "
                               f"{record.get('error', record.get('reason', 'no detail'))}")
        result.append(record)
    return result


def validate_stages(records: list[dict], app_pid: int, *, complete: bool = False) -> None:
    seen = [record.get("stage") for record in records]
    if seen != list(STAGES[:len(seen)]):
        raise ProofFailure(f"Duplicate, unexpected or out-of-order stages: {seen}")
    baseline = next((r for r in records if r["stage"] == "direct_stream"), None)
    workers, recoveries, profiles = {}, {}, {}
    for record in records:
        stage = record["stage"]
        if stage == "started":
            if record.get("tmdbAuthorizationSelfTest") is not True or record.get("tmdbAuthorizationSelfTestKind") != "synthetic_no_network":
                raise ProofFailure("Native synthetic TMDB authorization boundary check was not proven")
        if stage == "direct_stream":
            parsed = urlsplit(record.get("streamUrl", ""))
            if parsed.scheme not in ("http", "https") or not parsed.hostname:
                raise ProofFailure("Direct stream is not an HTTP(S) URL")
            if not record.get("title"):
                raise ProofFailure("Direct stream has no public-domain title")
        if stage in ("stremio_fixture_installed", "direct_stream", "crash_recovered", "timeout_recovered", "stremio_requeried", "completed"):
            if record.get("sourceKind") != "live_https_controlled_fixture":
                raise ProofFailure("Controlled live-HTTPS fixture provenance is missing")
        if stage in ("stremio_requeried", "completed"):
            if record.get("stremioRequery") is not True or record.get("nuvioRequery") is not True:
                raise ProofFailure("Fresh Stremio and Nuvio queries after both faults were not proven")
        if stage in ("direct_stream", "crash_survived", "crash_recovered", "timeout_survived", "timeout_recovered", "stremio_requeried", "completed"):
            if record.get("stremioIntact") is not True or type(record.get("stremioRows")) is not int or record["stremioRows"] <= 0:
                raise ProofFailure(f"Stremio sources not proven present at {stage}")
            for field in ("stremioFingerprint", "registryFingerprint"):
                if not record.get(field) or not baseline or record[field] != baseline.get(field):
                    raise ProofFailure(f"{field} changed or is missing at {stage}")
        if stage == "worker_started":
            fault = record.get("fault")
            worker_pid = record.get("workerPid")
            if fault not in ("crash", "timeout") or fault in workers:
                raise ProofFailure("Unexpected or duplicate worker fault invocation")
            if type(worker_pid) is not int or worker_pid <= 0 or worker_pid == app_pid:
                raise ProofFailure("Fault worker must be a separate process")
            workers[fault] = worker_pid
        if stage in ("crash_survived", "timeout_survived"):
            worker_pid = record.get("workerPid")
            if type(worker_pid) is not int or worker_pid <= 0 or worker_pid == app_pid:
                raise ProofFailure(f"{stage} did not identify a separate worker process")
            expected = ("crash", "crashed") if stage == "crash_survived" else ("timeout", "deadline")
            if workers.get(expected[0]) != worker_pid:
                raise ProofFailure("Fault worker PID changed between start and result")
            if (record.get("fault"), record.get("result")) != expected:
                raise ProofFailure(f"Wrong isolated-worker fault outcome at {stage}")
            if record.get("syntheticFaultScript") is not True:
                raise ProofFailure("Fault injection provenance must be explicit")
            elapsed = record.get("elapsedMs")
            if type(elapsed) not in (int, float) or elapsed < 0:
                raise ProofFailure("Missing measured fault duration")
            if stage == "timeout_survived" and elapsed < 15000:
                raise ProofFailure("Timeout did not exercise the production worker deadline")
        if stage in ("recovery_started", "recovery_worker_started", "crash_recovered", "timeout_recovered"):
            fault = record.get("fault")
            if fault not in workers or record.get("failedWorkerPid") != workers[fault]:
                raise ProofFailure("Recovery does not identify the preceding failed worker")
            if stage != "recovery_started":
                pid, slot = record.get("workerPid"), record.get("slot")
                if type(pid) is not int or pid <= 0 or pid == app_pid or pid in workers.values():
                    raise ProofFailure("Recovery must use a fresh service process after the fault")
                if type(slot) is not int or not 0 <= slot <= 3:
                    raise ProofFailure("Recovery slot identity is missing")
                if stage == "recovery_worker_started":
                    if fault in recoveries or pid in [r["workerPid"] for r in recoveries.values()]:
                        raise ProofFailure("Duplicate recovery invocation or reused recovery process")
                    if type(record.get("requestMonotonicMs")) is not int or record["requestMonotonicMs"] < 0:
                        raise ProofFailure("Recovery launch timing is missing")
                    recoveries[fault] = record
                else:
                    if stage != fault + "_recovered" or fault not in recoveries or recoveries[fault]["workerPid"] != pid or recoveries[fault]["slot"] != slot:
                        raise ProofFailure("Recovery result does not match its started process")
                    url = urlsplit(record.get("streamUrl", ""))
                    if record.get("result") != "ok" or type(record.get("directRows")) is not int or record["directRows"] <= 0 or url.scheme not in ("http", "https") or not url.hostname or not baseline or record.get("streamUrl") != baseline.get("streamUrl"):
                        raise ProofFailure("Next plugin call did not return a direct HTTP stream successfully")
        if stage.startswith("profile_"):
            count = record.get("concurrency")
            if count not in (1, 4) or type(count) is not int:
                raise ProofFailure("Resource profile must measure exactly 1 or 4 slots")
            if stage == "profile_baseline":
                if count in profiles or record.get("syntheticWorkload") is not True or record.get("holdMs") != 4000:
                    raise ProofFailure("Duplicate or undisclosed synthetic resource profile")
                profiles[count] = []
            elif count not in profiles:
                raise ProofFailure("Resource profile has no idle baseline")
            elif stage == "profile_started":
                if record.get("syntheticWorkload") is not True or record.get("workload") != "idle_initialized_quickjs_timer" or record.get("holdMs") != 16000:
                    raise ProofFailure("Wrong resource measurement workload or hold time")
            elif stage == "profile_worker_started":
                pid, slot, started = record.get("workerPid"), record.get("slot"), record.get("requestMonotonicMs")
                previous = [w for group in profiles.values() for w in group]
                if type(pid) is not int or pid <= 0 or pid == app_pid or pid in [w["workerPid"] for w in previous]:
                    raise ProofFailure("Resource workers must have distinct process identities")
                if type(slot) is not int or not 0 <= slot <= 3 or slot in [w["slot"] for w in profiles[count]]:
                    raise ProofFailure("Resource workers must occupy distinct service slots")
                if type(started) is not int or started < 0:
                    raise ProofFailure("Resource process-request timing is missing")
                profiles[count].append(record)
            elif stage == "profile_finished":
                group = profiles[count]
                if len(group) != count or record.get("result") != "ok" or record.get("syntheticWorkload") is not True or record.get("idleHoldMs") != 4000 or any(type(record.get(f)) is not int or record[f] != count for f in ("startedCount", "finishedCount", "maxConcurrentWorkers")):
                    raise ProofFailure("Resource profile did not complete all simultaneous workers")
                if sorted(record.get("workerPids", [])) != sorted(w["workerPid"] for w in group) or sorted(record.get("slots", [])) != sorted(w["slot"] for w in group):
                    raise ProofFailure("Resource profile completion identities changed")
        if stage == "completed":
            if record.get("profilesCompleted") != [1, 4] or record.get("recoveredFaults") != ["crash", "timeout"]:
                raise ProofFailure("Expanded fault recovery and resource profiles are incomplete")
    if complete and seen != list(STAGES):
        raise ProofFailure(f"Incomplete proof: {seen}")


def validate_native_crash(records: list[dict], logcat: str, crash_logcat: str) -> None:
    """Distinguish the intentional native crash from a loader/IPC failure."""
    crash = next(record for record in records if record["stage"] == "crash_survived")
    worker_pid = crash["workerPid"]
    hook = re.compile(r"^\s*\d\d-\d\d\s+\S+\s+" + str(worker_pid) +
                      r"\s+\d+\s+[VDIWEFA]\s+ColosseumNuvioProof\s*:.*native_abort_requested", re.M)
    if not hook.search(logcat):
        raise ProofFailure("Native crash hook was not observed in the reported worker process")
    # debuggerd's PID is different from the crashing worker. Its independent
    # crash-buffer tombstone names the subject PID, then the fatal signal.
    lines = crash_logcat.splitlines()
    subject = re.compile(r"\bpid:\s*" + str(worker_pid) + r"\s*,\s*tid:")
    for index, line in enumerate(lines):
        if subject.search(line) and re.search(r"\bsignal\s+6\s+\(SIGABRT\)",
                                               "\n".join(lines[index:index + 12])):
            return
    raise ProofFailure("Independent crash buffer has no SIGABRT tombstone for the reported worker")


def parse_service_processes(text: str, package: str) -> dict[int, int]:
    """Parse adb's untruncated `ps -A -o PID,NAME`, not a PID-only attestation."""
    result = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) != 2 or not fields[0].isdigit():
            continue
        match = re.fullmatch(re.escape(package) + r":nuvio([0-3])", fields[1])
        if not match:
            continue
        slot, pid = int(match[1]), int(fields[0])
        if slot in result or pid <= 0 or pid in result.values():
            raise ProofFailure("Ambiguous Nuvio service process identity")
        result[slot] = pid
    return result


def parse_meminfo(text: str, pid: int, process_name: str) -> dict:
    """Require API 34's PID-bound memory summary. Android kB means KiB here."""
    identity = re.compile(r"\*\* MEMINFO in pid " + str(pid) + r" \[" +
                          re.escape(process_name) + r"\] \*\*")
    pss = re.search(r"\bTOTAL PSS:\s*(\d+)\b", text)
    rss = re.search(r"\bTOTAL RSS:\s*(\d+)\b", text)
    if not identity.search(text) or not pss or not rss:
        raise ProofFailure(f"Missing PID-bound PSS/RSS memory summary for {process_name}")
    values = {"pid": pid, "process": process_name, "pssKiB": int(pss[1]), "rssKiB": int(rss[1])}
    if values["pssKiB"] <= 0 or values["rssKiB"] <= 0:
        raise ProofFailure(f"Invalid PSS/RSS measurements for {process_name}")
    return values


def runtime_ready_times(logcat: str) -> dict[int, int]:
    """Use independent worker log PIDs and CLOCK_MONOTONIC time, never app claims."""
    marker = "NUVIO_ANDROID_RUNTIME_READY="
    pattern = re.compile(r"^\s*\d\d-\d\d\s+\S+\s+(\d+)\s+\d+\s+[VDIWEFA]\s+"
                         r"ColosseumNuvioProof\s*:.*?" + re.escape(marker) + r"(\{.*\})\s*$")
    result = {}
    for line in logcat.splitlines():
        if marker not in line:
            continue
        match = pattern.match(line)
        if not match:
            continue
        try:
            record = json.loads(match[2])
        except json.JSONDecodeError as exc:
            raise ProofFailure("Malformed native runtime-ready marker") from exc
        value = record.get("monotonicMs")
        if type(value) is not int or value < 0 or record.get("workerPid") != int(match[1]) or int(match[1]) in result:
            raise ProofFailure("Invalid or duplicate native runtime-ready marker")
        result[int(match[1])] = value
    return result


def sample_process_memory(adb, directory: Path, pid: int, name: str, label: str) -> dict:
    text = adb.run("shell", "dumpsys", "meminfo", str(pid))
    (directory / f"{label}-meminfo.txt").write_text(text)
    return parse_meminfo(text, pid, name)


def sample_idle_memory(adb, directory: Path, package: str, app_pid: int) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    text = adb.run("shell", "ps", "-A", "-o", "PID,NAME")
    (directory / "processes.txt").write_text(text)
    if parse_service_processes(text, package):
        raise ProofFailure("Idle resource sample still has a live Nuvio service process")
    memory = sample_process_memory(adb, directory, app_pid, package, "app")
    after = adb.run("shell", "ps", "-A", "-o", "PID,NAME")
    (directory / "processes-after.txt").write_text(after)
    if parse_service_processes(after, package):
        raise ProofFailure("Idle memory sample overlapped a new Nuvio service launch")
    return memory


def sample_active_memory(adb, directory: Path, package: str, app_pid: int,
                         workers: list[dict], ready: dict[int, int]) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    expected = {worker["slot"]: worker["workerPid"] for worker in workers}
    if len(expected) != len(workers) or len(set(expected.values())) != len(workers):
        raise ProofFailure("Resource sample has duplicate slots or worker PIDs")
    before = adb.run("shell", "ps", "-A", "-o", "PID,NAME")
    (directory / "processes-before.txt").write_text(before)
    if parse_service_processes(before, package) != expected:
        raise ProofFailure("Live service processes do not match the resource profile")
    measured = []
    for worker in workers:
        slot, pid = worker["slot"], worker["workerPid"]
        started = worker["requestMonotonicMs"]
        if pid not in ready or not 0 <= ready[pid] - started <= 22000:
            raise ProofFailure("Runtime-ready timing is absent or predates its process request")
        row = sample_process_memory(adb, directory, pid, f"{package}:nuvio{slot}", f"slot-{slot}")
        row.update(slot=slot, requestMonotonicMs=started, runtimeReadyMonotonicMs=ready[pid],
                   coldStartMs=ready[pid] - started)
        measured.append(row)
    app = sample_process_memory(adb, directory, app_pid, package, "app")
    after = adb.run("shell", "ps", "-A", "-o", "PID,NAME")
    (directory / "processes-after.txt").write_text(after)
    if parse_service_processes(after, package) != expected:
        raise ProofFailure("A measured service exited or was replaced during memory collection")
    return {"workers": measured, "app": app,
            "totalWorkerPssKiB": sum(row["pssKiB"] for row in measured),
            "totalWorkerRssKiB": sum(row["rssKiB"] for row in measured)}


def wait_for_disposal(adb, directory: Path, package: str, app_pid: int) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + 3
    while True:
        if current_pid(adb, package) != app_pid:
            raise ProofFailure("Application died while waiting for idle service disposal")
        text = adb.run("shell", "ps", "-A", "-o", "PID,NAME")
        with (directory / "disposal-process-history.txt").open("a") as out:
            out.write(text + "\n")
        if not parse_service_processes(text, package):
            return sample_idle_memory(adb, directory, package, app_pid)
        if time.monotonic() >= deadline:
            raise ProofFailure("Finished Nuvio services were not stopped when idle")
        time.sleep(0.1)


def collect_resource_evidence(adb, artifact: Path, package: str, app_pid: int,
                              records: list[dict], logcat: str, evidence: dict) -> None:
    ready = runtime_ready_times(logcat)
    for count in (1, 4):
        group = [r for r in records if r.get("concurrency") == count and r["stage"].startswith("profile_")]
        if not group:
            continue
        row = evidence.setdefault(str(count), {})
        directory = artifact / "resources" / f"{count}-slot"
        if "baseline" not in row:
            # Once workers have started, an idle baseline cannot be reconstructed.
            if any(r["stage"] != "profile_baseline" for r in group):
                raise ProofFailure("Missed the real idle memory baseline before service launch")
            row["baseline"] = sample_idle_memory(adb, directory / "before", package, app_pid)
        workers = [r for r in group if r["stage"] == "profile_worker_started"]
        finished = any(r["stage"] == "profile_finished" for r in group)
        if "active" not in row and finished:
            raise ProofFailure("Resource workers finished before independent active memory sampling")
        if "active" not in row and len(workers) == count and all(r["workerPid"] in ready for r in workers):
            row["active"] = sample_active_memory(adb, directory / "active", package, app_pid, workers, ready)
        if finished and "after" not in row:
            row["after"] = wait_for_disposal(adb, directory / "after", package, app_pid)
        (artifact / "resource-measurements.json").write_text(json.dumps({
            "method": "Single dumpsys meminfo PSS/RSS snapshot per initialized idle service; KiB. Request-to-QuickJS-ready process-cold timings use CLOCK_MONOTONIC. OS/filesystem caches are not cold. Not peak plugin or OnePlus memory.",
            "profiles": evidence}, indent=2) + "\n")
        write_resource_report(artifact, evidence)


def write_resource_report(artifact: Path, evidence: dict) -> None:
    lines = ["# Android Nuvio service resource profile", "",
             "Single-run proof-enabled APK on an API 34 x86_64 emulator, not OnePlus measurements. Each service is a fresh process; OS/filesystem caches may be warm. Cold-start is process request to initialized QuickJS/bootstrap, before plugin module evaluation. Memory is one `dumpsys meminfo` snapshot during a synthetic idle timer, not peak arbitrary-plugin memory. PSS apportions shared pages; summed RSS double-counts them. All memory units are KiB.", "",
             "| Concurrent slots | Slot | PID | Cold start (ms) | PSS (KiB) | RSS (KiB) |",
             "|---:|---:|---:|---:|---:|---:|"]
    for count in ("1", "4"):
        row = evidence.get(count, {})
        for worker in row.get("active", {}).get("workers", []):
            lines.append(f"| {count} | {worker['slot']} | {worker['pid']} | {worker['coldStartMs']} | {worker['pssKiB']} | {worker['rssKiB']} |")
    lines += ["", "| Concurrent slots | Worker total PSS | Worker total RSS | App idle-before PSS/RSS | App active PSS/RSS | App idle-after PSS/RSS |",
              "|---:|---:|---:|---:|---:|---:|"]
    def pair(value):
        return f"{value['pssKiB']} / {value['rssKiB']}" if value else "pending"
    for count in ("1", "4"):
        row = evidence.get(count, {})
        active = row.get("active", {})
        lines.append(f"| {count} | {active.get('totalWorkerPssKiB', 'pending')} | {active.get('totalWorkerRssKiB', 'pending')} | {pair(row.get('baseline'))} | {pair(active.get('app'))} | {pair(row.get('after'))} |")
    lines += ["", "The raw PID-bound meminfo and before/after process lists are in `resources/`. A successful proof requires exactly the measured worker set throughout sampling and no Nuvio service processes at either idle boundary. Android slot count remains four until the user decides from these measurements.", ""]
    (artifact / "resource-profile.md").write_text("\n".join(lines))


def validate_resource_completion(evidence: dict) -> None:
    if set(evidence) != {"1", "4"} or any(set(evidence[count]) != {"baseline", "active", "after"} for count in evidence):
        raise ProofFailure("Independent 1-slot and 4-slot resource/disposal evidence is incomplete")


class Adb:
    def __init__(self, binary: str, serial: str):
        self.command = [binary, "-s", serial]

    def run(self, *arguments: str, check: bool = True, binary: bool = False):
        forwarded = list(arguments)
        if forwarded and forwarded[0] == "shell":
            forwarded = ["shell", " ".join(shlex.quote(argument) for argument in forwarded[1:])]
        result = subprocess.run(self.command + forwarded, capture_output=True,
                                text=not binary, timeout=45)
        if check and result.returncode:
            detail = result.stderr if not binary else result.stderr.decode(errors="replace")
            raise ProofFailure(f"adb {' '.join(arguments[:3])} failed: {detail.strip()}")
        return result.stdout


def choose_emulator(binary: str, requested: str | None) -> Adb:
    result = subprocess.run([binary, "devices"], capture_output=True, text=True,
                            check=True, timeout=45)
    devices = [line.split()[0] for line in result.stdout.splitlines()[1:]
               if len(line.split()) >= 2 and line.split()[1] == "device"]
    if requested:
        devices = [serial for serial in devices if serial == requested]
    if len(devices) != 1 or not devices[0].startswith("emulator-"):
        raise ProofFailure("Exactly one selected emulator is required; physical devices are forbidden")
    adb = Adb(binary, devices[0])
    if adb.run("shell", "getprop", "ro.kernel.qemu").strip() != "1":
        raise ProofFailure("Selected device does not report an Android emulator")
    return adb


def current_pid(adb: Adb, package: str) -> int | None:
    value = adb.run("shell", "pidof", package, check=False).strip()
    if not value:
        return None
    if not value.isdigit():
        raise ProofFailure(f"Application PID is ambiguous: {value}")
    return int(value)


def run_proof(args: argparse.Namespace) -> int:
    artifact = args.artifacts
    artifact.mkdir(parents=True, exist_ok=True)
    try:
        adb = choose_emulator(args.adb, args.serial)
    except (ProofFailure, OSError, subprocess.SubprocessError) as exc:
        report = {"commit": args.commit, "passed": False, "error": str(exc)}
        (artifact / "proof-result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))
        return 1
    nonce = secrets.token_hex(16)
    records, pid, failure = [], None, None
    started = time.monotonic()
    report = {"commit": args.commit, "nonce": nonce, "device": adb.command[-1],
              "package": args.package, "records": records, "passed": False, "resources": {}}
    try:
        (artifact / "device.txt").write_text(adb.run("shell", "getprop"))
        # This stop precedes the proof. No restart or force-stop is allowed after launch.
        adb.run("shell", "am", "force-stop", args.package)
        adb.run("logcat", "-c")
        intent = ["shell", "am", "start", "-W", "-n", f"{args.package}/{args.activity}",
                  "--es", "colosseum.nuvio.proof", "true",
                  "--es", "colosseum.nuvio.proof.nonce", nonce]
        for field in ("repository", "plugin", "media"):
            value = getattr(args, field)
            if value:
                intent += ["--es", f"colosseum.nuvio.proof.{field}", value]
        launch = adb.run(*intent)
        (artifact / "proof-am-start.txt").write_text(launch)
        if not re.search(r"^Status: ok\s*$", launch, re.MULTILINE):
            raise ProofFailure("Activity did not report a successful launch")
        pid = current_pid(adb, args.package)
        if not pid:
            raise ProofFailure("Application process missing immediately after launch")
        report["appPid"] = pid
        captured = set()
        while time.monotonic() - started < args.timeout:
            observed = current_pid(adb, args.package)
            with (artifact / "app-pid-observations.jsonl").open("a") as out:
                out.write(json.dumps({"seconds": round(time.monotonic() - started, 3),
                                      "pid": observed}) + "\n")
            if observed != pid:
                raise ProofFailure(f"Application died or restarted: {pid} → {observed}")
            logcat = adb.run("logcat", "-d", "-v", "threadtime")
            (artifact / "proof-logcat.txt").write_text(logcat)
            records = proof_records(logcat, nonce, pid)
            report["records"] = records
            validate_stages(records, pid)
            collect_resource_evidence(adb, artifact, args.package, pid, records, logcat, report["resources"])
            for index, record in enumerate(records):
                stage = record["stage"]
                if index not in captured:
                    (artifact / f"{index:02d}-observed-{stage}.png").write_bytes(
                        adb.run("exec-out", "screencap", "-p", binary=True))
                    captured.add(index)
            if records and records[-1]["stage"] == "completed":
                validate_stages(records, pid, complete=True)
                validate_resource_completion(report["resources"])
                # The app must remain alive and responsive to adb observations
                # beyond the native completion marker, without any relaunch.
                for _ in range(10):
                    time.sleep(1)
                    if current_pid(adb, args.package) != pid:
                        raise ProofFailure("Application died during post-proof hold")
                report["passed"] = True
                break
            time.sleep(0.5)
        if not report["passed"]:
            raise ProofFailure(f"Proof did not complete within {args.timeout} seconds")
    except (ProofFailure, OSError, subprocess.SubprocessError) as exc:
        failure = str(exc)
        report["error"] = failure
    finally:
        for filename, command, binary in (
            ("proof-logcat.txt", ("logcat", "-d", "-v", "threadtime"), False),
            ("proof-crash-logcat.txt", ("logcat", "-d", "-b", "crash", "-v", "threadtime"), False),
            ("proof-processes.txt", ("shell", "ps", "-A"), False),
            ("proof-activities.txt", ("shell", "dumpsys", "activity", "activities"), False),
            ("proof-final.png", ("exec-out", "screencap", "-p"), True),
        ):
            try:
                data = adb.run(*command, binary=binary)
                (artifact / filename).write_bytes(data) if binary else (artifact / filename).write_text(data)
            except (ProofFailure, OSError, subprocess.SubprocessError) as exc:
                report.setdefault("artifact_errors", []).append(f"{filename}: {exc}")
        if report["passed"]:
            try:
                if current_pid(adb, args.package) != pid:
                    raise ProofFailure("Application died during final artifact collection")
                validate_native_crash(records, (artifact / "proof-logcat.txt").read_text(),
                                      (artifact / "proof-crash-logcat.txt").read_text())
            except (ProofFailure, OSError, subprocess.SubprocessError) as exc:
                report["passed"] = False
                report["error"] = str(exc)
        report["duration_seconds"] = round(time.monotonic() - started, 3)
        (artifact / "proof-result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "appPid": pid,
                      "stages": [r["stage"] for r in records], "error": report.get("error")}))
    return 0 if report["passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default=os.environ.get("ADB", "adb"))
    parser.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL"))
    parser.add_argument("--package", default="org.qtproject.example")
    parser.add_argument("--activity", default="org.qtproject.qt.android.bindings.QtActivity")
    parser.add_argument("--artifacts", type=Path, default=Path("nuvio-artifacts"))
    parser.add_argument("--commit", default="local")
    parser.add_argument("--timeout", type=int, default=540)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--media")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return run_proof(args)


if __name__ == "__main__":
    raise SystemExit(main())
