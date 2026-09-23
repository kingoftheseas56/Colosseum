"""K12-A mutation controls.

Each mutant breaks one source behaviour in a K12 source file, rebuilds the
K12 test, and runs the focused K12 ctest cases, which compare native output
with the committed source outputs. A mutant is killed when the tests fail.
Every file is restored from its original bytes and checked by SHA-256.

Usage: python run_mutations.py <build-dir>
"""

import hashlib
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SRC = ROOT / "native" / "colosseum_server_v1" / "src" / "policy"
BUILD = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "C:/b/server1-k12-build")
OUT = HERE / "raw" / "mutations"
VCVARS = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
TOOLS = r"C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja"

MUTANTS = [
    ("counter-unknown-decrement-ignored", "EngineCounters.cpp",
     "        Entry entry;\n        entry.nan = true;\n        entries_->emplace(id, std::move(entry));\n        return;",
     "        return;"),
    ("counter-negative-cancels-timer", "EngineCounters.cpp",
     "    if (--found->second.count != 0)\n        return;",
     "    if (--found->second.count != 0) {\n        if (found->second.count < 0 && found->second.timer)\n            found->second.timer->cancel();\n        return;\n    }"),
    ("stream-timeout-read-at-construction", "EngineCounters.cpp",
     "        [impl]() { return impl->streamTimeout; }, timeout);\n    impl->engines",
     "        [value = impl->streamTimeout]() { return value; }, timeout);\n    impl->engines"),
    ("source-default-timeouts", "EngineCounters.cpp",
     "          streamTimeout(config.streamTimeoutMs), engineTimeout(config.engineTimeoutMs)",
     "          streamTimeout(kSourceStreamTimeoutMs), engineTimeout(kSourceEngineTimeoutMs)"),
    ("idle-uses-engine-timeout", "EngineCounters.cpp",
     "                impl->emitJoined(\"engine-idle\", hash, std::nullopt);\n        },\n        [impl]() { return impl->streamTimeout; }, timeout);",
     "                impl->emitJoined(\"engine-idle\", hash, std::nullopt);\n        },\n        [impl]() { return impl->engineTimeout; }, timeout);"),
    ("engine-counter-before-stream-counter", "EngineCounters.cpp",
     "            streams->increment(hash, idx);\n            engines->increment(hash, idx);",
     "            engines->increment(hash, idx);\n            streams->increment(hash, idx);"),
    ("progress-counts-file-pieces-only", "EngineCounters.cpp",
     "        tracker.filePieces = std::ceil(length / pieceLength);",
     "        tracker.filePieces = static_cast<double>(tracker.missing.size() ? tracker.missing.size() : 1);"),
    ("cache-events-not-once-per-file", "EngineCounters.cpp",
     "        if (!flags.second.insert(idx).second)\n            return;",
     "        flags.second.insert(idx);"),
    ("keep-ignores-slice-limit", "EngineCounters.cpp",
     "    candidates.resize(static_cast<std::size_t>(end));",
     "    static_cast<void>(end);"),
    ("keep-resolves-on-first-removal", "EngineCounters.cpp",
     "        const bool last = i + 1 == candidates.size();",
     "        const bool last = i == 0;"),
    ("upload-speed-corrected", "EngineStatistics.cpp",
     "        {\"uploadSpeed\", Value::number(s.swarmDownloadSpeed)},",
     "        {\"uploadSpeed\", Value::number(s.swarmUploadSpeed)},"),
    ("file-stats-natural-key-order", "EngineStatistics.cpp",
     "        object.emplace_back(\"streamProgress\", Value::number(available / filePieces));\n        object.emplace_back(\"streamName\", Value::string(file.name));\n        object.emplace_back(\"streamLen\", Value::number(length));",
     "        object.emplace_back(\"streamLen\", Value::number(length));\n        object.emplace_back(\"streamName\", Value::string(file.name));\n        object.emplace_back(\"streamProgress\", Value::number(available / filePieces));"),
    ("wires-kept-with-index", "EngineStatistics.cpp",
     "        {\"wires\", idx ? Value::null() : Value::array(std::move(wires))},",
     "        {\"wires\", Value::array(std::move(wires))},"),
    ("lenient-file-index", "EngineStatistics.cpp",
     "    if (key.empty() || key.size() > 10 || (key.size() > 1 && key[0] == '0'))",
     "    if (key.empty() || key.size() > 10)"),
    ("number-17-digits", "EngineStatistics.cpp",
     "    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,\n                                      std::chars_format::scientific);",
     "    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,\n                                      std::chars_format::scientific, 16);"),
    ("falsy-alternative-stops-search", "FileSelection.cpp",
     "            if (jsTruthy(entry.element))\n                return result;\n            break;",
     "            return result;"),
    ("guess-loose-season-equality", "FileSelection.cpp",
     "                && strictEquals(infoSeason, season) && infoEpisode.kind() == Value::Kind::Array",
     "                && jsNumber(infoSeason) == jsNumber(season) && infoEpisode.kind() == Value::Kind::Array"),
    ("media-extension-literal-dot", "FileSelection.cpp",
     "        const auto before = static_cast<unsigned char>(path[at - 1]);\n        if (before == '\\n' || before == '\\r')",
     "        const auto before = static_cast<unsigned char>(path[at - 1]);\n        if (before != '.')"),
    ("m304-constructor-not-excluded", "FileSelection.cpp",
     "    return w == \"constructor\" || w == \"__proto__\";",
     "    return false;"),
    ("m304-stamp-split-not-reversed", "FileSelection.cpp",
     "        std::reverse(firstNameSplit.begin(), firstNameSplit.end()); // in place, as the source",
     ""),
    ("m304-episode-lookbehind-dropped", "FileSelection.cpp",
     "            && (!isWord(prev) || isDigit(prev))) {",
     ") {"),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log):
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    log.write_text((result.stdout or "") + (result.stderr or ""), encoding="utf-8")
    return result.returncode


