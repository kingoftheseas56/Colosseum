#!/usr/bin/env python3
"""Runner for the WebView2 close integration suite.

Runs the real-engine suite green once, then re-runs the departure-evidence
control with FERIA_UNSAFE_EARLY_DISPOSE=1, which makes the adapter settle
Leave without waiting for the correlated internal navigation completion.
That RED run must fail on the correlated-navigation-id assertion (exit 1
with the specific message), never as a crash or timeout. Writes results
and logs to --output.

Usage: run_integration.py <exe> <source-dir> [--output DIR]
"""

import argparse
import json
import os
import pathlib
import subprocess
import sys
import time

def run(exe, source_dir, output_dir, name, env_extra, test_filter=None,
        timeout=420):
    env = dict(os.environ)
    env["FERIA_ENABLE_TEST_INPUT"] = "1"
    env["QT_QPA_PLATFORM"] = "windows"
    env.update(env_extra)
    log_path = output_dir.resolve() / (name + ".log")
    args = [str(exe), "-o",
            str(output_dir.resolve() / (name + ".txt")) + ",txt",
            "-input", str(source_dir.resolve())]
    if test_filter:
        args.append(test_filter)
    started = time.time()
    try:
        proc = subprocess.run(args, env=env, capture_output=True,
                              text=True, timeout=timeout, cwd=str(source_dir))
        exit_code, stdout, stderr = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as expired:
        exit_code = "timeout"
        stdout = (expired.stdout or b"").decode("utf-8", "replace")
        stderr = (expired.stderr or b"").decode("utf-8", "replace")
    log_path.write_text(stdout + "\n--- stderr ---\n" + stderr, encoding="utf-8")
    detail = ""
    txt = output_dir.resolve() / (name + ".txt")
    if txt.exists():
        detail = txt.read_text(encoding="utf-8", errors="replace")
    return {"name": name, "exit": exit_code, "seconds": round(time.time() - started, 1),
            "log": str(log_path), "detail": detail}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("source_dir", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    output = args.output
    output.mkdir(parents=True, exist_ok=True)

    green = run(args.exe, args.source_dir, output, "green", {})
    green_ok = green["exit"] == 0

    red = run(args.exe, args.source_dir, output, "red-early-dispose",
              {"FERIA_UNSAFE_EARLY_DISPOSE": "1"},
              test_filter="FeriaWebView2Close::test_leaveRequiresCorrelatedDeparture")
    expected_marker = "correlated native navigation id"
    red_failed_for_reason = (
        red["exit"] == 1
        and expected_marker in red["detail"]
        and "Totals: 1 passed" not in red["detail"]
    )

    results = {
        "suite": "feria_webview2_close_integration",
        "exe": str(args.exe),
        "green": {k: green[k] for k in ("name", "exit", "seconds", "log")},
        "green_summary": [line for line in green["detail"].splitlines()
                          if line.startswith("Totals")],
        "negative_control": {
            "mode": "FERIA_UNSAFE_EARLY_DISPOSE=1 (adapter settles Leave without correlated departure)",
            "expected": "exit 1 with the correlated-navigation-id assertion",
            "exit": red["exit"],
            "failed_for_intended_reason": red_failed_for_reason,
            "log": red["log"],
        },
        "passed": bool(green_ok and red_failed_for_reason),
    }
    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in results.items() if k != "negative_control"}, indent=2))
    print("negative control:", json.dumps(results["negative_control"], indent=2))
    return 0 if results["passed"] else 1

if __name__ == "__main__":
    sys.exit(main())
