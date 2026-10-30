#!/usr/bin/env python3
"""Run bounded existing updater components without fetching or changing a feed.

This is evidence plumbing around the production verifier and existing native
harnesses, not a replacement updater. Windows installation/handoff remains a
separate receiving test. Exit 2 means incomplete, exit 1 means a failed case.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA = "a6104f7a8d9aab9766e71f031b7908beaeca85ad"
PRODUCTION_DER_SHA256 = "4cffb5eea31c17ee342dff6b6e2b1412d4e95306986596d4ba5aa5e6549a892f"
DISCOVERY_URL = "https://getcolosseum.world/updates/stable.json"
KNOWN_WINDOWS_INSTALLER = {
    "name": "Colosseum-1.1.9-setup.exe", "size": 328380744,
    "sha256": "a4f4fe1279335769d96b5e30f9917a21fcddd8fc87ce0c9d6f2538fd6aaa57e6",
    "packaged_source_sha": "70e0773b56fa4a35adc1c0e14a8520e4b680c27b",
    "compiled_source_sha": "47e90b3f244d9ad76b7f5bbc7607629fe358288a",
    "runtime": "Qt 6.11.1 / MSVC 2022", "verification": "NOT RUN: receiving-side artifact",
}

# Existing binaries execute their production components against local fixtures.
# These names are registered in tests/CMakeLists.txt, except the Qt Test name.
NATIVE_CASES = {
    "UPD-NATIVE-VERSION": ("update_version_harness", "UPDATE_VERSION_OK",
        "canonical version ordering; native component"),
    "UPD-NATIVE-TRUST": ("update_manifest_trust_harness", "UPDATE_MANIFEST_TRUST_OK",
        "Ed25519 mutation rejection, schema/unsafe URL rejection; explicit test public key"),
    "UPD-NATIVE-DISCOVERY": ("update_release_client_harness", "UPDATE_RELEASE_CLIENT_OK",
        "signed legacy and first-party loopback feeds; metadata mismatch, 503, timeout, unsafe redirect"),
    "UPD-NATIVE-DOWNLOAD": ("update_download_harness", "UPDATE_DOWNLOAD_OK",
        "real loopback streaming, cancellation/resume, changed ETag, wrong size/hash and unsafe path"),
    "UPD-NATIVE-SERVICE": ("update_service_harness", "UPDATE_SERVICE_OK",
        "native service state/persistence with synthetic release-client seam; no assembled GUI"),
    "UPD-NATIVE-BRIDGE": ("update_install_bridge_harness", "UPDATE_INSTALL_BRIDGE_OK",
        "install-bridge unit behavior; Linux execution does not prove Windows installer handoff"),
    "UPD-NATIVE-CHRONICLE": ("tst_installed_chronicle", "Totals:",
        "native loader rejection tests with signed test-key 1.1.0 fixture, not installed 1.1.9 UI"),
}
PYTHON_CASES = {
    "UPD-TOOLING": ("tests/update_release_tooling_test.py", "production-component/modeled-publisher",
        "actual release generation/verification; publisher calls are FakeGitHub only"),
    "UPD-FEED-SECURITY": ("tests/test_stable_feed.py", "production-component",
        "offline feed generator verifies ephemeral signed fixtures, unsafe URLs and altered assets"),
    "WIN-INSTALLER-CONTRACT": ("tests/test_release_installer_smoke_contract.py", "static-source",
        "installer/workflow source contracts only; no Windows runtime or installation"),
}


def _sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _production_tools():
    # Import the repository's actual verifier; no duplicate signature algorithm.
    path = str(ROOT / "scripts/update")
    if path not in sys.path:
        sys.path.insert(0, path)
    from generate_update_manifest import DER_PREFIX, cpp_header_key
    from verify_update_release import verify_signature
    return DER_PREFIX, cpp_header_key, verify_signature


def check_production_chronicle(source_root: Path, output: Path) -> dict[str, Any]:
    prefix, read_key, verify_signature = _production_tools()
    key = prefix + read_key(source_root / "native/update/UpdatePublicKey.h")
    digest = hashlib.sha256(key).hexdigest()
    if digest != PRODUCTION_DER_SHA256:
        raise ValueError(f"production public-key continuity failed: {digest}")
    output.mkdir(parents=True, exist_ok=True)
    public_der = output / "production-public.der"
    public_der.write_bytes(key)  # Public verification key only; no private key.
    manifest = source_root / "resources/installed-chronicle/installed-manifest.json"
    signature = manifest.with_suffix(".json.sig")
    verify_signature(manifest, signature, public_der)
    data = json.loads(manifest.read_bytes())
    if (data.get("version"), data.get("tag"), data.get("title")) != (
            "1.1.9", "v1.1.9", "Colosseum 1.1.9"):
        raise ValueError("verified chronicle is not the installed 1.1.9 identity")
    if len(data.get("highlights", [])) != 6:
        raise ValueError("installed 1.1.9 chronicle must contain six release chapters")
    return {
        "version": data["version"], "highlights": len(data["highlights"]),
        "public_key_der_sha256": digest, "manifest_sha256": _sha(manifest),
        "signature_sha256": _sha(signature), "manifest": str(manifest),
        "signature": str(signature), "public_der": str(public_der),
        "limit": "verifies checked-in production-signed bytes; installed GUI rendering is separate",
    }


def check_discovery_url(source_root: Path) -> dict[str, str]:
    source = source_root / "native/main.cpp"
    exact = r'updateConfig\.latestReleaseUrl\s*=\s*QUrl\(\s*QStringLiteral\("' + re.escape(DISCOVERY_URL) + r'"\)\s*\);'
    if not re.search(exact, source.read_text(encoding="utf-8")):
        raise ValueError("production discovery URL is not the exact first-party assignment")
    return {"layer": "static-source", "url": DISCOVERY_URL, "source": str(source),
            "source_sha256": _sha(source), "limit": "no production network request or feed activation"}


def run_command(case_id: str, command: list[str], output: Path, cwd: Path,
                sentinel: str | None = None, timeout: float = 120) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    log = output / f"{case_id}.log"
    result: dict[str, Any] = {
        "case_id": case_id, "status": "FAIL", "command": command, "cwd": str(cwd),
        "exit_code": None, "assertions": [], "evidence": [str(log)], "timed_out": False,
    }
    start = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=timeout)
        log.write_text(completed.stdout + "\n--- stderr ---\n" + completed.stderr, encoding="utf-8")
        result["exit_code"] = completed.returncode
        result["assertions"].append({"check": "exit code is zero", "pass": completed.returncode == 0})
        if sentinel:
            result["assertions"].append({"check": f"component success sentinel: {sentinel}",
                                          "pass": sentinel in completed.stdout + completed.stderr})
        result["status"] = "PASS" if all(a["pass"] for a in result["assertions"]) else "FAIL"
        combined = completed.stdout + completed.stderr
        # unittest, Qt Test and Node summaries must not turn omitted cases green.
        skipped = any(int(value) > 0 for pattern in (
            r"\bskipped=(\d+)", r"\b(\d+) skipped\b", r"\bskipped\s+(\d+)\b")
            for value in re.findall(pattern, combined))
        if skipped:
            result["assertions"].append({"check": "component reports zero skipped tests", "pass": False})
            if result["status"] == "PASS":
                result.update(status="BLOCKED", reason="component reported skipped tests")
    except FileNotFoundError as error:
        result.update(status="BLOCKED", reason=str(error))
        log.write_text(str(error), encoding="utf-8")
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode("utf-8", "replace") if isinstance(value, bytes) else (value or "")
        log.write_text(decoded(error.stdout) + "\n--- stderr ---\n" + decoded(error.stderr), encoding="utf-8")
        result.update(timed_out=True, reason=f"component exceeded {timeout}s and was terminated")
    except OSError as error:
        result.update(status="BLOCKED", reason=str(error))
        log.write_text(str(error), encoding="utf-8")
    result["duration_ms"] = round((time.monotonic() - start) * 1000)
    return result


def _function_case(case_id: str, function, source_root: Path, output: Path,
                   layer: str, description: str) -> dict[str, Any]:
    start = time.monotonic()
    log = output / f"{case_id}.json"
    result: dict[str, Any] = {
        "case_id": case_id, "layer": layer, "description": description,
        "command": [f"python:{function.__name__}", str(source_root)],
        "exit_code": None, "evidence": [str(log)], "assertions": [],
    }
    try:
        details = function(source_root, output)
        result.update(status="PASS", details=details,
                      assertions=[{"check": description, "pass": True}])
    except (FileNotFoundError, PermissionError) as error:
        result.update(status="BLOCKED", reason=str(error))
    except Exception as error:
        result.update(status="FAIL", reason=f"{type(error).__name__}: {error}")
        if isinstance(error, subprocess.CalledProcessError):
            result["failing_command"] = error.cmd
            result["exit_code"] = error.returncode
            result["stderr"] = (error.stderr or b"").decode("utf-8", "replace")
    result["duration_ms"] = round((time.monotonic() - start) * 1000)
    log.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def run_checks(source_root: Path = ROOT, output: Path | None = None,
               source_sha: str = SOURCE_SHA, native_bin_dir: Path | None = None,
               cases: list[str] | None = None) -> dict[str, Any]:
    source_root = Path(source_root).resolve()
    if output is None:
        raise ValueError("an explicit evidence output directory is required")
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    all_ids = ["UPD-PROD-CHRONICLE", "UPD-DISCOVERY-CONTRACT", *PYTHON_CASES,
               "UPD-ROUTE-CONTRACT", *NATIVE_CASES, "WIN-MANUAL-MIGRATION", "WIN-AUTO-HANDOFF"]
    selected = cases if cases is not None else all_ids
    if not selected or any(case not in all_ids for case in selected):
        raise ValueError("unknown case or empty case selection")
    if source_sha != SOURCE_SHA:
        raise ValueError("candidate source SHA differs from the approved starting pin")
    results: list[dict[str, Any]] = []
    for case_id in dict.fromkeys(selected):
        if case_id == "UPD-PROD-CHRONICLE":
            result = _function_case(case_id, check_production_chronicle, source_root, output,
                                    "production-component", "production key continuity and authentic 1.1.9 chronicle signature")
        elif case_id == "UPD-DISCOVERY-CONTRACT":
            def discovery(root, _output): return check_discovery_url(root)
            result = _function_case(case_id, discovery, source_root, output, "static-source",
                                    "first-party discovery URL assignment")
        elif case_id in PYTHON_CASES:
            script, layer, description = PYTHON_CASES[case_id]
            result = run_command(case_id, [sys.executable, "-m", "unittest", "discover", "-s", str(source_root / "tests"), "-p", Path(script).name, "-v"], output, source_root, "OK")
            result.update(layer=layer, description=description)
        elif case_id == "UPD-ROUTE-CONTRACT":
            result = run_command(case_id, [shutil.which("node") or "node", "--test",
                                 str(source_root / "scripts/update/test_cloudflare_update_route.mjs")], output, source_root)
            result.update(layer="production-component/mocked-fetch", description="existing route worker unit tests; no deployed-route check")
        elif case_id in NATIVE_CASES:
            target, sentinel, description = NATIVE_CASES[case_id]
            binary = (Path(native_bin_dir) if native_bin_dir else output / "missing-native-build") / (target + (".exe" if os.name == "nt" else ""))
            # Qt Test targets are emitted into build/tests, unlike harnesses.
            if not binary.is_file() and native_bin_dir and (Path(native_bin_dir) / "tests" / binary.name).is_file():
                binary = Path(native_bin_dir) / "tests" / binary.name
            result = run_command(case_id, [str(binary)], output, source_root, sentinel)
            result.update(layer="native-component", description=description, binary_sha256=_sha(binary) if binary.is_file() else None)
        else:
            result = {
                "case_id": case_id, "status": "BLOCKED" if case_id == "WIN-AUTO-HANDOFF" else "NOT RUN",
                "layer": "windows-receiving", "command": [], "exit_code": None,
                "assertions": [], "evidence": [],
                "reason": ("Requires authentic higher-version signed installer fixture and supported private staging route. "
                           "Same-version checks and test-key/native mocks cannot qualify automatic install/restart."
                           if case_id == "WIN-AUTO-HANDOFF" else
                           "Run windows_receiving.ps1 and complete windows_receiving.md on a disposable Windows account/VM."),
            }
        results.append(result)
    counts = {status: 0 for status in ("PASS", "FAIL", "BLOCKED", "NOT RUN")}
    counts.update(Counter(result["status"] for result in results))
    report = {
        "schema": "colosseum.acceptance119.updater.v1", "created_at": datetime.now(timezone.utc).isoformat(),
        "source_root": str(source_root), "source_sha": source_sha,
        "source_identity_limit": "source_sha is the approved base; build provenance must be supplied by the package/build record",
        "os": platform.platform(), "runtime": {"python": sys.version, "openssl": shutil.which("openssl")},
        "native_bin_dir": str(native_bin_dir) if native_bin_dir else None,
        "known_windows_installer": KNOWN_WINDOWS_INSTALLER, "cases": results, "counts": counts,
        "qualified": bool(results) and all(result["status"] == "PASS" for result in results),
        "limits": ["A selected subset can pass without qualifying the complete release.",
                   "No actual installer update, Windows helper shutdown, or GUI navigation is implied by component results."],
    }
    (output / "updater-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-sha", default=SOURCE_SHA)
    parser.add_argument("--native-bin-dir", type=Path)
    parser.add_argument("--case", action="append")
    args = parser.parse_args()
    try:
        report = run_checks(args.source_root, args.output, args.source_sha, args.native_bin_dir, args.case)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps({"counts": report["counts"], "report": str(args.output / "updater-results.json")}, indent=2))
    return 1 if report["counts"]["FAIL"] else (0 if report["qualified"] else 2)


if __name__ == "__main__":
    raise SystemExit(main())