def build(log):
    return run(f'call "{VCVARS}" >nul && set PATH={TOOLS};%PATH% && '
               f'cmake --build "{BUILD}" --target server1_k12_enginefs_lifecycle_test', log)


def focused(log):
    env_path = r"C:\Qt\Tools\CMake_64\bin"
    return run(f'set PATH={env_path};%PATH% && ctest --test-dir "{BUILD}" -L K12 --output-on-failure -j 1', log)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    originals = {name: (SRC / name).read_bytes() for name in {m[1] for m in MUTANTS}}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in originals.items()}
    results = []
    baseline_build = build(OUT / "baseline.build.txt")
    baseline = focused(OUT / "baseline.ctest.txt")
    if baseline_build != 0 or baseline != 0:
        print("baseline not green", baseline_build, baseline)
        return 2
    try:
        for name, filename, old, new in MUTANTS:
            path = SRC / filename
            text = originals[filename].decode("utf-8")
            if text.count(old) != 1:
                results.append({"mutant": name, "state": "anchor-missing"})
                continue
            path.write_bytes(text.replace(old, new).encode("utf-8"))
            built = build(OUT / f"{name}.build.txt")
            if built != 0:
                state = "build-failed"
            else:
                state = "killed" if focused(OUT / f"{name}.ctest.txt") != 0 else "SURVIVED"
            path.write_bytes(originals[filename])
            results.append({"mutant": name, "file": filename, "state": state})
            print(name, state, flush=True)
    finally:
        for name, data in originals.items():
            (SRC / name).write_bytes(data)
    restored = all(sha(SRC / name) == digest for name, digest in hashes.items())
    rebuilt = build(OUT / "restored.build.txt") == 0 and focused(OUT / "restored.ctest.txt") == 0
    killed = sum(1 for r in results if r["state"] == "killed")
    summary = {"mutants": len(MUTANTS), "killed": killed, "results": results,
               "sourcesRestored": restored, "restoredGreen": rebuilt, "sourceSha256": hashes}
    (OUT / "MUTATION-RESULTS.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    print(f"killed {killed}/{len(MUTANTS)} restored={restored} restoredGreen={rebuilt}")
    return 0 if killed == len(MUTANTS) and restored and rebuilt else 1


if __name__ == "__main__":
    sys.exit(main())
