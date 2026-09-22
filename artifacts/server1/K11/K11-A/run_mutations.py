#!/usr/bin/env python3
"""K11-A behavioral mutation controls.

Each mutant applies one source edit, rebuilds the K11 test binary in an isolated
build directory, and runs the focused checks named for it. A mutant is killed
when any check fails. Every edited file is restored and its SHA-256 is verified
against the pre-mutation bytes, whatever happens.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SRC = ROOT / "native" / "colosseum_server_v1" / "src" / "policy"
ENGINE = SRC / "TorrentEngine.cpp"
REGISTRY = SRC / "EngineRegistry.cpp"
BUILD = HERE / "mutant-build"
RAW = HERE / "raw" / "repair" / "mutants"
VCVARS = os.environ.get(
    "K11_VCVARS",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
CMAKE = os.environ.get("K11_CMAKE", r"C:\Qt\Tools\CMake_64\bin\cmake.exe")
QT = os.environ.get("K11_QT", "C:/Qt/6.11.1/msvc2022_64")
NINJA = os.environ.get("K11_NINJA", "C:/Qt/Tools/Ninja/ninja.exe")

READY_BLOCK = """            emit(EngineEventType::ScopedReady, key, generation);
            for (auto &completion : waiters) {
                if (!current(key, generation)) break;
                completion->deliver(EngineCreateOutcome::Ready, engine);
            }
            if (!current(key, generation)) return;
            emit(EngineEventType::Ready, key, generation);"""
READY_AFTER = """            emit(EngineEventType::ScopedReady, key, generation);
            if (!current(key, generation)) return;
            emit(EngineEventType::Ready, key, generation);
            for (auto &completion : waiters) {
                if (!current(key, generation)) break;
                completion->deliver(EngineCreateOutcome::Ready, engine);
            }"""
EMIT_BEFORE_ID = """        const Value emitted = options;
        options = shallowExtend(options, Value::object({
            {"id", Value::string("-TS0008-native-" + std::to_string(pending.token))},
        }));"""
EMIT_AFTER_ID = """        options = shallowExtend(options, Value::object({
            {"id", Value::string("-TS0008-native-" + std::to_string(pending.token))},
        }));
        const Value emitted = options;"""

# (id, finding, [(file, old, new)], checks)
MUTANTS = [
    ("strand-runs-inline-on-app-lane", "1",
     [(REGISTRY, "        auto self = shared_from_this();\n        if (executor_ && postDeferred(",
       "        auto self = shared_from_this();\n        self->drain();\n        if (false && postDeferred(")],
     ["R1", "K11-02"]),
    ("destruction-delivers-on-destroying-thread", "2",
     [(REGISTRY, "    if (impl->config.callbackExecutor\n        && impl->app->postThroughExecutor("
                 "[impl] { impl->finalize(true); }))\n        return;\n"
                 "    impl->finalize(std::this_thread::get_id() == impl->appThread);",
       "    impl->finalize(true);")],
     ["R2"]),
    ("destruction-rewrites-decided-removal", "2",
     [(REGISTRY, "if (deliver) (*create)->deliverDecided();",
       "if (deliver) (*create)->deliver(EngineCreateOutcome::Cancelled);")],
     ["R2"]),
    ("timer-interval-500", "3",
     [(ENGINE, "constexpr std::uint64_t kRechokeIntervalMs = 10000;",
       "constexpr std::uint64_t kRechokeIntervalMs = 500;")],
     ["R3", "oracle"]),
    ("timer-never-cancelled", "3",
     [(ENGINE, "        if (timer) {\n            timer->cancel();",
       "        if (timer) {\n            static_cast<void>(0);")],
     ["R3", "K11-02", "oracle"]),
    ("timer-ticks-after-close", "3",
     [(ENGINE, "        if (closed) {\n            trace(\"timer-tick-ignored\");\n            return;\n        }\n",
       "")],
     ["R3", "oracle"]),
    ("timer-tick-without-rechoke", "3",
     [(ENGINE, "static_cast<void>(transport->submit(ports::ChokeAction{peer, action.choke}));",
       "static_cast<void>(peer);")],
     ["R3", "oracle"]),
    ("peer-adds-never-drained", "4",
     [(ENGINE, "        if (!peerSearch) return;\n        for (auto &address : peerSearch->takePeerAdds()) {",
       "        if (true) return;\n        for (auto &address : peerSearch->takePeerAdds()) {")],
     ["R4"]),
    ("malformed-port-accepted", "4",
     [(ENGINE, "    if (number == 0 || number > 65535) return std::nullopt;\n", "")],
     ["R4"]),
    ("closed-generation-still-connects", "4",
     [(ENGINE, "            if (state->closing.load() || !state->transport) return;\n"
               "            const bool accepted",
       "            if (!state->transport) return;\n            const bool accepted")],
     ["R4"]),
    ("wire-event-skips-peer-search", "4",
     [(ENGINE, "            peerSearchUpdate();\n            updateSwarmCap();",
       "            updateSwarmCap();")],
     ["R4"]),
    ("staged-bytes-uploadable", "upload",
     [(ENGINE, "        if (circular || virtualPiece >= metadata->geometry().virtualPieces().size()\n"
               "            || committed.count(virtualPiece) == 0) {",
       "        if (circular || virtualPiece >= metadata->geometry().virtualPieces().size()) {"),
      (ENGINE, "return store_.isCommitted(piece) ? store_.read(piece) : std::nullopt;",
       "return store_.read(piece);")],
     ["R5", "K11-02"]),
    ("staged-piece-marked-visible", "upload",
     [(ENGINE, "        if (!result.complete) {\n            static_cast<void>(piece);",
       "        if (!result.complete) {\n            committed.insert(piece);")],
     ["R5", "K11-02", "oracle"]),
    ("scheduler-decision-discarded", "scheduler",
     [(ENGINE, "const auto actions = schedulerActions->decide(context, {candidate}, {});",
       "const std::vector<SchedulerAction> actions{};")],
     ["K11-02"]),
    ("stale-generation-block-accepted", "generation",
     [(ENGINE, "        if (observation.ownership.generation != generation\n"
               "            || observation.ownership.selectionId != request.selectionId",
       "        if (observation.ownership.selectionId != request.selectionId")],
     ["K11-02"]),
    ("callbacks-after-global-ready", "5",
     [(REGISTRY, READY_BLOCK, READY_AFTER)],
     ["oracle"]),
    ("create-event-carries-id", "5",
     [(REGISTRY, EMIT_BEFORE_ID, EMIT_AFTER_ID)],
     ["oracle"]),
    ("reuse-rebinds-swarm-cap", "5",
     [(ENGINE, "        options = std::move(effective);\n        ++resumeCount;",
       "        options = std::move(effective);\n        ++resumeCount;\n"
       "        if (const auto *cap = options.find(\"swarmCap\");\n"
       "            swarmCap && cap && cap->kind() == Value::Kind::Object)\n"
       "            swarmCap->maxSpeed = optionNumber(*cap, \"maxSpeed\");")],
     ["K11-01"]),
    ("create-does-not-resume-swarm", "5",
     [(ENGINE, "        // M172 createEngine calls e.swarm.resume() on every create.\n"
               "        setSwarmPaused(false);\n",
       "")],
     ["K11-01", "R4"]),
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(log: Path) -> int:
    configure = ""
    if not (BUILD / "build.ninja").exists():
        configure = (f'"{CMAKE}" -S "{HERE}" -B "{BUILD}" -G Ninja -DCMAKE_BUILD_TYPE=Release '
                     f'-DCMAKE_PREFIX_PATH={QT} -DCMAKE_MAKE_PROGRAM={NINJA} && ')
    command = (f'call "{VCVARS}" >nul && {configure}"{CMAKE}" --build "{BUILD}" '
               f'--target server1_k11_engine_registry_test -j 6')
    result = subprocess.run(f'cmd /d /s /c "{command}"', capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    return result.returncode


def run_check(check: str, log: Path) -> int:
    exe = BUILD / "server1_k11_engine_registry_test.exe"
    env = dict(os.environ)
    env["PATH"] = QT.replace("/", "\\") + "\\bin;" + env.get("PATH", "")
    if check == "oracle":
        env["K11_ORACLE_DRY_RUN"] = "1"
        command = ["node", str(HERE / "run_oracle.js"), str(exe)]
    else:
        command = [str(exe), check]
    try:
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=300)
        code, output = result.returncode, result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        code, output = 124, "timeout"
    log.write_text(output, encoding="utf-8")
    return code


def main() -> int:
    RAW.mkdir(parents=True, exist_ok=True)
    selected = set(sys.argv[1:])
    originals = {path: path.read_bytes() for path in (ENGINE, REGISTRY)}
    hashes = {path: sha(path) for path in originals}
    results = []
    try:
        for mutant_id, finding, edits, checks in MUTANTS:
            if selected and mutant_id not in selected:
                continue
            try:
                for path, old, new in edits:
                    text = path.read_text(encoding="utf-8")
                    if text.count(old) < 1:
                        raise RuntimeError(f"{mutant_id}: anchor missing in {path.name}")
                    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
                build_code = build(RAW / f"{mutant_id}.build.txt")
                outcomes = {}
                if build_code == 0:
                    for check in checks:
                        outcomes[check] = run_check(check, RAW / f"{mutant_id}.{check}.txt")
                killed = build_code == 0 and any(code != 0 for code in outcomes.values())
                results.append({"id": mutant_id, "finding": finding, "build": build_code,
                                "checks": outcomes, "killed": killed})
                print(json.dumps(results[-1]), flush=True)
            finally:
                for path, data in originals.items():
                    path.write_bytes(data)
                    if sha(path) != hashes[path]:
                        raise RuntimeError(f"{path.name} was not restored exactly")
    finally:
        for path, data in originals.items():
            path.write_bytes(data)
    summary = {"mutants": results,
               "killed": sum(1 for item in results if item["killed"]),
               "total": len(results),
               "sourcesRestored": {path.name: sha(path) == hashes[path] for path in hashes}}
    (HERE / "raw" / "repair" / "MUTATION-RESULTS.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"killed": summary["killed"], "total": summary["total"],
                      "sourcesRestored": summary["sourcesRestored"]}))
    return 0 if summary["killed"] == summary["total"] and all(summary["sourcesRestored"].values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
