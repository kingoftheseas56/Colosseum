#!/usr/bin/env python3
"""Run unchanged Qt/WebEngine tests around one production fence mutation.

Context cases include real press/cancel events plus explicitly synthetic late
clicked delivery to isolate the captured-token handler from Qt cancellation.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
build = Path(sys.argv[2]).resolve()
out = root / "evidence/native-qml-controls"
out.mkdir(parents=True, exist_ok=True)
cases = [
    ("play-result-honesty", "qml/feria/FeriaPlayback.js", "tst_OwnedMedia.qml",
     "FeriaOwnedMedia16::test_playRejectionHasObservedBlockedResult",
     "else item.outcome=allowed && !m.paused && !m.ended ? 'resumed' : 'blocked'",
     "else item.outcome='resumed'"),
    ("resource-context-press", "qml/feria/FeriaSaveContextBar.qml", "tst_SaveContext.qml",
     "FeriaNativeSave20::test_contextButtonPressCannotTargetReplacementContext",
     "resourceAction.contextIdAtPress", "root.snapshot.contextId"),
    ("open-context-press", "qml/feria/FeriaSaveContextBar.qml", "tst_SaveContext.qml",
     "FeriaNativeSave20::test_contextButtonPressCannotTargetReplacementContext",
     "openAction.contextIdAtPress", "root.snapshot.contextId"),
    ("copy-context-press", "qml/feria/FeriaSaveContextBar.qml", "tst_SaveContext.qml",
     "FeriaNativeSave20::test_contextButtonPressCannotTargetReplacementContext",
     "copyAction.contextIdAtPress", "root.snapshot.contextId"),
]
results = []
for label, relative, fixture, name, needle, replacement in cases:
    source = root / relative
    original = source.read_bytes()
    text = original.decode()
    if text.count(needle) != 1:
        raise SystemExit(f"{label}: mutation anchor is not unique")
    test_file = root / "tests/feria_capabilities_16_20/qml" / fixture
    assertions = test_file.read_bytes()
    command = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24",
               str(build / "feria_capabilities_runtime"), "-input", str(test_file), name]
    def run(stage):
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        log = result.stdout + result.stderr
        (out / f"{label}-{stage}.log").write_text(log)
        print(log, flush=True)
        if test_file.read_bytes() != assertions:
            raise RuntimeError("Test assertions changed")
        return result.returncode, log
    before, before_log = run("before")
    if before or "FAIL!" in before_log or name not in before_log:
        raise SystemExit(f"{label}: original test is not green")
    try:
        source.write_text(text.replace(needle, replacement))
        negative, negative_log = run("mutant")
    finally:
        source.write_bytes(original)
    restored, restored_log = run("restored")
    valid = negative != 0 and any("FAIL!" in line and name in line for line in negative_log.splitlines())
    valid = valid and not any(x in negative_log for x in ["QML testcase compilation failed", "Unknown test function", "is not a function"])
    expected_assertions = (["Actual   (): succeeded", "Expected (): blocked"]
                           if label == "play-result-honesty" else
                           ["Native replaced-context release must fail closed",
                            "Delivered late context click must reject replaced press token"])
    if label == "play-result-honesty":
        valid = valid and all(message in negative_log for message in expected_assertions)
    else:
        valid = valid and any(message in negative_log for message in expected_assertions)
    valid = valid and restored == 0 and source.read_bytes() == original and test_file.read_bytes() == assertions
    row = {"name": label, "test": name, "before": before, "mutant": negative, "restored": restored,
           "boundary": ("real decoded-media play-result boundary" if label == "play-result-honesty" else "real Qt press/cancel plus explicit synthetic late clicked delivery"),
           "sourceSha256": hashlib.sha256(original).hexdigest(), "assertionsSha256": hashlib.sha256(assertions).hexdigest(),
           "expectedFenceAssertions": expected_assertions, "passed": valid}
    results.append(row)
    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    if not valid:
        raise SystemExit(f"{label}: expected targeted failure and exact restored pass were not observed")
print("FERIA_REAL_QML_NEGATIVE_CONTROLS_PASS", len(results))
