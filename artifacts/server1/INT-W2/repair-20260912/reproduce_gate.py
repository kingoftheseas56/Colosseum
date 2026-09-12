"""Re-run the repaired Windows W2 gate in new scratch directories, never accepting B-W2B."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def path_variants(value):
    text = str(value)
    return (text, text.replace("\\", "/"))


def scrub_text(value, replacements):
    for source, replacement in replacements:
        for variant in path_variants(source):
            value = value.replace(variant, replacement)
    return value


def scrub_bytes(value, replacements):
    text = value.decode("utf-8", errors="replace")
    return scrub_text(text, replacements).encode("utf-8")


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
        print(f"{name}: exit {result.returncode}", flush=True)
        if result.returncode:
            raise RuntimeError(f"{name} failed; inspect {logs}")
        outputs[name] = result.stdout.decode("utf-8")
    names = [test["name"] for test in json.loads(outputs["AGG-INVENTORY"])["tests"]]
    assert len(names) == 16 and set(names) == set(config["expected_ctest"])
    for name in ("AGG-CTEST", "AGG-CTEST-REPEAT"):
        assert "100% tests passed, 0 tests failed out of 16" in outputs[name]
    assert len(re.findall(r"^\s*Start\s+\d+:", outputs["AGG-CTEST-REPEAT"], re.MULTILINE)) == 48
    pairs = [("K01", 72), ("K13A", 11), ("K13B", 9)]
    for name, count in pairs:
        source = outputs[name + "-SOURCE"].splitlines()
        candidate = outputs[name + "-NATIVE"].splitlines()
        assert len(source) == count and source == candidate, name
    expected_http = [line for line in (repo / "artifacts/server1/H00/H00-A/DIFFERENTIAL-TRACE.txt")
                     .read_text(encoding="utf-8").splitlines() if line.startswith("candidate ")]
    assert outputs["H00-NATIVE-TRACE"].splitlines() == expected_http
    assert "H00-01 PASS" in outputs["H00-RAW-WIRE"]
    assert "H00-03 PASS" in outputs["H00-STREAM-DISCONNECT"]
    assert "oversized-chunk-status=413" in outputs["COMBINED-RUN"]
    m00 = json.loads((logs / "m00-differential/RESULT.json").read_text(encoding="utf-8"))
    assert m00["differences"] == 0 and m00["source_lines"] == 11 and m00["candidate_lines"] == 11
    assert m00["mutated_oracle_rejected"]
    head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"]).decode().strip()
    verdict = {"result": "PASS", "candidate_head": head, "public_path_guard": "PASS",
               "ctest": "16/16", "repeated_test_executions": 48,
               "combined_link": "PASS", "source_native_lines": {"K01": 72, "K13-A": 11, "K13-B": 9, "M00": 11},
               "H00_scope": "pinned source-derived trace profile and native real-loopback raw-wire/stream checks, not full live-oracle HTTP parity",
               "B-W2B": "closed pending independent Codex acceptance", "W3": "closed"}
    (logs / "RESULT.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict), flush=True)


if __name__ == "__main__":
    main()
