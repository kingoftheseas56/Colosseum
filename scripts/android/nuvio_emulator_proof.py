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
          "crash_started", "worker_started", "crash_survived", "timeout_started", "worker_started",
          "timeout_survived", "stremio_requery_started", "stremio_requeried", "completed")
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
    workers = {}
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
        if stage in ("stremio_fixture_installed", "direct_stream", "stremio_requeried", "completed"):
            if record.get("sourceKind") != "live_https_controlled_fixture":
                raise ProofFailure("Controlled live-HTTPS fixture provenance is missing")
        if stage in ("stremio_requeried", "completed"):
            if record.get("stremioRequery") is not True or record.get("nuvioRequery") is not True:
                raise ProofFailure("Fresh Stremio and Nuvio queries after both faults were not proven")
        if stage in ("direct_stream", "crash_survived", "timeout_survived", "stremio_requeried", "completed"):
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
              "package": args.package, "records": records, "passed": False}
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
            for index, record in enumerate(records):
                stage = record["stage"]
                if index not in captured:
                    (artifact / f"{index:02d}-observed-{stage}.png").write_bytes(
                        adb.run("exec-out", "screencap", "-p", binary=True))
                    captured.add(index)
            if records and records[-1]["stage"] == "completed":
                validate_stages(records, pid, complete=True)
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
    parser.add_argument("--timeout", type=int, default=360)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--media")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return run_proof(args)


if __name__ == "__main__":
    raise SystemExit(main())
