#!/usr/bin/env python3
"""Compile real owners with one fence removed, requiring its assertion to fail.

Uses the already installed Qt SDK through CMAKE_PREFIX_PATH. Never downloads or
builds Qt, edits the working checkout, or launches external applications.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

P = "native/feria/FeriaBrowserPolicy.cpp"
H = "native/feria/FeriaBrowserPolicy.h"
A = "native/feria/FeriaAccountStore.cpp"
CONTROLS = [
    ("native_profile_parent", H, "return request && profile && request->parent() == profile;",
     "return request && profile;", "Only exact native request parent profile is accepted"),
    ("unknown_total", A, "row.insert(key, bytes);", "row.insert(key, qMax<qint64>(0, bytes));",
     "Unknown total is not coerced to a known zero-byte total"),
    ("one_use_grant", A, "if (saved) m_downloadAuthorizations.erase(grant);",
     "if (saved) { grant->committing = false; grant->pendingRow.clear(); }",
     "A committed metadata grant cannot replay"),
    ("atomic_retry_grant", A, "else grant->committing = false;",
     "else m_downloadAuthorizations.erase(grant);",
     "Write retry cannot change its settled descriptor"),
    ("settled_retry_binding", A,
     'if (!pendingRow.isEmpty() && row != pendingRow) return reject("settled-retry-descriptor-changed");',
     "// negative control: allow changed retry",
     "Write retry cannot change its settled descriptor"),
    ("synchronous_reentry", A, "m_downloadAuthorizations[token].committing = true;",
     "m_downloadAuthorizations[token].committing = false;",
     "Retry keeps its exact grant and synchronous changed callbacks cannot replay it"),
    ("terminal_validation", A,
     'if (authorized) m_downloadAuthorizations.remove(token);\n        return outcome("rejected", reason);',
     'return outcome("rejected", reason);', "Rejected URL cannot reuse grant for another descriptor"),
    ("url_privacy", A,
     'const auto url = browserUrl(inputUrl.toString(), true);\n    if (url.isEmpty()',
     'const auto url = QStringLiteral("https://example.com/file");\n    if (url.isEmpty()',
     "Inadmissible URL has terminal rejection"),
    ("transfer_binding", A,
     'if (binding.contains(key) && descriptor.value(key) != binding.value(key)) return reject("transfer-binding-mismatch");',
     "if (false) return reject(\"transfer-binding-mismatch\");", "Grant binds exact transfer ID"),
    ("private_grant", A,
     "if (!visit || visit->privateBrowsing || !hasDownloadScope()) return {};",
     "if (!visit || !hasDownloadScope()) return {};", "Private visit cannot acquire ordinary history grant"),
    ("unsafe_leaf", P, "if (!safeDownloadLeaf(fileName) || !safeDownloadText(normalized)",
     "if (!safeDownloadText(normalized)", "Unsafe destination leaf is not admitted"),
    ("overwrite_admission", P, "|| target.exists() || target.isSymLink())",
     ")", "No implicit overwrite admission"),
    ("completed_state", P, 'if (snapshot.size() != 6 || snapshot.value("state") != "completed") return false;',
     'if (snapshot.size() != 6) return false;', "Cancelled descriptor cannot open a file"),
    ("native_file_identity", P,
     '|| downloadNativeIdentity(path, false) != snapshot.value("identity").toMap()',
     "", "Same path and bytes but new native file identity is rejected"),
    ("action_time_validation", P, 'if (status.value("status") != "available") return status;',
     "// negative control: dispatch without checking", "Action-time replacement fence blocks shell dispatch"),
    ("canonical_account_store", P, "m_downloadAccountStore.data() == store",
     "true", "A retained distinct old account store cannot authorize history in the new canonical scope"),
    ("account_scope", P,
     ["store->downloadScopeGeneration() == scope.accountGeneration", "store->downloadScopeGeneration() == m_downloadAccountGeneration"],
     ["true", "true"], "Account change retires prior live file scope"),
    ("profile_scope", P,
     ["m_storageGeneration == scope.storageGeneration", "m_storageGeneration == m_downloadAccountStorageGeneration"],
     ["true", "true"], "Storage root replacement retires registered live scope"),
    ("corrupt_reload_preservation", A, "if (!m_error.isEmpty()) m_state = lastGoodState;",
     "// negative control: discard last good state", "A same-account partial reload preserves last good in-memory state"),
    ("admitted_file_path", A,
     '|| row.value("path") != snapshot.value("path") || admission.value("status") != "admitted")',
     '|| admission.value("status") != "admitted")', "Completed snapshot must match admitted metadata path"),
]


def command(args, cwd):
    run = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=240, env=os.environ.copy())
    if run.returncode:
        raise RuntimeError("Command failed: " + " ".join(args) + "\n" + run.stdout)
    return run.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cmake = os.environ.get("CMAKE", "cmake")
    paths = [P, H, A, "native/feria/FeriaAccountStore.h", "native/feria/app-state/PorticoAppStateBridge.cpp",
             "native/feria/app-state/PorticoAppStateBridge.h", "tests/feria_download_policy_test.cpp"]
    originals = {path: (args.source / path).read_text() for path in paths}
    results = []
    with tempfile.TemporaryDirectory(prefix="feria-native-controls-") as temporary:
        root = Path(temporary)
        for path, text in originals.items():
            destination = root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(text)
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.24)
project(FeriaNativeDownloadControls LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Qml Sql)
add_executable(feria_download_policy_test tests/feria_download_policy_test.cpp
 native/feria/FeriaAccountStore.cpp native/feria/FeriaBrowserPolicy.cpp
 native/feria/app-state/PorticoAppStateBridge.cpp)
target_include_directories(feria_download_policy_test PRIVATE native/feria)
target_link_libraries(feria_download_policy_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql)
''')
        (args.output / "configure.log").write_text(command([cmake, "-S", str(root), "-B", "build"], root))
        executable = root / "build" / "feria_download_policy_test"
        for name, path, needle, replacement, expected in [("positive", "", "", "", "FERIA_DOWNLOAD_POLICY_PASS")] + CONTROLS:
            for source, text in originals.items():
                (root / source).write_text(text)
            if name != "positive":
                needles = [needle] if isinstance(needle, str) else needle
                replacements = [replacement] if isinstance(replacement, str) else replacement
                text = originals[path]
                for before, after in zip(needles, replacements):
                    if text.count(before) != 1:
                        raise RuntimeError(f"{name}: expected exactly one mutation anchor, found {text.count(before)}")
                    text = text.replace(before, after)
                (root / path).write_text(text)
            (args.output / f"{name}.build.log").write_text(command([cmake, "--build", "build", "--parallel", "2"], root))
            run = subprocess.run([str(executable)], cwd=root, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=45, env=os.environ.copy())
            (args.output / f"{name}.log").write_text(run.stdout)
            passed = expected in run.stdout and ((run.returncode == 0) == (name == "positive"))
            result = {"name": name, "exitCode": run.returncode, "expectedAssertion": expected, "passed": passed}
            results.append(result)
            print(json.dumps(result), flush=True)
            (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            if not passed:
                raise RuntimeError(f"{name}: wrong or missing mutation failure\n{run.stdout}")
    print("FERIA_NATIVE_DOWNLOAD_NEGATIVE_CONTROLS_PASS", len(CONTROLS), flush=True)


if __name__ == "__main__":
    main()
