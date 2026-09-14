"""Re-run the repaired Windows W2 gate in new scratch directories, never accepting B-W2B."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


class EvidenceIntegrityError(RuntimeError):
    """The committed evidence cannot be verified or safely published."""


AGGREGATE_REQUIRED_PATHS = (
    "artifacts/server1/INT-W2/repair-20260912/TDD-GREEN.txt",
    "artifacts/server1/INT-W2/repair-20260912/TDD-RED.txt",
    "artifacts/server1/INT-W2/repair-20260912/reproduce_gate.py",
    "artifacts/server1/INT-W2/repair-20260912/test_reproduce_gate.py",
    "artifacts/server1/K01/K01-A/HASHES.json",
    "artifacts/server1/K13/K13-A/HASHES.json",
    "artifacts/server1/K13/K13-B/HASHES.json",
)


MANIFEST_SPECS = (
    ("artifacts/server1/INT-W2/repair-20260912/HASHES.json", ("files",),
     AGGREGATE_REQUIRED_PATHS),
    ("artifacts/server1/K01/K01-A/HASHES.json", ("candidate_files", "evidence_files"), ()),
    ("artifacts/server1/K13/K13-A/HASHES.json", ("worker_files_sha256",), ()),
    ("artifacts/server1/K13/K13-B/HASHES.json", ("worker_files_sha256",), ()),
)


def _git(repo, *argv):
    return subprocess.run(
        ["git", "-C", str(repo), *argv],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def _git_blob(repo, content_ref, relative):
    result = _git(repo, "cat-file", "blob", f"{content_ref}:{relative}")
    if result.returncode:
        raise EvidenceIntegrityError(f"missing Git blob at {content_ref}:{relative}")
    return result.stdout


def verify_hash_manifest(repo, manifest_path, sections, manifest_ref="HEAD", required_paths=()):
    """Verify listed paths against immutable committed Git blob bytes."""
    repo = Path(repo).resolve()
    manifest_path = Path(manifest_path).resolve()
    try:
        relative = manifest_path.relative_to(repo).as_posix()
    except ValueError as exc:
        raise EvidenceIntegrityError("manifest is outside the repository") from exc

    resolved_manifest_ref = _git(repo, "rev-parse", "--verify", f"{manifest_ref}^{{commit}}")
    if resolved_manifest_ref.returncode:
        raise EvidenceIntegrityError("manifest ref is not a commit")
    committed_manifest = _git_blob(repo, resolved_manifest_ref.stdout.decode().strip(), relative)
    try:
        worktree_manifest = manifest_path.read_bytes()
    except OSError as exc:
        raise EvidenceIntegrityError(f"manifest is unreadable: {relative}") from exc
    if worktree_manifest != committed_manifest:
        raise EvidenceIntegrityError(f"manifest differs from committed {manifest_ref}: {relative}")
    try:
        manifest = json.loads(committed_manifest.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvidenceIntegrityError(f"manifest is not valid UTF-8 JSON: {relative}") from exc

    if manifest.get("schema") != "colosseum-server1-git-blob-manifest/v1":
        raise EvidenceIntegrityError(f"unsupported manifest schema: {relative}")
    if manifest.get("hash_algorithm") != "sha256-git-blob":
        raise EvidenceIntegrityError(f"unsupported hash algorithm: {relative}")
    content_ref = manifest.get("content_ref", "")
    if not re.fullmatch(r"[0-9a-f]{40}", content_ref):
        raise EvidenceIntegrityError(f"content_ref is not an explicit full commit id: {relative}")
    resolved_content_ref = _git(repo, "rev-parse", "--verify", f"{content_ref}^{{commit}}")
    if (resolved_content_ref.returncode
            or resolved_content_ref.stdout.decode().strip().lower() != content_ref):
        raise EvidenceIntegrityError(f"content_ref is not an available commit: {relative}")
    ancestor = _git(repo, "merge-base", "--is-ancestor", content_ref,
                    resolved_manifest_ref.stdout.decode().strip())
    if ancestor.returncode:
        raise EvidenceIntegrityError(f"content_ref is not an ancestor of {manifest_ref}: {relative}")

    checked = 0
    mismatches = []
    seen = set()
    for section in sections:
        entries = manifest.get(section)
        if not isinstance(entries, dict) or not entries:
            raise EvidenceIntegrityError(f"missing hash section {section}: {relative}")
        for path, expected in entries.items():
            if path in seen:
                raise EvidenceIntegrityError(f"duplicate manifest path: {path}")
            seen.add(path)
            if not isinstance(expected, str) or not re.fullmatch(r"[0-9A-Fa-f]{64}", expected):
                raise EvidenceIntegrityError(f"invalid SHA-256 for manifest path: {path}")
            try:
                blob = _git_blob(repo, content_ref, path)
            except EvidenceIntegrityError:
                mismatches.append({"path": path, "reason": "missing"})
                continue
            actual = hashlib.sha256(blob).hexdigest()
            if actual != expected.lower():
                mismatches.append({"path": path, "reason": "sha256-mismatch"})
            checked += 1
    missing_required = sorted(set(required_paths) - seen)
    if missing_required:
        raise EvidenceIntegrityError(
            f"manifest omits required sealing artifacts for {relative}: "
            + ", ".join(missing_required))
    if mismatches:
        raise EvidenceIntegrityError(
            f"manifest verification failed for {relative}: {len(mismatches)} mismatch(es)")
    return {
        "result": "PASS",
        "manifest": relative,
        "manifest_ref": resolved_manifest_ref.stdout.decode().strip(),
        "content_ref": content_ref,
        "hash_algorithm": "sha256-git-blob",
        "checked_files": checked,
    }


def verify_all_manifests(repo):
    results = []
    for relative, sections, required_paths in MANIFEST_SPECS:
        results.append(verify_hash_manifest(
            repo, Path(repo) / relative, sections, required_paths=required_paths,
        ))
    return {
        "result": "PASS",
        "manifests": results,
        "checked_files": sum(item["checked_files"] for item in results),
    }


def path_variants(value):
    text = str(value)
    return (text, text.replace("\\", "/"))


def scrub_text(value, replacements):
    for source, replacement in sorted(replacements, key=lambda item: len(str(item[0])), reverse=True):
        parts = [re.escape(part) for part in re.split(r"[\\/]+", str(source)) if part]
        if not parts:
            continue
        pattern = re.compile(r"[\\/]+".join(parts), re.IGNORECASE)
        value = pattern.sub(lambda _: str(replacement), value)
    return value


def scrub_bytes(value, replacements):
    text = value.decode("utf-8", errors="replace")
    return scrub_text(text, replacements).encode("utf-8")


def _sensitive_pattern(value):
    parts = [re.escape(part) for part in re.split(r"[\\/]+", str(value)) if part]
    if not parts:
        raise EvidenceIntegrityError("empty sensitive path")
    return re.compile(r"[\\/]+".join(parts), re.IGNORECASE)


def scrub_generated_tree(root, replacements):
    """Recursively scrub every regular UTF-8 generated file below root."""
    root = Path(root)
    changed = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.is_symlink():
            raise EvidenceIntegrityError(f"generated output contains a symlink: {path.relative_to(root)}")
        try:
            original = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            raise EvidenceIntegrityError(
                f"generated output is not readable UTF-8: {path.relative_to(root)}") from exc
        scrubbed = scrub_text(original, replacements)
        if scrubbed != original:
            path.write_text(scrubbed, encoding="utf-8", newline="")
            changed += 1
    return changed


def find_private_path_leaks(root, sensitive_paths):
    root = Path(root)
    patterns = [_sensitive_pattern(path) for path in sensitive_paths]
    findings = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.is_symlink():
            findings.append({"path": path.relative_to(root).as_posix(), "reason": "symlink"})
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            findings.append({"path": path.relative_to(root).as_posix(), "reason": "unreadable"})
            continue
        if any(pattern.search(text) for pattern in patterns):
            findings.append({"path": path.relative_to(root).as_posix(), "reason": "private-path"})
    return findings


def assert_no_private_path_leaks(root, sensitive_paths):
    findings = find_private_path_leaks(root, sensitive_paths)
    if findings:
        names = ", ".join(item["path"] for item in findings)
        raise EvidenceIntegrityError(
            f"generated output private-path scan failed: {len(findings)} file(s): {names}")
    return len([path for path in Path(root).rglob("*") if path.is_file()])


def _require_verdict(condition, message):
    if not condition:
        raise EvidenceIntegrityError(message)


def verify_mandatory_verdicts(outputs, expected_ctest, expected_http, m00):
    """Fail closed on every result required before emitting a PASS receipt."""
    try:
        inventory = json.loads(outputs["AGG-INVENTORY"])
        tests = inventory["tests"]
        names = [test["name"] for test in tests]
    except (KeyError, TypeError, json.JSONDecodeError) as exc:
        raise EvidenceIntegrityError("aggregate CTest inventory is malformed") from exc

    _require_verdict(
        len(expected_ctest) == 16 and len(set(expected_ctest)) == 16,
        "configured CTest inventory must contain 16 unique tests",
    )
    _require_verdict(
        len(names) == 16 and len(set(names)) == 16 and set(names) == set(expected_ctest),
        "aggregate CTest inventory does not match the exact 16-test contract",
    )
    for name in ("AGG-CTEST", "AGG-CTEST-REPEAT"):
        _require_verdict(
            "100% tests passed, 0 tests failed out of 16" in outputs.get(name, ""),
            f"{name} does not report 16/16 passing",
        )
    repeated = len(re.findall(
        r"^\s*Start\s+\d+:", outputs.get("AGG-CTEST-REPEAT", ""), re.MULTILINE,
    ))
    _require_verdict(repeated == 48, "aggregate repeat gate did not execute exactly 48 tests")

    line_counts = {}
    for name, label, count in (("K01", "K01", 72), ("K13A", "K13-A", 11),
                               ("K13B", "K13-B", 9)):
        source = outputs.get(name + "-SOURCE", "").splitlines()
        candidate = outputs.get(name + "-NATIVE", "").splitlines()
        _require_verdict(
            len(source) == count and source == candidate,
            f"{label} source/native differential is not exact at {count} lines",
        )
        line_counts[label] = count

    _require_verdict(
        outputs.get("H00-NATIVE-TRACE", "").splitlines() == list(expected_http),
        "H00 native trace does not match the pinned candidate profile",
    )
    _require_verdict("H00-01 PASS" in outputs.get("H00-RAW-WIRE", ""),
                     "H00 raw-wire verdict is missing")
    _require_verdict("H00-03 PASS" in outputs.get("H00-STREAM-DISCONNECT", ""),
                     "H00 stream-disconnect verdict is missing")
    _require_verdict("oversized-chunk-status=413" in outputs.get("COMBINED-RUN", ""),
                     "combined consumer did not prove oversized chunk status 413")

    try:
        differences = m00["differences"]
        source_lines = m00["source_lines"]
        candidate_lines = m00["candidate_lines"]
        m00_exact = (
            type(differences) is int and differences == 0
            and type(source_lines) is int and source_lines == 11
            and type(candidate_lines) is int and candidate_lines == 11
        )
        mutation_rejected = m00["mutated_oracle_rejected"] is True
    except (KeyError, TypeError) as exc:
        raise EvidenceIntegrityError("M00 differential result is malformed") from exc
    _require_verdict(m00_exact, "M00 source/native differential is not exact at 11 lines")
    _require_verdict(mutation_rejected, "M00 one-byte oracle mutation was not rejected")

    line_counts["M00"] = 11
    return {
        "ctest": "16/16",
        "repeated_test_executions": repeated,
        "source_native_lines": line_counts,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True, type=Path, help="A new, nonexistent scratch directory")
    ap.add_argument("--oracle", required=True, type=Path)
    args = ap.parse_args()
    evidence = Path(__file__).resolve().parent
    repo = evidence.parents[3]
    config = json.loads((evidence / "REPRODUCTION.json").read_text(encoding="utf-8"))
    records = json.loads((evidence / "COMMANDS.json").read_text(encoding="utf-8"))
    if os.name != "nt":
        raise RuntimeError("This receipt qualifies the locked Windows lane only")
    if hashlib.sha256(args.oracle.read_bytes()).hexdigest() != config["oracle_sha256"]:
        raise RuntimeError("oracle SHA-256 mismatch")
    args.work = args.work.resolve()
    args.oracle = args.oracle.resolve()
    args.work.mkdir(parents=True, exist_ok=False)
    logs = args.work / "logs"
    logs.mkdir()
    manifest_result = verify_all_manifests(repo)
    (logs / "MANIFEST-VERIFIER.json").write_text(
        json.dumps(manifest_result, indent=2) + "\n", encoding="utf-8")
    print(f"MANIFEST-VERIFIER: {manifest_result['checked_files']} Git blobs PASS", flush=True)
    guard = subprocess.run(
        [sys.executable, str(repo / "scripts" / "check_public_paths.py")],
        cwd=repo, capture_output=True, text=True, check=False,
    )
    (logs / "PUBLIC-PATH-GUARD.stdout.txt").write_text(guard.stdout, encoding="utf-8")
    (logs / "PUBLIC-PATH-GUARD.stderr.txt").write_text(guard.stderr, encoding="utf-8")
    print(f"PUBLIC-PATH-GUARD: exit {guard.returncode}", flush=True)
    if guard.returncode:
        raise RuntimeError("public path guard failed")
    replacements = [
        (args.work, config["recorded_scratch"]),
        (args.oracle, "<ORACLE_PATH>"),
        (repo, config["recorded_checkout"]),
        (Path.home(), "<USER_HOME>"),
    ]
    env_script = args.work / "load-msvc.cmd"
    env_script.write_text('@echo off\ncall "' + config["vsdevcmd"]
                          + '" -arch=x64 -host_arch=x64 >nul\nif errorlevel 1 exit /b 1\nset\n',
                          encoding="utf-8")
    captured = subprocess.check_output(["cmd", "/d", "/c", str(env_script)])
    env = {}
    for line in captured.decode("utf-8", errors="replace").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key] = value
    path_key = next(key for key in env if key.lower() == "path")
    env[path_key] = str(Path(config["qt_prefix"]) / "bin") + ";" + env[path_key]
    for key in ("M00_ORACLE_PATH", "K13_A_ORACLE_PATH", "K13_B_ORACLE_PATH"):
        env[key] = str(args.oracle)
    env["SETTINGS_PATH"] = str(args.work / "k01-oracle-run")
    (args.work / "k01-oracle-run").mkdir()
    def relocate(value):
        return (value.replace(config["recorded_scratch"], str(args.work))
                .replace(config["recorded_checkout"], str(repo)))
    outputs = {}
    executed = []
    for record in records:
        if record.get("qualification", "").startswith("discarded"):
            continue
        name = record["name"]
        argv = [relocate(value) for value in record["argv"]]
        if argv[0] == "python":
            argv[0] = sys.executable
        if name == "K01-SOURCE":
            argv[-1] = str(args.oracle)
        if name == "M00-ASSEMBLED-DIFFERENTIAL":
            argv[argv.index("--oracle") + 1] = str(args.oracle)
            argv[argv.index("--output") + 1] = str(logs / "m00-differential")
        cwd = Path(relocate(record["cwd"]))
        result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, timeout=180)
        (logs / (name + ".stdout.txt")).write_bytes(scrub_bytes(result.stdout, replacements))
        (logs / (name + ".stderr.txt")).write_bytes(scrub_bytes(result.stderr, replacements))
        executed.append({
            "name": name,
            "argv": [scrub_text(value, replacements) for value in argv],
            "cwd": scrub_text(str(cwd), replacements),
            "exit": result.returncode,
        })
        (logs / "EXECUTED.json").write_text(json.dumps(executed, indent=2) + "\n", encoding="utf-8")
        scrub_generated_tree(logs, replacements)
        assert_no_private_path_leaks(logs, [item[0] for item in replacements])
        print(f"{name}: exit {result.returncode}", flush=True)
        if result.returncode:
            raise RuntimeError(f"{name} failed; inspect {logs}")
        outputs[name] = result.stdout.decode("utf-8")
    expected_http = [line for line in (repo / "artifacts/server1/H00/H00-A/DIFFERENTIAL-TRACE.txt")
                     .read_text(encoding="utf-8").splitlines() if line.startswith("candidate ")]
    try:
        m00 = json.loads((logs / "m00-differential/RESULT.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvidenceIntegrityError("M00 differential result is unreadable") from exc
    mandatory = verify_mandatory_verdicts(
        outputs, config["expected_ctest"], expected_http, m00,
    )
    head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"]).decode().strip()
    verdict = {"result": "PASS", "candidate_head": head, "public_path_guard": "PASS",
               "ctest": mandatory["ctest"],
               "repeated_test_executions": mandatory["repeated_test_executions"],
               "combined_link": "PASS", "source_native_lines": mandatory["source_native_lines"],
               "H00_scope": "pinned source-derived trace profile and native real-loopback raw-wire/stream checks, not full live-oracle HTTP parity",
               "B-W2B": "closed pending independent Codex acceptance", "W3": "closed"}
    (logs / "RESULT.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    scrubbed_files = scrub_generated_tree(logs, replacements)
    regular_files = assert_no_private_path_leaks(logs, [item[0] for item in replacements])
    public_path_result = {
        "result": "PASS",
        "tracked_public_path_guard": "PASS",
        "generated_output_scan": "PASS",
        "regular_files_scanned": regular_files + 1,
        "private_path_findings": 0,
        "files_scrubbed_in_final_pass": scrubbed_files,
    }
    (logs / "PUBLIC-PATH-RESULT.json").write_text(
        json.dumps(public_path_result, indent=2) + "\n", encoding="utf-8")
    assert_no_private_path_leaks(logs, [item[0] for item in replacements])
    print(json.dumps(verdict), flush=True)


if __name__ == "__main__":
    main()
