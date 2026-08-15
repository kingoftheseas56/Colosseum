#!/usr/bin/env python3
"""
Colosseum Guardian Loop - the LIVE stage runners (batched runtime pass wiring).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md +
docs/autorepair/batched-runtime-pass.md. Every pure module (policy.py G1, sandbox.py G2,
incident.py G3, triage.py G4, diagnosis.py G5, repair_contract.py G6, verify.py G7,
promotion.py G8, orchestrator.py G9) is code-complete and 339-test green; each one's own
`default_invoke`/`default_run_once`/`DEFAULT_STAGE_RUNNERS` seam raises NotImplementedError
by design, naming exactly what a LIVE implementation must do. This module IS that live
implementation - it drives real sandboxes, real headless `claude -p` sessions, a real
`git`/`gh` sequence, and returns exactly the dict shapes each pure module's own
`run_*()`/`triage()`/`diagnose()`/`promote()` orchestration entry expects from its `invoke`
argument. It never reimplements any pure module's own logic - it only supplies the live half
each one already named as deferred.

Isolation dies at the swap (house law): this module is a separate file from every pure
module above. It imports them, drives them, and returns their own dicts verbatim to the
orchestrator - it does not fork their control flow or duplicate their gates.

## The five stage_runners entries

`build_live_stage_runners(...)` returns a dict with keys "triage", "diagnosis", "repair",
"verify", "promotion" - each a callable matching orchestrator.py's own stage-runner
signature: `(incident: dict, incident_dir: Path, policy_obj: Policy, *, prior: dict) -> dict`.
`orchestrator.run_incident(incident_dir, stage_runners=build_live_stage_runners())` (or the
CLI at the bottom of this file) drives a real incident through all five live stages.

  triage     -> sandbox.create()/build()/provision() (or REUSE an already-built clone - see
                "Sandbox reuse" below), then k tagged `lanista session run` reproductions of
                reproduce.ps1 in that ONE sandbox, parsed via incident.parse_failure_log()
                (reused, never reimplemented) into triage.RunResult, fed through
                triage.classify_triage() via triage.triage()'s own orchestration. NO model
                call (D4).
  diagnosis  -> headless `claude -p --model opus --allowedTools Read,Grep,Glob`, guard hook
                wired via a generated settings.json, `--add-dir` = sandbox + incident dir +
                docs/encyclopedia/, prompt = incident evidence + "read the encyclopedia guide
                first, then answer in exactly this diagnosis.json shape." Feeds the model's
                answer through diagnosis.diagnose()'s own validate_diagnosis()/
                check_citations()/check_forbidden_escalation()/may_proceed_to_repair() gates.
  repair     -> headless `claude -p --model sonnet --allowedTools
                Read,Grep,Glob,Edit,Write,Bash` (Bash under the SAME guard hook), sandbox
                reset to baseSha before each attempt, contract text + prior rejection
                verbatim on retry. The declared bug test MUST be a `lanista session run
                <scenario>` the patch itself ADDED under tests/lanista_scenarios/ (the
                batched-runtime-pass audit fix - ctest cannot serve, a repair cannot register
                a new ctest target). Red/green: two REAL scratch sandboxes (test-adds-only,
                then +production), each built+provisioned, bug test run 2x in each. Feeds the
                result through repair_contract.run_repair()'s own gates.
  verify     -> a SECOND, independently-built pristine sandbox from baseSha alone, `git
                apply` the patch, every mechanical gate run for real (red/green re-proof,
                original reproduce.ps1 now-green, `ctest -L unit`, warning gate, risk-class
                journeys, both `ctest -N` inventory counts, the ONE orchestrator-side
                diagnosis.json read for the citation-intersection gate), then headless
                `claude -p --model opus` (read-only, context = verify.build_verifier_context()
                output, never the incident dir - see "Ruling 4, mechanized" below) + one
                best-effort GLM refutation (advisory). Feeds the result through
                verify.run_verify()'s own gates.
  promotion  -> a git fetch + rebase-onto-master attempt (A6), a TEMP WORKTREE of MAIN
                branched `autorepair/AR-<id>` from baseSha (Rule-28 carve-out, never checking
                out over the dirty main tree), apply + one commit + push, `gh pr create
                --draft` with the assembled 12-item dossier. A `gh` failure pushes the branch
                and writes the body to a file, reported honestly as Bridge-blocked, never a
                silently-claimed success. Feeds the result through promotion.promote()'s own
                idempotence/guard checks.

## Sandbox reuse (the audit's "detect, don't rebuild" fix)

The golden incident's baseSha (353b6757f812b5453040d0f313477e85253d9263, short 353b675) is
ALREADY built and Runtime-validated at `C:\\arsbx\\g2-live-proof` (docs/autorepair/
batched-runtime-pass.md: "Build ONE sandbox once and reuse it across G2/G4/G5/G6/G7 - do not
rebuild per proof"). `find_or_build_sandbox()` below generalizes that rule: before ever
calling sandbox.create()/build()/provision(), it checks whether a clone ALREADY exists at
the expected location (either the well-known golden reuse dir, when its own HEAD sha matches
the incident's baseSha, or `<sandbox_root>/<incident_id>` left over from an earlier stage of
THIS SAME incident) and is genuinely usable (zero remotes, HEAD at the right sha, a built +
provisioned exe) before falling back to a real cold create()/build()/provision(). This is
the ONE sandbox triage/diagnosis/repair share (D5-adjacent: repair's own attempts reset THIS
sandbox's working tree to baseSha rather than re-cloning). Verify (D7) is the deliberate
exception - it ALWAYS builds its own second, independent clone; see `_stage_verify()`.

## The guard hook, wired for real

`write_guard_settings()` generates a real Claude Code `settings.json` with a PreToolUse hook
entry invoking `scripts/autorepair/hooks/guard.py --sandbox-root <clone>` (G4's own proven
mechanism - see hooks/guard.py's own docstring). Every headless session this module launches
that can touch the filesystem (diagnosis's read-only tools, repair's Bash) is launched with
`--settings <that file>` and `cwd` pinned inside the sandbox clone.

## GLM refutation - an honest limitation

policy.verifierRefutation names provider "glm" - the brotherhood's `glm`/`deepseek` delegates
are MCP tools reachable from an AGENT session, not from a bare headless Python subprocess (no
`glm`/`deepseek` CLI binary is on PATH in this environment - confirmed empirically while
building this module: `where glm` and `where deepseek` both resolve to nothing). Since
policy.verifierRefutation.advisory is True (it never vetoes), `run_glm_refutation()` below is
a best-effort seam: it shells out to a `glm` CLI if one is ever added to PATH, and otherwise
returns an honest `{"available": False, "reason": ...}` record rather than fabricating a
refutation or blocking the pipeline on a missing delegate. Flagged plainly here, not silently
skipped - see this module's own execution report for the open-risk write-up.

Stdlib only (house pattern), except that this module is the one place in scripts/autorepair/
that DOES shell out to `claude`/`git`/`gh`/`ctest`/`pwsh` for real - every sibling pure module
keeps that behind an injectable seam this module now fills.

Public API:

    LiveRunnerError, ClaudeInvocationError                      # named refusals
    find_or_build_sandbox(incident, *, policy_obj, ...) -> (Path, bool)
    write_guard_settings(dest_dir, sandbox_root, *, repo_root=REPO_ROOT) -> Path
    run_headless_claude(prompt, *, cwd, model, allowed_tools, add_dirs,
                         settings_path, claude_cli="claude", timeout_sec) -> dict
    run_glm_refutation(patch_summary, *, thinking="high") -> dict
    extract_patch_text(clone, base_sha) -> str
    make_live_triage_run_once(incident, clone) -> Callable[[int, dict], RunResult]
    live_diagnosis_invoke(incident, sandbox_root, *, model) -> dict
    live_repair_invoke(incident, diagnosis, sandbox_root, *, attempt,
                        prior_rejection, model) -> dict
    live_verify_invoke(incident, patch, base_sha, sandbox_root, *,
                        model, refutation) -> dict
    live_promotion_invoke(incident, patch, verdict, dossier, base_sha, *,
                           target_branch, pr_body) -> dict
    build_live_stage_runners(*, repo_root=REPO_ROOT, artifacts_root=...,
                              sandbox_root=..., claude_cli="claude",
                              gh_cli="gh", jobs=1) -> dict[str, Callable]
    main(argv=None) -> int         # --from-run RUN_DIR | --incident ID --resume

Usage:
    python scripts/autorepair/live_runners.py --from-run artifacts/lanista-sessions/<id>
    python scripts/autorepair/live_runners.py --incident AR-2026-08-14-0001 --resume
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable

# scripts/autorepair/live_runners.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling imports (house pattern: flat scripts/autorepair/, no package __init__.py - see
# every other module's identical sys.path setup). This module is the one place that DOES
# call every sibling's own real, previously-deferred orchestration entry point.
_THIS_DIR = Path(__file__).resolve().parent
_HOOKS_DIR = _THIS_DIR / "hooks"
for _p in (_THIS_DIR, _HOOKS_DIR):
    _p_str = str(_p)
    if _p_str not in sys.path:
        sys.path.insert(0, _p_str)

from policy import Policy, load_policy  # noqa: E402
import sandbox  # noqa: E402
import incident as incident_mod  # noqa: E402
import triage as triage_mod  # noqa: E402
import diagnosis as diagnosis_mod  # noqa: E402
import repair_contract  # noqa: E402
import verify as verify_mod  # noqa: E402
import promotion as promotion_mod  # noqa: E402
import orchestrator  # noqa: E402
from guard import canonicalize  # noqa: E402

__all__ = [
    "REPO_ROOT",
    "GOLDEN_REUSE_DIR",
    "LiveRunnerError",
    "ClaudeInvocationError",
    "find_or_build_sandbox",
    "write_guard_settings",
    "run_headless_claude",
    "run_glm_refutation",
    "extract_patch_text",
    "make_live_triage_run_once",
    "live_diagnosis_invoke",
    "live_repair_invoke",
    "live_verify_invoke",
    "live_promotion_invoke",
    "build_live_stage_runners",
    "main",
]


class LiveRunnerError(RuntimeError):
    """Base for every clean, named live-runner refusal (never a bare traceback)."""


class ClaudeInvocationError(LiveRunnerError):
    """A headless `claude -p` call failed, timed out, or returned unparsable output."""


# The already-built, Runtime-validated golden-incident sandbox (docs/autorepair/
# batched-runtime-pass.md). "Detect and reuse" checks this FIRST before ever cloning fresh.
# 2026-08-16: the golden's contents were PROMOTED in place - incident AR-2026-08-15-0004's
# built sandbox (de18d5d) was moved over the retired 353b675 build when the frontier
# advanced, so the pinned sha moves with the fixture it names.
GOLDEN_REUSE_DIR = Path("C:/arsbx/g2-live-proof")
GOLDEN_BASE_SHA = "de18d5d5801a1305b31c1e7f62146fc5e7a3c25f"

CLAUDE_CLI_DEFAULT = "claude"
GH_CLI_DEFAULT = "gh"
CTEST_EXE = sandbox.CMAKE_EXE.parent / "ctest.exe"

# The exact `sandbox.provision()`-deployed marker files that prove a clone is BUILT and
# RUNTIME-READY, not merely configured (G0's own residual proof criterion).
_BUILT_MARKERS = ("native/build-msvc/colosseum.exe", "native/build-msvc/MpvQt.dll")

# A4/G6 belt-and-braces: defeat any QML disk cache from masking a genuine red/green
# difference between runs (every red/green session below sets this).
_RED_GREEN_ENV_EXTRA = {"QML_DISABLE_DISK_CACHE": "1"}


# ══════════════════════════════════════════════════════════════════════════
# small subprocess/git helpers
# ══════════════════════════════════════════════════════════════════════════


def run_captured(cmd: list[str], *, cwd: Path | str | None = None,
                 timeout: int | None = None, check: bool = False,
                 env: dict[str, str] | None = None,
                 input_text: str | None = None) -> subprocess.CompletedProcess:
    """subprocess.run(capture_output=True) replacement immune to the grandchild-pipe
    freeze (proven live twice on 2026-08-15/16: every colosseum boot spawns
    stremio-runtime, which INHERITS the child's stdout pipe handle and outlives both
    lanista.exe and colosseum.exe; subprocess.run's post-kill pipe drain then blocks
    FOREVER - its timeout cannot fire through a grandchild-held write end, which froze
    the Guardian's triage once and the Night Watch itself once). Output goes to temp
    FILES instead of pipes: proc.wait() waits on the process handle only, so neither
    the child's exit NOR a timeout can be wedged by inherited handles. Same return
    contract as subprocess.run (CompletedProcess with text stdout/stderr; raises
    TimeoutExpired after killing the child, with whatever partial output the files
    hold). stdin is a temp file when input_text is given, DEVNULL otherwise - a child
    that reads stdin never hangs the caller on a tty."""
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as out_f, \
         tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as err_f, \
         tempfile.TemporaryFile(mode="w+", encoding="utf-8") as in_f:
        stdin_arg: Any = subprocess.DEVNULL
        if input_text is not None:
            in_f.write(input_text)
            in_f.seek(0)
            stdin_arg = in_f
        proc = subprocess.Popen(
            cmd, cwd=str(cwd) if cwd is not None else None,
            stdout=out_f, stderr=err_f, stdin=stdin_arg, env=env,
        )
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:  # kill refused (debugger-held?): report honestly
                pass
            out_f.seek(0)
            err_f.seek(0)
            raise subprocess.TimeoutExpired(cmd, timeout, output=out_f.read())
        out_f.seek(0)
        err_f.seek(0)
        stdout_text, stderr_text = out_f.read(), err_f.read()
    if check and proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd, stdout_text, stderr_text)
    return subprocess.CompletedProcess(cmd, proc.returncode, stdout_text, stderr_text)


def _run(cmd: list[str], *, cwd: Path | str | None = None, timeout: int | None = None,
          check: bool = False, env: dict[str, str] | None = None,
          input_text: str | None = None) -> subprocess.CompletedProcess:
    return run_captured(
        cmd, cwd=cwd, timeout=timeout, check=check, env=env, input_text=input_text,
    )


def _git(cwd: Path, *args: str, timeout: int = 60) -> subprocess.CompletedProcess:
    return _run(["git", *args], cwd=cwd, timeout=timeout)


def _git_output_or_none(cwd: Path, *args: str) -> str | None:
    try:
        result = _git(cwd, *args)
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


# ══════════════════════════════════════════════════════════════════════════
# Sandbox reuse detection (the "detect, don't rebuild" fix)
# ══════════════════════════════════════════════════════════════════════════


def _sha_matches(candidate_head: str | None, base_sha: str) -> bool:
    if not candidate_head:
        return False
    a, b = candidate_head.lower(), base_sha.lower()
    shortest = min(len(a), len(b))
    return shortest >= 7 and a[:shortest] == b[:shortest]


def _looks_built(clone: Path) -> bool:
    return all((clone / rel).is_file() for rel in _BUILT_MARKERS)


def _sandbox_reusable_at(clone: Path, base_sha: str) -> bool:
    """A clone is reusable iff: it exists, has zero git remotes (D5/A1 - never a clone we'd
    accidentally push through), its HEAD matches `base_sha`, and it is already built +
    provisioned (colosseum.exe + MpvQt.dll present - the G0 runtime-DLL proof). Any single
    failing check means "build fresh," never a guess."""
    if not clone.is_dir() or not (clone / ".git").exists():
        return False
    remotes = _git_output_or_none(clone, "remote")
    if remotes:
        return False
    head = _git_output_or_none(clone, "rev-parse", "HEAD")
    if not _sha_matches(head, base_sha):
        return False
    return _looks_built(clone)


def find_or_build_sandbox(
    incident: dict[str, Any],
    *,
    policy_obj: Policy | None = None,
    sandbox_root: Path | str = sandbox.DEFAULT_SANDBOX_ROOT,
    main_repo: Path | str = REPO_ROOT,
    golden_reuse_dir: Path | str = GOLDEN_REUSE_DIR,
    incident_id_suffix: str = "",
    jobs: int = 1,
) -> tuple[Path, bool]:
    """
    Returns `(clone_path, reused)`. Checks, in order:

      1. `golden_reuse_dir` (default C:\\arsbx\\g2-live-proof) - reusable iff its own HEAD
         matches `incident['baseSha']` per `_sandbox_reusable_at()`. This is the golden
         incident's own already-built, Runtime-validated clone; reusing it is the whole
         point of the batched-runtime-pass "build once" rule.
      2. `<sandbox_root>/<incident['id']><incident_id_suffix>` - a clone a PRIOR stage of
         THIS SAME incident may already have built (triage builds it; diagnosis/repair reuse
         it by finding it here on their own, separate invocation).
      3. Otherwise: a real `sandbox.create()` + `sandbox.build(jobs=jobs)` +
         `sandbox.provision()` at that same `<sandbox_root>/<incident['id']><suffix>` path -
         the one real cold-build path, reused (never reimplemented) from sandbox.py (G2).

    Raises LiveRunnerError (never returns a half-built clone) if build()/provision() fails.
    """
    base_sha = incident["baseSha"]
    golden_dir = Path(golden_reuse_dir)
    if not incident_id_suffix and _sandbox_reusable_at(golden_dir, base_sha):
        return golden_dir, True

    root = Path(sandbox_root)
    candidate = (root / f"{incident['id']}{incident_id_suffix}").resolve()
    if _sandbox_reusable_at(candidate, base_sha):
        return candidate, True

    if candidate.exists():
        raise LiveRunnerError(
            f"sandbox path {candidate} already exists but is not reusable (wrong sha, "
            "still has a remote, or not yet built) - refusing to silently overwrite it; "
            "destroy it by hand first if it is genuinely stale"
        )

    clone = sandbox.create(
        base_sha, f"{incident['id']}{incident_id_suffix}", main_repo=main_repo, sandbox_root=root,
    )
    build_result = sandbox.build(clone, jobs=jobs)
    if not build_result.ok:
        raise LiveRunnerError(
            f"sandbox build failed for {clone} (see {build_result.log_path}): "
            f"returncode={build_result.returncode} error_lines={build_result.error_lines!r} "
            f"timed_out={build_result.timed_out}"
        )
    sandbox.provision(clone, main_repo=main_repo)
    return clone, False


# ══════════════════════════════════════════════════════════════════════════
# The guard hook, wired for real (settings.json generation)
# ══════════════════════════════════════════════════════════════════════════

GUARD_HOOK_SCRIPT = _HOOKS_DIR / "guard.py"


def write_guard_settings(dest_dir: Path | str, sandbox_root: Path | str, *,
                          repo_root: Path | str = REPO_ROOT,
                          python_exe: str = sys.executable) -> Path:
    """
    Writes a real Claude Code `settings.json` (G4's own proven PreToolUse mechanism - see
    hooks/guard.py's own docstring) wiring `scripts/autorepair/hooks/guard.py --sandbox-root
    <sandbox_root>` as a PreToolUse hook covering every tool (matcher "" - Claude Code's own
    "match every tool" convention), so the hook decides on Read/Write/Edit/MultiEdit/Glob/
    Grep/Bash calls alike, never just one tool name. Returns the written file's path;
    `dest_dir` is created if missing.
    """
    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    command = f'"{python_exe}" "{GUARD_HOOK_SCRIPT}" --sandbox-root "{Path(sandbox_root)}"'
    settings = {
        "hooks": {
            "PreToolUse": [
                {
                    "matcher": "",
                    "hooks": [{"type": "command", "command": command}],
                }
            ]
        }
    }
    settings_path = dest_dir / "settings.json"
    settings_path.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
    return settings_path


# ══════════════════════════════════════════════════════════════════════════
# Headless claude invocation
# ══════════════════════════════════════════════════════════════════════════


def _claude_argv(*, claude_cli: str, model: str, allowed_tools: list[str],
                  add_dirs: list[Path | str], settings_path: Path | str) -> list[str]:
    """Pure command-line assembly (no subprocess) - separated so a test can assert the exact
    shape without ever launching a real `claude` process. The prompt itself is NEVER an argv
    element (Windows argv-length safety, and it can carry newlines/quotes freely); it is
    always piped via stdin by `run_headless_claude()` below."""
    argv = [
        claude_cli, "-p",
        "--model", model,
        "--allowedTools", ",".join(allowed_tools),
        "--output-format", "json",
        "--settings", str(settings_path),
    ]
    for add_dir in add_dirs:
        argv += ["--add-dir", str(add_dir)]
    return argv


def _extract_json_object(text: str) -> dict[str, Any]:
    """Best-effort JSON-object extraction from a model's free-form final answer: try a
    straight `json.loads()` first (the prompt always asks for JSON ONLY), then fall back to
    a fenced ```json ... ``` block, then the largest brace-balanced `{...}` span found
    anywhere in the text. Raises ClaudeInvocationError (never returns a guessed/partial
    dict) if none of the three shapes parse."""
    text = text.strip()
    try:
        obj = json.loads(text)
        if isinstance(obj, dict):
            return obj
    except json.JSONDecodeError:
        pass

    fence = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.DOTALL)
    if fence:
        try:
            obj = json.loads(fence.group(1))
            if isinstance(obj, dict):
                return obj
        except json.JSONDecodeError:
            pass

    start = text.find("{")
    end = text.rfind("}")
    if start != -1 and end != -1 and end > start:
        try:
            obj = json.loads(text[start:end + 1])
            if isinstance(obj, dict):
                return obj
        except json.JSONDecodeError:
            pass

    raise ClaudeInvocationError(
        f"could not extract a JSON object from the model's final answer (first 300 chars): "
        f"{text[:300]!r}"
    )


def run_headless_claude(
    prompt: str,
    *,
    cwd: Path | str,
    model: str,
    allowed_tools: list[str],
    add_dirs: list[Path | str],
    settings_path: Path | str,
    claude_cli: str = CLAUDE_CLI_DEFAULT,
    timeout_sec: int = 3600,
) -> dict[str, Any]:
    """
    The one real `claude -p` launcher every stage's live invoke() below calls through.
    `cwd` is pinned inside the sandbox (or, for verify's Verifier, the verify sandbox); the
    prompt is piped via stdin (never an argv element); `--output-format json` wraps the
    model's final answer in Claude Code's own result envelope, whose own `"result"` field
    (the model's final text) is fed through `_extract_json_object()` to recover the
    structured answer the prompt asked for (diagnosis.json/verdict.json/bugtest shape).

    Raises ClaudeInvocationError on a nonzero exit, a timeout, an unparsable envelope, or an
    envelope whose own `"is_error"` is true - never returns a fabricated result.
    """
    # Backend seam (see run_brain_file_handoff below): when GUARDIAN_BRAIN=handoff (or
    # set_model_backend("handoff") was called by the embedding watch), this launcher
    # delegates the WHOLE call to the file handoff instead of spawning a CLI - one
    # choke point, so every stage invoke (diagnosis/repair/verify) inherits it.
    if _resolved_backend() == "handoff":
        return run_brain_file_handoff(
            prompt, cwd=cwd, model=model, allowed_tools=allowed_tools,
            add_dirs=add_dirs, settings_path=settings_path,
            claude_cli=claude_cli, timeout_sec=timeout_sec,
        )
    argv = _claude_argv(
        claude_cli=claude_cli, model=model, allowed_tools=allowed_tools,
        add_dirs=add_dirs, settings_path=settings_path,
    )
    try:
        proc = _run(argv, cwd=cwd, timeout=timeout_sec, input_text=prompt)
    except subprocess.TimeoutExpired as exc:
        raise ClaudeInvocationError(
            f"headless claude timed out after {timeout_sec}s: {' '.join(argv)}"
        ) from exc
    except OSError as exc:
        raise ClaudeInvocationError(f"could not launch headless claude ({argv[0]!r}): {exc}") from exc

    if proc.returncode != 0:
        raise ClaudeInvocationError(
            f"headless claude exited {proc.returncode}: {' '.join(argv)}\n"
            f"--- stdout ---\n{proc.stdout[-4000:]}\n--- stderr ---\n{proc.stderr[-4000:]}"
        )

    try:
        envelope = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise ClaudeInvocationError(
            f"headless claude's --output-format json stdout did not parse: {exc}\n"
            f"stdout (last 2000 chars): {proc.stdout[-2000:]}"
        ) from exc

    if envelope.get("is_error"):
        raise ClaudeInvocationError(f"headless claude reported an error result: {envelope!r}")

    result_text = envelope.get("result")
    if not isinstance(result_text, str) or not result_text.strip():
        raise ClaudeInvocationError(f"headless claude's envelope carries no result text: {envelope!r}")

    return _extract_json_object(result_text)


# ══════════════════════════════════════════════════════════════════════════
# GLM brain file handoff - the model backend for "the thinking brain is the
# GLM agent session itself" (Hemanth directive 2026-08-15: "opus doesn't think,
# opus reviews after the entire process is done, the thinking brain shifts over
# to you"). Proven live in the D10 dress rehearsal via a throwaway monkeypatch;
# promoted here to a first-class backend so the standing Night Watch can run
# the same way without out-of-repo shims.
# ══════════════════════════════════════════════════════════════════════════


_MODEL_BACKENDS = {"claude", "handoff"}
_model_backend: str | None = None  # resolved lazily from GUARDIAN_BRAIN, then sticky


def set_model_backend(name: str) -> None:
    """Select how every stage's model call is served. 'claude' (default): the real
    headless `claude -p` launcher below. 'handoff': run_brain_file_handoff() - the
    prompt is written to artifacts/glm-brain/call-NNN/ and the process BLOCKS until
    an answering mind (the GLM agent session driving the watch) drops answer.json
    there. The two tiers stay honest: policy.modelRouting names WHO should think
    (e.g. 'glm'); the backend names HOW the call is served."""
    global _model_backend
    if name not in _MODEL_BACKENDS:
        raise ClaudeInvocationError(
            f"unknown model backend {name!r}; expected one of {sorted(_MODEL_BACKENDS)}"
        )
    _model_backend = name


def _resolved_backend() -> str:
    global _model_backend
    if _model_backend is None:
        _model_backend = os.environ.get("GUARDIAN_BRAIN", "claude").strip().lower() or "claude"
        if _model_backend not in _MODEL_BACKENDS:
            raise ClaudeInvocationError(
                f"GUARDIAN_BRAIN={_model_backend!r} is not one of {sorted(_MODEL_BACKENDS)}"
            )
    return _model_backend


def _next_brain_call_dir() -> Path:
    root = REPO_ROOT / "artifacts" / "glm-brain"
    root.mkdir(parents=True, exist_ok=True)
    n = 1
    while (root / f"call-{n:03d}").exists():
        n += 1
    d = root / f"call-{n:03d}"
    d.mkdir()
    return d


def run_brain_file_handoff(
    prompt: str,
    *,
    cwd: Path | str,
    model: str,
    allowed_tools: list[str],
    add_dirs: list[Path | str],
    settings_path: Path | str,
    claude_cli: str = CLAUDE_CLI_DEFAULT,
    timeout_sec: int = 3600,
) -> dict[str, Any]:
    """File-handoff model backend (same signature as run_headless_claude, so the
    delegation in it is a drop-through). Writes prompt.txt + context.json into a
    fresh artifacts/glm-brain/call-NNN/ directory, then polls (5 s) for answer.json
    until the caller's own timeout_sec deadline - the SAME bound the claude backend
    and the orchestrator's per-stage caps impose, deliberately NOT extended: a brain
    too slow to answer inside its stage budget is an honest BUDGET terminal, never
    a fabricated answer. An EMPTY answer.json is the sanctioned {} (a no-op the
    stage's own gates judge); anything else must parse as a JSON object or the call
    fails loudly. The call dir is the audit record: which prompt, which context,
    which backend served it, what came back."""
    d = _next_brain_call_dir()
    (d / "prompt.txt").write_text(prompt, encoding="utf-8")
    (d / "context.json").write_text(
        json.dumps(
            {
                "cwd": str(cwd),
                "model": model,
                "allowedTools": list(allowed_tools),
                "addDirs": [str(p) for p in add_dirs],
                "settings": str(settings_path),
                "backend": "file-handoff",
                "note": (
                    "GLM brain: do this prompt's tool work yourself in the named dirs, "
                    "then write answer.json in THIS directory."
                ),
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    answer = d / "answer.json"
    deadline = time.monotonic() + max(timeout_sec, 60)
    while not answer.is_file():
        if time.monotonic() > deadline:
            raise ClaudeInvocationError(
                f"brain file handoff timed out after {timeout_sec}s waiting for {answer}"
            )
        time.sleep(5)
    text = answer.read_text(encoding="utf-8").strip()
    if not text:
        return {}  # sanctioned empty answer: the stage gates judge the no-op honestly
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ClaudeInvocationError(
            f"brain handoff answer.json is not valid JSON ({answer}): {exc}"
        ) from exc
    if not isinstance(parsed, dict):
        raise ClaudeInvocationError(
            f"brain handoff answer.json must be a JSON object, got {type(parsed).__name__} ({answer})"
        )
    return parsed


# ══════════════════════════════════════════════════════════════════════════
# GLM refutation - best-effort, advisory, honest about unavailability
# ══════════════════════════════════════════════════════════════════════════


def run_glm_refutation(patch_summary: str, *, thinking: str = "high",
                        glm_cli: str = "glm", timeout_sec: int = 600) -> dict[str, Any]:
    """
    policy.verifierRefutation: one single-shot GLM refutation, advisory (never a veto). No
    `glm`/`deepseek` CLI is on PATH in this environment (only the MCP delegate tools are,
    reachable from an agent session, not a bare subprocess - see the module docstring's own
    "GLM refutation" section). Best-effort: if a `glm` CLI is ever added to PATH, this shells
    out to it with the patch summary and `thinking` depth; otherwise it returns an HONEST
    `{"available": False, ...}` record rather than fabricating a refutation or raising (the
    caller's `advisory: True` policy already means a missing refutation must never block).
    """
    if shutil.which(glm_cli) is None:
        return {
            "available": False,
            "provider": "glm",
            "thinking": thinking,
            "reason": (
                f"no {glm_cli!r} CLI found on PATH in this environment - the brotherhood's "
                "GLM delegate is an MCP tool reachable from an agent session, not from a "
                "bare headless subprocess; advisory only, so the pipeline proceeds without it"
            ),
        }
    try:
        proc = _run(
            [glm_cli, "--thinking", thinking],
            timeout=timeout_sec, input_text=patch_summary,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return {"available": False, "provider": "glm", "thinking": thinking, "reason": str(exc)}
    if proc.returncode != 0:
        return {
            "available": False, "provider": "glm", "thinking": thinking,
            "reason": f"glm CLI exited {proc.returncode}: {proc.stderr[-1000:]}",
        }
    return {"available": True, "provider": "glm", "thinking": thinking, "output": proc.stdout.strip()}


# ══════════════════════════════════════════════════════════════════════════
# Patch extraction (the first sandbox's own attempt-ledger history)
# ══════════════════════════════════════════════════════════════════════════


def extract_patch_text(clone: Path | str, base_sha: str) -> str:
    """The accepted repair's own patch, as `git diff <base_sha> HEAD` inside the first
    (repair) sandbox - repair_contract.py's own docstring: "the sandbox's own git history is
    the attempt ledger." Since every attempt resets the working tree to `base_sha` before
    committing its own candidate (see `live_repair_invoke()`), the LAST commit on HEAD is
    always the accepted attempt (run_repair() stops attempting the moment one is accepted),
    so this diff is exactly that attempt's own full candidate patch, never a stack of prior
    rejected attempts."""
    clone = Path(clone).resolve()
    result = _git(clone, "diff", base_sha, "HEAD")
    if result.returncode != 0:
        raise LiveRunnerError(f"git diff {base_sha} HEAD failed in {clone}: {result.stderr}")
    return result.stdout


# ══════════════════════════════════════════════════════════════════════════
# Stage 1: triage - k tagged reproduce.ps1 runs, zero model calls (D4)
# ══════════════════════════════════════════════════════════════════════════


def _reproduce_command(incident: dict[str, Any], clone: Path, *, tag: str) -> list[str]:
    """Rebuilds reproduce.ps1's own `lanista session run` invocation (incident.py's own
    _reproduce_script()), but pointed at the SANDBOX clone's own lanista.exe/colosseum.exe/
    qml, never the main repo's - the whole point of running it inside a disposable clone.
    `--seed`, if present, stays wherever incident.json recorded it (a read-only fixture
    directory, not sandbox-contained - only the exe/qml/lanista under test need to be the
    sandbox's own copies) and `--tag` is overridden per-run so k reproductions never share
    one isolated session's appData/cache root."""
    lanista_exe = clone / "native" / "build-msvc" / "lanista.exe"
    colosseum_exe = clone / "native" / "build-msvc" / "colosseum.exe"
    qml = incident.get("qml") or "qml/Main.qml"
    cmd = [
        str(lanista_exe), "session", "run", incident["scenario"],
        "--exe", str(colosseum_exe),
        "--qml", qml,
        "--tag", tag,
    ]
    if incident.get("seed"):
        cmd += ["--seed", str(incident["seed"])]
    if incident.get("drive"):
        cmd.append("--drive")
    if incident.get("readyMs"):
        cmd += ["--ready-ms", str(incident["readyMs"])]
    cmd.append("--verbose")
    return cmd


def _classify_reproduce_run(incident: dict[str, Any], raw_stdout: str, cmd: list[str]) -> triage_mod.RunResult:
    """Reuses incident.parse_failure_log() (never reimplemented) - lanista's `session run`
    delegates to the SAME runScenario() plain `run` uses (native/tools/lanista.cpp), so its
    stdout carries the identical PASS/FAIL/INFRA step-line format incident.py already parses
    for a post-mortem failure.log. incident.parse_failure_log() requires the invocation
    header convention ("$ <command>" as the first non-empty line - the minting-step
    convention, not lanista's own output) so it is synthesized here before parsing."""
    label = incident["failingStep"]["label"]
    synthetic_log = "$ " + " ".join(cmd) + "\n" + raw_stdout
    try:
        parsed = incident_mod.parse_failure_log(synthetic_log)
    except incident_mod.MalformedRunDirError:
        # No PASS/FAIL/INFRA line at all - the session never reached ANY step (a boot/
        # isolation failure). D4's own INFRA semantics: "any run never reached the asserted
        # step at all."
        return triage_mod.RunResult(status="INFRA", stepLabel=label)

    for step in parsed.steps:
        if step.label == label:
            if step.status == "PASS":
                return triage_mod.RunResult(status="PASS")
            return triage_mod.RunResult(status=step.status, stepLabel=step.label)

    # The asserted step's own label never appeared among the steps this run actually
    # reached - the run stopped short of it (D4 INFRA: "never reached the asserted step").
    return triage_mod.RunResult(status="INFRA", stepLabel=label)


def make_live_triage_run_once(
    incident: dict[str, Any], clone: Path,
) -> Callable[[int, dict[str, Any]], triage_mod.RunResult]:
    """
    Builds a `run_once(index, incident) -> RunResult` closure against ONE already-built
    sandbox `clone`, re-invoked k times by triage.triage()'s own orchestration loop.

    Interpretation note (flagged, not silent): triage.py's own `make_live_run_once()` is
    typed `Callable[[int], RunResult]` (one argument), but `triage.triage()` itself calls
    `run_once(i, incident)` - TWO positional arguments. That is a real inconsistency inside
    triage.py's own (unimplemented, NotImplementedError-raising) deferred seam, not something
    this module can silently paper over by guessing which shape is authoritative. Rather than
    calling triage.py's own `make_live_run_once()` (which raises NotImplementedError
    unconditionally and is never actually invoked anywhere in this codebase, including by
    triage.triage() itself), this module provides its OWN two-argument factory matching
    triage.triage()'s REAL, executable call convention - flagged here loudly per this task's
    own "if wiring reveals a needed pure-module change, STOP and report it" instruction,
    rather than editing triage.py.
    """
    tag_base = f"gl-{incident['id']}-triage"

    def _run_once(index: int, incident_arg: dict[str, Any]) -> triage_mod.RunResult:
        del incident_arg  # identical to `incident` (triage.triage() passes it back unchanged)
        cmd = _reproduce_command(incident, clone, tag=f"{tag_base}-{index}")
        try:
            proc = _run(cmd, cwd=clone, timeout=900)
        except subprocess.TimeoutExpired:
            return triage_mod.RunResult(status="INFRA", stepLabel=incident["failingStep"]["label"])
        return _classify_reproduce_run(incident, proc.stdout, cmd)

    return _run_once


def _stage_triage(incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *,
                   prior: dict[str, Any], sandbox_root: Path, main_repo: Path, jobs: int) -> dict[str, Any]:
    del prior, incident_dir  # triage is the FIRST loop stage; nothing prior to consult yet
    clone, _reused = find_or_build_sandbox(
        incident, policy_obj=policy_obj, sandbox_root=sandbox_root, main_repo=main_repo, jobs=jobs,
    )
    run_once = make_live_triage_run_once(incident, clone)
    return triage_mod.triage(incident, policy_obj=policy_obj, run_once=run_once)


# ══════════════════════════════════════════════════════════════════════════
# Stage 2: diagnosis - one headless Opus, read-only tools
# ══════════════════════════════════════════════════════════════════════════

DIAGNOSIS_SCHEMA_TEXT = """Answer with EXACTLY this JSON shape and nothing else (no prose
before or after, no markdown fence unless your tool forces one):

{
  "observed": "<what actually happened>",
  "expected": "<what should have happened>",
  "rootCause": {"file": "<sandbox-relative path>", "line": <1-based int>, "claim": "<why>"},
  "seam": "<the interface/boundary where the bug lives>",
  "confidence": "high" | "medium" | "low",
  "proposedRepair": "<what the repair stage should do>",
  "wouldNeedForbiddenChange": true | false
}

rootCause.file MUST be a path relative to the sandbox root you were given via --add-dir, and
MUST be a real file inside it, at a real line number - your citation will be mechanically
checked before anything else happens with your answer."""


def _diagnosis_prompt(incident: dict[str, Any]) -> str:
    failing = incident.get("failingStep", {})
    return (
        "You are the Diagnosis stage of the Colosseum Guardian Loop. Read "
        "docs/encyclopedia/ FIRST for the subsystem this failure touches (house law - it "
        "roughly halves a cold agent's search time), THEN read the sandboxed source at the "
        "seam this incident points at, and only then answer.\n\n"
        f"Incident {incident.get('id')}: scenario {incident.get('scenario')!r}, failing "
        f"step {failing.get('index')} ({failing.get('label')!r}): expected "
        f"{failing.get('expected')!r}, got {failing.get('got')!r}.\n\n"
        "You have Read/Grep/Glob only - no Bash, no edits, no web. Your context includes "
        "the sandbox (read-only in this stage), the incident's own evidence directory "
        "(incident.json, failure.log, journey.json, warnings.json, environment.json, "
        "reproduce.ps1, grabs/), and docs/encyclopedia/.\n\n" + DIAGNOSIS_SCHEMA_TEXT
    )


def live_diagnosis_invoke(
    incident: dict[str, Any], sandbox_root: Path | str, *, model: str,
    incident_dir: Path | None = None, repo_root: Path = REPO_ROOT,
    claude_cli: str = CLAUDE_CLI_DEFAULT, timeout_sec: int = 1800,
) -> dict[str, Any]:
    """
    The live implementation of diagnosis.default_invoke()'s own documented seam: headless
    `claude -p --model <model> --allowedTools Read,Grep,Glob`, guard hook wired via a
    generated settings.json, `--add-dir` = sandbox + incident dir + docs/encyclopedia/, cwd
    pinned inside the sandbox. Returns the model's raw (not-yet-validated) diagnosis dict -
    diagnosis.diagnose() itself runs validate_diagnosis()/check_citations()/
    check_forbidden_escalation()/may_proceed_to_repair() on whatever this returns.
    """
    clone = Path(sandbox_root)
    incident_dir = Path(incident_dir) if incident_dir else (
        orchestrator.DEFAULT_ARTIFACTS_ROOT / incident["id"]
    )
    with tempfile.TemporaryDirectory(prefix="gl-diag-settings-") as tmp:
        settings_path = write_guard_settings(tmp, clone, repo_root=repo_root)
        return run_headless_claude(
            _diagnosis_prompt(incident),
            cwd=clone,
            model=model,
            allowed_tools=["Read", "Grep", "Glob"],
            add_dirs=[clone, incident_dir, repo_root / "docs" / "encyclopedia"],
            settings_path=settings_path,
            claude_cli=claude_cli,
            timeout_sec=timeout_sec,
        )


def _stage_diagnosis(incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *,
                      prior: dict[str, Any], sandbox_root: Path, main_repo: Path,
                      claude_cli: str, jobs: int) -> dict[str, Any]:
    del prior  # diagnosis reads the incident packet + sandbox itself; no prior stage-file needed
    clone, _reused = find_or_build_sandbox(
        incident, policy_obj=policy_obj, sandbox_root=sandbox_root, main_repo=main_repo, jobs=jobs,
    )
    invoke = lambda inc, sbx, *, model: live_diagnosis_invoke(  # noqa: E731
        inc, sbx, model=model, incident_dir=incident_dir, repo_root=main_repo, claude_cli=claude_cli,
    )
    return diagnosis_mod.diagnose(incident, clone, policy_obj=policy_obj, invoke=invoke)


# ══════════════════════════════════════════════════════════════════════════
# Stage 3: repair - handcuffed edits, a Lanista bug test, mechanical red/green
# ══════════════════════════════════════════════════════════════════════════

# The bugtest declaration hand-off file: repair's own headless Sonnet has Write access
# (unlike diagnosis's read-only tools), so it is instructed to WRITE its final answer here
# rather than rely on parsing it out of chat text - more robust when a build/test run has
# already produced a huge amount of incidental stdout. This path is INSIDE the sandbox (the
# guard hook would deny a Write outside it anyway) and is deleted before extract_patch() ever
# runs, so it never pollutes the patch's own classification.
_BUGTEST_HANDOFF_RELPATH = ".guardian/bugtest.json"


def _repair_contract_text(incident: dict[str, Any], diagnosis: dict[str, Any],
                           policy_obj: Policy, prior_rejection: str | None) -> str:
    forbidden = ", ".join(sorted(policy_obj.forbidden_modify_delete))
    text = (
        "You are the Repair stage of the Colosseum Guardian Loop. You have Read/Grep/Glob/"
        "Edit/Write/Bash (Bash runs under a containment guard hook - it will refuse any "
        "path outside this sandbox, network egress, and git fetch/push/pull/clone/gc/"
        "repack/prune).\n\n"
        f"Incident {incident.get('id')}: scenario {incident['scenario']!r}, failing step "
        f"{incident['failingStep']['label']!r}.\n\n"
        f"Diagnosis: rootCause {diagnosis['rootCause']['file']}:{diagnosis['rootCause']['line']} "
        f"- {diagnosis['rootCause']['claim']}\nSeam: {diagnosis['seam']}\nProposed repair: "
        f"{diagnosis['proposedRepair']}\n\n"
        "CONTRACT (mechanically enforced, not a suggestion):\n"
        "1. You MUST add a NEW test file under tests/lanista_scenarios/ that is a Lanista "
        "scenario (JSON, same shape as the existing tests/lanista_scenarios/journey_*.json "
        "files) which FAILS (nonzero exit via `lanista session run <your-scenario>`) before "
        "your fix and PASSES after it. This is the ONLY bug-test shape this program accepts "
        "- a `ctest -R <name>` target CANNOT be registered by a patch (tests/CMakeLists.txt "
        "is forbidden to modify), so ctest can never serve as your proof.\n"
        "2. You may NOT modify or delete any EXISTING file under tests/, or any of these "
        f"forbidden paths: {forbidden}.\n"
        "3. You MUST change production code, not only add a test - a test-only patch proves "
        "the bug existed but proves nothing was fixed.\n"
        "4. When you are done, write your bug-test declaration to "
        f"{_BUGTEST_HANDOFF_RELPATH} (relative to this sandbox root) as EXACTLY this JSON "
        'shape: {"cmd": "lanista", "args": ["session", "run", "<the scenario path you '
        'added, tests/lanista_scenarios/...>"], "expectRedWithoutFix": true}\n'
    )
    if prior_rejection:
        text += (
            "\nYour PREVIOUS attempt was REJECTED for this exact reason (verbatim) - do not "
            f"repeat it:\n{prior_rejection}\n"
        )
    return text


def _reset_sandbox_to_base(clone: Path, base_sha: str) -> None:
    """Resets the repair sandbox's WORKING TREE to `base_sha` before each attempt, without
    discarding the built `native/build-msvc/` tree (so each attempt's own build stays
    incremental, never a fresh cold configure) and without discarding prior attempts'
    commits from reachability - see `_tag_attempt()` below for how those are preserved."""
    _git(clone, "checkout", "-f", base_sha)
    # -e excludes native/build-msvc from the untracked/ignored sweep (A9-adjacent: keep the
    # built exe + ninja's incremental state alive across attempts).
    _git(clone, "clean", "-fd", "-e", "native/build-msvc", "--", ".")


def _tag_attempt(clone: Path, incident_id: str, attempt: int) -> None:
    """Preserves this attempt's own commit under a tag BEFORE the next attempt resets the
    working tree back to base_sha - "the sandbox's own git history is the attempt ledger"
    (repair_contract.py's own docstring) stays true even though the branch tip itself moves
    back to base_sha between attempts; every attempt's commit remains reachable via its tag."""
    _git(clone, "tag", "-f", f"attempt-{attempt}-{incident_id}", "HEAD")


def _run_bugtest_twice(clone: Path, bugtest: dict[str, Any]) -> list[int]:
    """Runs the declared `lanista session run <scenario>` bug test exactly twice (A4: "red
    and green are each proven 2/2 runs"), each as its own tagged, isolated session, with
    QML_DISABLE_DISK_CACHE=1 set (A4 belt-and-braces). Returns the two exit codes."""
    lanista_exe = clone / "native" / "build-msvc" / "lanista.exe"
    scenario = bugtest["args"][2]
    env = dict(os.environ)
    env.update(_RED_GREEN_ENV_EXTRA)
    codes: list[int] = []
    for i in range(repair_contract.RED_GREEN_RUNS_REQUIRED):
        tag = f"gl-bugtest-{Path(scenario).stem}-{i}"
        cmd = [str(lanista_exe), "session", "run", scenario, "--tag", tag, "--verbose"]
        try:
            proc = _run(cmd, cwd=clone, timeout=600, env=env)
            codes.append(proc.returncode)
        except subprocess.TimeoutExpired:
            codes.append(-1)
    return codes


def _scratch_export_build(
    incident: dict[str, Any], base_clone: Path, *, suffix: str, main_repo: Path,
    sandbox_root: Path, testadds: list[str], production: list[str] | None, jobs: int,
) -> Path:
    """
    Builds a REAL, independent pristine scratch sandbox for one red or green red/green pass:
    `sandbox.create()` fresh from `incident['baseSha']`, then copies over ONLY `testadds`
    (the red pass) or `testadds` + `production` (the green pass) from `base_clone`'s working
    tree, then `sandbox.build()` + `sandbox.provision()`.

    This is a genuinely SEPARATE built sandbox, not a source-only export: the mandated bug
    test is a `lanista session run <scenario>` invocation (the batched-runtime-pass audit's
    own fix - ctest cannot serve), which needs a real built+provisioned colosseum.exe to run
    against, not just source files on disk. Named honestly in this module's own execution
    report as a real cost: one extra build per red pass and per green pass, on top of the
    shared repair sandbox's own build (docs/autorepair/batched-runtime-pass.md's own "Cost:
    two builds' worth" note for G6).
    """
    clone = sandbox.create(
        incident["baseSha"], f"{incident['id']}-{suffix}", main_repo=main_repo, sandbox_root=sandbox_root,
    )
    paths = list(testadds) + list(production or [])
    for rel in paths:
        src = base_clone / rel
        dst = clone / rel
        if src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    build_result = sandbox.build(clone, jobs=jobs)
    if not build_result.ok:
        raise LiveRunnerError(
            f"scratch export build failed for {clone}: returncode={build_result.returncode} "
            f"error_lines={build_result.error_lines!r}"
        )
    sandbox.provision(clone, main_repo=main_repo)
    return clone


def live_repair_invoke(
    incident: dict[str, Any], diagnosis: dict[str, Any], sandbox_root: Path | str, *,
    attempt: int, prior_rejection: str | None, model: str,
    policy_obj: Policy | None = None, incident_dir: Path | None = None,
    main_repo: Path = REPO_ROOT, sandbox_pool_root: Path = sandbox.DEFAULT_SANDBOX_ROOT,
    claude_cli: str = CLAUDE_CLI_DEFAULT, timeout_sec: int = 5400, jobs: int = 1,
) -> dict[str, Any]:
    """
    The live implementation of repair_contract.default_invoke()'s own documented seam, ONE
    attempt:

      1. Reset `sandbox_root`'s working tree to `incident['baseSha']` (`_reset_sandbox_to_base`).
      2. Headless `claude -p --model <model> --allowedTools Read,Grep,Glob,Edit,Write,Bash`,
         Bash under the SAME guard hook, prompt = the contract text (+ prior rejection
         verbatim on retry) - see `_repair_contract_text()`.
      3. `git add -A && git commit` (the attempt-ledger commit) + tag it (`_tag_attempt()`).
      4. Read the bug-test declaration the agent wrote to `.guardian/bugtest.json` inside the
         sandbox, then delete that file (it must never appear in the patch itself).
      5. Two REAL scratch sandboxes (`_scratch_export_build()`): test-adds-only (RED
         expected) and test-adds-plus-production (GREEN expected), the bug test run twice in
         each (`_run_bugtest_twice()`), then destroyed.

    Returns `{"bugtest": {...}, "redExitCodes": [c1, c2], "greenExitCodes": [c1, c2]}` -
    repair_contract.run_repair() feeds this through sandbox.extract_patch() +
    count_patch_lines() + its own three mechanical gates; this function never classifies the
    patch itself (Program ruling 1 - the orchestrator/pure module owns the laws).
    """
    if policy_obj is None:
        policy_obj = load_policy()
    clone = Path(sandbox_root)
    incident_dir = Path(incident_dir) if incident_dir else (
        orchestrator.DEFAULT_ARTIFACTS_ROOT / incident["id"]
    )

    _reset_sandbox_to_base(clone, incident["baseSha"])

    with tempfile.TemporaryDirectory(prefix="gl-repair-settings-") as tmp:
        settings_path = write_guard_settings(tmp, clone, repo_root=main_repo)
        run_headless_claude(
            _repair_contract_text(incident, diagnosis, policy_obj, prior_rejection),
            cwd=clone,
            model=model,
            allowed_tools=["Read", "Grep", "Glob", "Edit", "Write", "Bash"],
            add_dirs=[clone, incident_dir, main_repo / "docs" / "encyclopedia"],
            settings_path=settings_path,
            claude_cli=claude_cli,
            timeout_sec=timeout_sec,
        )

    _git(clone, "add", "-A")
    commit = _git(clone, "commit", "-m", f"Guardian Loop repair attempt {attempt} ({incident['id']})")
    if commit.returncode != 0 and "nothing to commit" not in (commit.stdout + commit.stderr).lower():
        raise LiveRunnerError(f"repair attempt {attempt} commit failed in {clone}: {commit.stderr}")
    _tag_attempt(clone, incident["id"], attempt)

    handoff_path = clone / _BUGTEST_HANDOFF_RELPATH
    if not handoff_path.is_file():
        raise LiveRunnerError(
            f"repair attempt {attempt} wrote no bug-test declaration at {handoff_path} - "
            "the contract requires one"
        )
    bugtest = json.loads(handoff_path.read_text(encoding="utf-8"))
    handoff_path.unlink()
    if handoff_path.parent.exists() and not any(handoff_path.parent.iterdir()):
        handoff_path.parent.rmdir()

    classification = sandbox.extract_patch(clone, policy_obj)

    red_clone = _scratch_export_build(
        incident, clone, suffix=f"repair-red-{attempt}", main_repo=main_repo,
        sandbox_root=sandbox_pool_root, testadds=classification["testAdds"], production=None, jobs=jobs,
    )
    try:
        red_codes = _run_bugtest_twice(red_clone, bugtest)
    finally:
        sandbox.destroy(red_clone)

    green_clone = _scratch_export_build(
        incident, clone, suffix=f"repair-green-{attempt}", main_repo=main_repo,
        sandbox_root=sandbox_pool_root, testadds=classification["testAdds"],
        production=classification["production"], jobs=jobs,
    )
    try:
        green_codes = _run_bugtest_twice(green_clone, bugtest)
    finally:
        sandbox.destroy(green_clone)

    return {"bugtest": bugtest, "redExitCodes": red_codes, "greenExitCodes": green_codes}


def _stage_repair(incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *,
                   prior: dict[str, Any], sandbox_root: Path, main_repo: Path,
                   claude_cli: str, jobs: int) -> dict[str, Any]:
    diagnosis = prior["diagnosis"]
    clone, _reused = find_or_build_sandbox(
        incident, policy_obj=policy_obj, sandbox_root=sandbox_root, main_repo=main_repo, jobs=jobs,
    )
    invoke = lambda inc, diag, sbx, *, attempt, prior_rejection, model: live_repair_invoke(  # noqa: E731
        inc, diag, sbx, attempt=attempt, prior_rejection=prior_rejection, model=model,
        policy_obj=policy_obj, incident_dir=incident_dir, main_repo=main_repo,
        sandbox_pool_root=sandbox_root, claude_cli=claude_cli, jobs=jobs,
    )
    return repair_contract.run_repair(incident, diagnosis, clone, policy_obj=policy_obj, invoke=invoke)


# ══════════════════════════════════════════════════════════════════════════
# Stage 4: verify - the independent judge in a pristine second laboratory
# ══════════════════════════════════════════════════════════════════════════


def _ctest_inventory_count(clone: Path) -> int:
    build_dir = clone / "native" / "build-msvc"
    result = _run([str(CTEST_EXE), "-N"], cwd=build_dir, timeout=120)
    matches = re.findall(r"Total Tests:\s*(\d+)", result.stdout)
    if not matches:
        raise LiveRunnerError(f"could not parse `ctest -N` inventory count in {build_dir}: {result.stdout[-500:]}")
    return int(matches[-1])


def _ctest_unit_run(clone: Path) -> dict[str, Any]:
    build_dir = clone / "native" / "build-msvc"
    result = _run(
        [str(CTEST_EXE), "-L", "unit", "--output-on-failure"], cwd=build_dir, timeout=1800,
    )
    total_match = re.search(r"tests passed, (\d+) tests failed out of (\d+)", result.stdout)
    failed = int(total_match.group(1)) if total_match else (0 if result.returncode == 0 else 1)
    total = int(total_match.group(2)) if total_match else 0
    failed_names = re.findall(r"^\s*\d+/\d+ Test\s+#\d+:\s+(\S+)\s+\.+\*\*\*Failed", result.stdout, re.MULTILINE)
    return {"label": "unit", "total": total, "failed": failed, "failedNames": failed_names}


def _warning_gate_run(repo_root: Path, log_paths: list[Path]) -> dict[str, Any]:
    existing = [p for p in log_paths if p.is_file()]
    args = ["-NoProfile", "-NonInteractive", "-File", str(repo_root / "tests" / "warning_gate.ps1"),
            "-LogPath", ",".join(str(p) for p in existing)]
    for shell in ("pwsh", "powershell"):
        if shutil.which(shell) is None:
            continue
        result = _run([shell, *args], timeout=120)
        return {
            "exitCode": result.returncode,
            "verdict": "WARNING_GATE_OK" if result.returncode == 0 else "FAIL",
            "output": [ln for ln in result.stdout.splitlines() if ln.strip()],
        }
    return {"exitCode": 1, "verdict": "FAIL", "output": ["neither pwsh nor powershell is on PATH"]}


def _run_journey(clone: Path, scenario_rel: str, *, tag: str) -> dict[str, Any]:
    lanista_exe = clone / "native" / "build-msvc" / "lanista.exe"
    result = _run(
        [str(lanista_exe), "session", "run", scenario_rel, "--tag", tag, "--verbose"],
        cwd=clone, timeout=600,
    )
    return {"scenario": scenario_rel, "passed": result.returncode == 0}


VERIFIER_SCHEMA_TEXT = """Answer with EXACTLY this JSON shape and nothing else:

{"approve": true | false, "reasons": ["<reason 1>", ...], "riskAssessment": "low"|"medium"|"high"}

`reasons` must be non-empty. Every mechanical gate below already passed - your job is the
part a script cannot judge: does the fix address the actual diagnosed cause (not just a
symptom), does it risk adjacent behavior, and is the bug test meaningful (not a vacuous
tautology)? You are allowed, and expected, to reject even with every mechanical gate green."""


def _verifier_prompt(context: dict[str, Any]) -> str:
    return (
        "You are the Verifier stage of the Colosseum Guardian Loop - an independent judge "
        "with NO knowledge of how this patch was diagnosed or written; you see only the "
        "incident, the patch, gate results, and acceptance criteria below.\n\n"
        + json.dumps(context, indent=2, ensure_ascii=False)
        + "\n\n" + VERIFIER_SCHEMA_TEXT
    )


def live_verify_invoke(
    incident: dict[str, Any], patch: str, base_sha: str, sandbox_root: Path | str, *,
    model: str, refutation: dict[str, Any], policy_obj: Policy | None = None,
    main_repo: Path = REPO_ROOT, first_sandbox: Path | None = None,
    claude_cli: str = CLAUDE_CLI_DEFAULT, timeout_sec: int = 5400, jobs: int = 1,
) -> dict[str, Any]:
    """
    The live implementation of verify.default_invoke()'s own documented seam. D7: a SECOND,
    independent pristine sandbox built from `base_sha` alone - `sandbox_root` here is that
    clone's own target path (this function creates it; nothing has built it yet when this is
    called). Sequence: create+build+provision -> `git apply` (D7 auto-reject boundary) ->
    every mechanical gate run for real -> build_verifier_context() -> headless Opus Verifier
    (read-only, `--add-dir` = ONLY the verify sandbox - see the "Ruling 4, mechanized" note
    below) -> one best-effort GLM refutation.
    """
    if policy_obj is None:
        policy_obj = load_policy()
    clone = Path(sandbox_root)

    if not clone.exists():
        built = sandbox.create(
            base_sha, clone.name, main_repo=main_repo, sandbox_root=clone.parent,
        )
        if built.resolve() != clone.resolve():
            raise LiveRunnerError(f"verify sandbox landed at {built}, expected {clone}")
        build_result = sandbox.build(clone, jobs=jobs)
        if not build_result.ok:
            raise LiveRunnerError(f"verify sandbox build failed for {clone}: returncode={build_result.returncode}")
        sandbox.provision(clone, main_repo=main_repo)

    patch_file = clone / ".guardian-verify.patch"
    patch_file.write_text(patch, encoding="utf-8")
    apply_result_proc = _git(clone, "apply", "--check", str(patch_file))
    apply_exit = apply_result_proc.returncode
    apply_stderr = apply_result_proc.stderr
    if apply_exit == 0:
        real_apply = _git(clone, "apply", str(patch_file))
        apply_exit = real_apply.returncode
        apply_stderr = real_apply.stderr
    patch_file.unlink(missing_ok=True)

    apply_result = {"exitCode": apply_exit, "stderr": apply_stderr}
    if apply_exit != 0:
        return {"applyResult": apply_result, "gateResults": {}, "verifierRaw": None, "refutation": None}

    # ---- mechanical gates, run independently (never trusting G6's own report) ----
    build_result = sandbox.build(clone, jobs=jobs)
    if not build_result.ok:
        raise LiveRunnerError(f"verify sandbox rebuild-after-apply failed for {clone}")
    sandbox.provision(clone, main_repo=main_repo)

    classification = sandbox.extract_patch(clone, policy_obj)

    red_clone = _scratch_export_build(
        incident, clone, suffix="verify-red", main_repo=main_repo, sandbox_root=clone.parent,
        testadds=classification["testAdds"], production=None, jobs=jobs,
    )
    green_clone = _scratch_export_build(
        incident, clone, suffix="verify-green", main_repo=main_repo, sandbox_root=clone.parent,
        testadds=classification["testAdds"], production=classification["production"], jobs=jobs,
    )
    # The declared bugtest command needs recovering; repair's own accepted declaration lives
    # only in the first (repair) sandbox's git-ignored .guardian/ scratch file, which this
    # function already deleted after reading. Re-derive the scenario path from testAdds
    # instead (D6: testAdds is exactly the file(s) the patch added under tests/) rather than
    # re-reading a file that no longer exists - the FIRST *.json testAdds path is the
    # scenario (an interpretation call, flagged: a patch could in principle add more than
    # one test file, but A4's own template requires ONE declared bugtest command).
    scenario_candidates = [p for p in classification["testAdds"] if p.endswith(".json")]
    bugtest = {
        "cmd": "lanista", "args": ["session", "run", scenario_candidates[0]] if scenario_candidates else [],
        "expectRedWithoutFix": True,
    }
    try:
        red_codes = _run_bugtest_twice(red_clone, bugtest) if scenario_candidates else [1, 1]
    finally:
        sandbox.destroy(red_clone)
    try:
        green_codes = _run_bugtest_twice(green_clone, bugtest) if scenario_candidates else [0, 0]
    finally:
        sandbox.destroy(green_clone)

    reproduce_cmd = _reproduce_command(incident, clone, tag="gl-verify-reproduce")
    reproduce_proc = _run(reproduce_cmd, cwd=clone, timeout=900)

    unit_tests = _ctest_unit_run(clone)
    warning_gate = _warning_gate_run(main_repo, [clone / "artifacts" / "lanista-sessions"])

    risk_class = None
    scenario = incident.get("scenario") or ""
    for rc in policy_obj.risk_classes:
        if fnmatch.fnmatch(scenario, rc["areaPattern"]):
            risk_class = rc
            break
    risk_class = risk_class or policy_obj.risk_classes[-1]
    journeys = [
        _run_journey(clone, j, tag=f"gl-verify-journey-{i}")
        for i, j in enumerate(risk_class["verify"]["journeys"])
    ]

    verify_count = _ctest_inventory_count(clone)
    base_count = None
    if first_sandbox is not None and first_sandbox.exists():
        try:
            base_count = _ctest_inventory_count(first_sandbox)
        except LiveRunnerError:
            base_count = None
    if base_count is None:
        base_count = verify_count - len(classification["testAdds"])

    # The ONE legitimate diagnosis.json read of the whole Verify stage (orchestrator-side
    # only, never handed to the model) - A5 gate 2's drive-by-fix guard.
    incident_dir = orchestrator.DEFAULT_ARTIFACTS_ROOT / incident["id"]
    diagnosis_path = incident_dir / "diagnosis.json"
    cited_files: list[str] = []
    if diagnosis_path.is_file():
        try:
            diag_obj = json.loads(diagnosis_path.read_text(encoding="utf-8"))
            cited_files = [diag_obj["rootCause"]["file"]]
        except (json.JSONDecodeError, KeyError, TypeError):
            cited_files = []
    touched_files = classification["testAdds"] + classification["production"]

    gate_results = {
        "bugTestRedGreen": {"redExitCodes": red_codes, "greenExitCodes": green_codes},
        "reproduceExitCode": reproduce_proc.returncode,
        "unitTests": unit_tests,
        "warningGate": warning_gate,
        "journeys": journeys,
        "inventory": {
            "baseCount": base_count, "verifyCount": verify_count,
            "patchAddedTestCount": len(classification["testAdds"]),
        },
        "diagnosisPatchIntersection": {
            "diagnosisCitedFiles": cited_files, "patchTouchedFiles": touched_files,
        },
    }

    agg = verify_mod.aggregate_gates(gate_results)
    if agg["overall"] == "FAIL":
        return {"applyResult": apply_result, "gateResults": gate_results, "verifierRaw": None, "refutation": None}

    context = verify_mod.build_verifier_context(incident, patch, clone, gate_results, policy_obj=policy_obj)

    # Ruling 4, mechanized: the Verifier's --add-dir is ONLY the verify sandbox - never the
    # incident dir at all (which would require excluding diagnosis.json/attempt-*/, a thing
    # --add-dir structurally cannot do). The prompt carries the full context as text instead,
    # so the model never needs directory access to the incident's own evidence tree to answer.
    with tempfile.TemporaryDirectory(prefix="gl-verify-settings-") as tmp:
        settings_path = write_guard_settings(tmp, clone, repo_root=main_repo)
        verifier_raw = run_headless_claude(
            _verifier_prompt(context), cwd=clone, model=model,
            allowed_tools=["Read", "Grep", "Glob"], add_dirs=[clone], settings_path=settings_path,
            claude_cli=claude_cli, timeout_sec=timeout_sec,
        )

    patch_summary = patch[:4000]
    refutation_result = run_glm_refutation(patch_summary, thinking=refutation.get("thinking", "high"))

    return {
        "applyResult": apply_result, "gateResults": gate_results,
        "verifierRaw": verifier_raw, "refutation": refutation_result,
    }


def _stage_verify(incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *,
                   prior: dict[str, Any], sandbox_root: Path, main_repo: Path,
                   claude_cli: str, jobs: int) -> dict[str, Any]:
    del incident_dir
    first_clone, _reused = find_or_build_sandbox(
        incident, policy_obj=policy_obj, sandbox_root=sandbox_root, main_repo=main_repo, jobs=jobs,
    )
    patch = extract_patch_text(first_clone, incident["baseSha"])
    verify_clone_target = (Path(sandbox_root) / f"{incident['id']}-verify").resolve()

    invoke = lambda inc, pat, base_sha, sbx, *, model, refutation: live_verify_invoke(  # noqa: E731
        inc, pat, base_sha, sbx, model=model, refutation=refutation, policy_obj=policy_obj,
        main_repo=main_repo, first_sandbox=first_clone, claude_cli=claude_cli, jobs=jobs,
    )
    return verify_mod.run_verify(
        incident, patch, incident["baseSha"], verify_clone_target, policy_obj=policy_obj, invoke=invoke,
    )


# ══════════════════════════════════════════════════════════════════════════
# Stage 5: promotion - branch, draft PR, dossier; the human gate
# ══════════════════════════════════════════════════════════════════════════


def _dossier_list(items: list[str]) -> list[str]:
    return items if items else ["(none recorded)"]


def _assemble_dossier(incident: dict[str, Any], diagnosis: dict[str, Any],
                       repair: dict[str, Any], verify_result: dict[str, Any]) -> dict[str, Any]:
    """The D9 twelve-item dossier, assembled from the three prior stages' own already-
    validated outputs (never re-deriving any of their judgments) - fed to
    promotion.assemble_pr_body() which performs the actual completeness gate + rendering."""
    failing = incident.get("failingStep", {})
    gates = (verify_result.get("gates") or {}).get("gates", {})
    unit = gates.get("unitTestsFullPass") or {}
    return {
        "incidentId": incident.get("id"),
        "problem": (
            f"{incident.get('scenario')}: step {failing.get('index')} ({failing.get('label')}) "
            f"- expected {failing.get('expected')!r}, got {failing.get('got')!r}."
        ),
        "rootCause": f"{diagnosis['rootCause']['file']}:{diagnosis['rootCause']['line']} - {diagnosis['rootCause']['claim']}",
        "reproduction": [f"pwsh {incident.get('id')}/reproduce.ps1"],
        "filesChanged": _dossier_list(
            (repair.get("classification") or {}).get("production", [])
            + (repair.get("classification") or {}).get("testAdds", [])
        ),
        "whyThisFix": diagnosis.get("proposedRepair", "(not recorded)"),
        "negativeControl": (
            f"Bug test red {repair.get('redExitCodes')} without the fix, green "
            f"{repair.get('greenExitCodes')} with it (independently re-proven at verify: "
            f"{(gates.get('bugTestRedGreen') or {}).get('detail', 'n/a')})."
        ),
        "focusedTests": _dossier_list([
            " ".join((repair.get("bugtest") or {}).get("args", []))
        ]),
        "journeyVerification": _dossier_list([
            (gates.get("journeysAllPass") or {}).get("detail", "n/a")
        ]),
        "fullRegression": unit.get("detail", "n/a"),
        "warnings": gates.get("warningGateClean", {}).get("detail", "n/a"),
        "beforeAfterScreenshots": ["(no screenshot capture wired in the live runner yet - see incident's own grabs/)"],
        "riskAssessment": verify_result.get("riskAssessment") or "(not assessed)",
    }


def _idempotence_check(repo_root: Path, target_branch: str, incident_id: str, gh_cli: str) -> bool:
    local = _git(repo_root, "branch", "--list", target_branch).stdout.strip()
    if local:
        return True
    remote = _git(repo_root, "ls-remote", "--heads", "origin", target_branch).stdout.strip()
    if remote:
        return True
    if shutil.which(gh_cli):
        pr_check = _run(
            [gh_cli, "pr", "list", "--search", incident_id, "--state", "open", "--json", "number"],
            cwd=repo_root, timeout=30,
        )
        if pr_check.returncode == 0:
            try:
                prs = json.loads(pr_check.stdout or "[]")
                if prs:
                    return True
            except json.JSONDecodeError:
                pass
    return False


def live_promotion_invoke(
    incident: dict[str, Any], patch: str, verdict: dict[str, Any], dossier: dict[str, Any],
    base_sha: str, *, target_branch: str, pr_body: str, repo_root: Path = REPO_ROOT,
    gh_cli: str = GH_CLI_DEFAULT,
) -> dict[str, Any]:
    """
    The live implementation of promotion.default_invoke()'s own documented seam - Program
    ruling 10's Rule-28 carve-out: ALL git work happens through a TEMP WORKTREE of the MAIN
    repo, never a `git checkout` inside the primary working tree (which may hold another
    lane's WIP - Rule 28's general no-worktree-without-asking rule does not apply here
    because this carve-out was ratified BY the Guardian Loop spec itself, ruling 10).

      1. Idempotence check FIRST (before any mutation).
      2. `git fetch origin` (read-only network op - the one this whole program's v0 scope
         permits, and only here, never inside any sandboxed agent stage).
      3. A6 rebase-onto-master ATTEMPT via a scratch check (never force-applied; conflict ->
         `rebase_flag()`-shaped banner prepended to `pr_body`, the base-SHA branch is kept).
      4. `git worktree add <tmp> -b <target_branch> <base_sha>`.
      5. `git apply` the patch inside the worktree.
      6. One commit (dossier trailer + Co-Authored-By).
      7. `git push origin <target_branch>`.
      8. `gh pr create --draft` - a `gh` failure still leaves the branch pushed; the body is
         written to a file and reported Bridge-blocked, never a silently-claimed success.
    """
    if _idempotence_check(repo_root, target_branch, incident.get("id", ""), gh_cli):
        return {"alreadyPromoted": True}

    _git(repo_root, "fetch", "origin", timeout=120)

    master_sha = _git_output_or_none(repo_root, "rev-parse", "origin/master") or base_sha
    rebase_result: dict[str, Any] = {"conflict": False, "currentMasterSha": master_sha}
    if master_sha != base_sha:
        merge_base = _git_output_or_none(repo_root, "merge-base", "--is-ancestor", base_sha, "origin/master")
        # `git merge-base --is-ancestor` communicates via exit code only (empty stdout on
        # both success and failure) - re-run explicitly to read the exit code, never guessed
        # from stdout text.
        ancestor_check = _git(repo_root, "merge-base", "--is-ancestor", base_sha, "origin/master")
        if ancestor_check.returncode != 0:
            rebase_result = {
                "conflict": True, "currentMasterSha": master_sha,
                "conflictFiles": [],  # a real conflict-file list needs an attempted rebase in
                                       # the worktree itself; the base-SHA branch is kept
                                       # either way (A6), so this is left honestly empty
                                       # rather than fabricated.
            }
        del merge_base

    banner = promotion_mod.rebase_flag(rebase_result)
    effective_pr_body = f"{banner}\n\n{pr_body}" if banner else pr_body

    worktree_dir = Path(tempfile.mkdtemp(prefix="gl-promote-worktree-"))
    worktree_dir.rmdir()  # `git worktree add` requires the target NOT already exist
    branch_pushed = False
    try:
        add_result = _git(
            repo_root, "worktree", "add", str(worktree_dir), "-b", target_branch, base_sha, timeout=120,
        )
        if add_result.returncode != 0:
            raise LiveRunnerError(f"git worktree add failed: {add_result.stderr}")

        patch_file = worktree_dir / ".guardian-promote.patch"
        patch_file.write_text(patch, encoding="utf-8")
        apply_result = _git(worktree_dir, "apply", str(patch_file))
        patch_file.unlink(missing_ok=True)
        if apply_result.returncode != 0:
            raise LiveRunnerError(f"git apply failed in promotion worktree: {apply_result.stderr}")

        _git(worktree_dir, "add", "-A")
        commit_message = (
            f"autorepair: {incident.get('id')} - {(incident.get('failingStep') or {}).get('label', '')}\n\n"
            f"Dossier: artifacts/autorepair/{incident.get('id')}/report.md\n\n"
            "Co-Authored-By: Colosseum Guardian Loop <noreply@colosseum.local>"
        )
        commit_result = _git(worktree_dir, "commit", "-m", commit_message)
        if commit_result.returncode != 0:
            raise LiveRunnerError(f"git commit failed in promotion worktree: {commit_result.stderr}")

        push_result = _git(worktree_dir, "push", "-u", "origin", target_branch, timeout=180)
        if push_result.returncode != 0:
            raise LiveRunnerError(f"git push failed for {target_branch}: {push_result.stderr}")
        branch_pushed = True

        if shutil.which(gh_cli) is None:
            body_path = orchestrator.DEFAULT_ARTIFACTS_ROOT / incident["id"] / "pr-body.md"
            body_path.parent.mkdir(parents=True, exist_ok=True)
            body_path.write_text(effective_pr_body, encoding="utf-8")
            return {
                "alreadyPromoted": False, "rebaseResult": rebase_result, "rebaseFlag": banner,
                "branchPushed": True, "ghFailed": True, "prUrl": None, "prBodyFilePath": str(body_path),
            }

        title = f"autorepair: {incident.get('id')} - {(incident.get('failingStep') or {}).get('label', '')[:60]}"
        pr_result = _run(
            [gh_cli, "pr", "create", "--draft", "--base", "master", "--head", target_branch,
             "--title", title, "--body", effective_pr_body],
            cwd=repo_root, timeout=120,
        )
        if pr_result.returncode != 0:
            body_path = orchestrator.DEFAULT_ARTIFACTS_ROOT / incident["id"] / "pr-body.md"
            body_path.parent.mkdir(parents=True, exist_ok=True)
            body_path.write_text(effective_pr_body, encoding="utf-8")
            return {
                "alreadyPromoted": False, "rebaseResult": rebase_result, "rebaseFlag": banner,
                "branchPushed": True, "ghFailed": True, "prUrl": None, "prBodyFilePath": str(body_path),
            }

        pr_url = pr_result.stdout.strip().splitlines()[-1] if pr_result.stdout.strip() else None
        return {
            "alreadyPromoted": False, "rebaseResult": rebase_result, "rebaseFlag": banner,
            "branchPushed": True, "ghFailed": False, "prUrl": pr_url, "prBodyFilePath": None,
        }
    finally:
        if worktree_dir.exists():
            _git(repo_root, "worktree", "remove", "--force", str(worktree_dir), timeout=60)


def _stage_promotion(incident: dict[str, Any], incident_dir: Path, policy_obj: Policy, *,
                      prior: dict[str, Any], sandbox_root: Path, main_repo: Path,
                      gh_cli: str, jobs: int) -> dict[str, Any]:
    del incident_dir
    diagnosis, repair, verify_result = prior["diagnosis"], prior["repair"], prior["verify"]
    first_clone, _reused = find_or_build_sandbox(
        incident, policy_obj=policy_obj, sandbox_root=sandbox_root, main_repo=main_repo, jobs=jobs,
    )
    patch = extract_patch_text(first_clone, incident["baseSha"])
    dossier = _assemble_dossier(incident, diagnosis, repair, verify_result)

    invoke = lambda inc, pat, verd, doss, base_sha, *, target_branch, pr_body: live_promotion_invoke(  # noqa: E731
        inc, pat, verd, doss, base_sha, target_branch=target_branch, pr_body=pr_body,
        repo_root=main_repo, gh_cli=gh_cli,
    )
    return promotion_mod.promote(
        incident, patch, verify_result, dossier, incident["baseSha"], policy_obj=policy_obj, invoke=invoke,
    )


# ══════════════════════════════════════════════════════════════════════════
# build_live_stage_runners() - the orchestrator-consumable dict
# ══════════════════════════════════════════════════════════════════════════


def build_live_stage_runners(
    *, repo_root: Path = REPO_ROOT, artifacts_root: Path | None = None,
    sandbox_root: Path = sandbox.DEFAULT_SANDBOX_ROOT, claude_cli: str = CLAUDE_CLI_DEFAULT,
    gh_cli: str = GH_CLI_DEFAULT, jobs: int = 1,
) -> dict[str, Callable[..., dict[str, Any]]]:
    """
    Returns the `stage_runners` dict `orchestrator.run_incident(incident_dir,
    stage_runners=...)` consumes - one callable per orchestrator.LOOP_STAGES entry
    ("triage", "diagnosis", "repair", "verify", "promotion"), each matching the
    orchestrator's own stage-runner signature `(incident, incident_dir, policy_obj, *,
    prior) -> dict`.
    """
    del artifacts_root  # reserved for a future override; every stage below derives its own
    # incident_dir from orchestrator.DEFAULT_ARTIFACTS_ROOT / incident['id'] today, matching
    # exactly what the orchestrator itself uses for the SAME incident directory.

    def _triage(incident, incident_dir, policy_obj, *, prior):
        return _stage_triage(
            incident, incident_dir, policy_obj, prior=prior,
            sandbox_root=sandbox_root, main_repo=repo_root, jobs=jobs,
        )

    def _diagnosis(incident, incident_dir, policy_obj, *, prior):
        return _stage_diagnosis(
            incident, incident_dir, policy_obj, prior=prior,
            sandbox_root=sandbox_root, main_repo=repo_root, claude_cli=claude_cli, jobs=jobs,
        )

    def _repair(incident, incident_dir, policy_obj, *, prior):
        return _stage_repair(
            incident, incident_dir, policy_obj, prior=prior,
            sandbox_root=sandbox_root, main_repo=repo_root, claude_cli=claude_cli, jobs=jobs,
        )

    def _verify(incident, incident_dir, policy_obj, *, prior):
        return _stage_verify(
            incident, incident_dir, policy_obj, prior=prior,
            sandbox_root=sandbox_root, main_repo=repo_root, claude_cli=claude_cli, jobs=jobs,
        )

    def _promotion(incident, incident_dir, policy_obj, *, prior):
        return _stage_promotion(
            incident, incident_dir, policy_obj, prior=prior,
            sandbox_root=sandbox_root, main_repo=repo_root, gh_cli=gh_cli, jobs=jobs,
        )

    return {
        "triage": _triage,
        "diagnosis": _diagnosis,
        "repair": _repair,
        "verify": _verify,
        "promotion": _promotion,
    }


# ══════════════════════════════════════════════════════════════════════════
# CLI - --from-run (mint + run live) and --incident ... --resume (continue live)
# ══════════════════════════════════════════════════════════════════════════


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Colosseum Guardian Loop - run a failure from incident to draft PR "
        "using the REAL live stage runners (sandboxes, headless claude, git/gh)."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--from-run", metavar="RUN_DIR", help="build a new incident and run the live loop")
    group.add_argument("--incident", metavar="INCIDENT_ID", help="continue an existing incident (requires --resume)")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--artifacts-root", default=str(orchestrator.DEFAULT_ARTIFACTS_ROOT))
    parser.add_argument("--sandbox-root", default=str(sandbox.DEFAULT_SANDBOX_ROOT))
    parser.add_argument("--claude-cli", default=CLAUDE_CLI_DEFAULT)
    parser.add_argument("--gh-cli", default=GH_CLI_DEFAULT)
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args(argv)

    artifacts_root = Path(args.artifacts_root)

    if args.from_run:
        result = incident_mod.build_incident(args.from_run, artifacts_root=artifacts_root)
        incident_dir = result.dir
    else:
        if not args.resume:
            parser.error("--incident requires --resume (there is no other supported mode yet)")
        incident_dir = artifacts_root / args.incident
        if not incident_dir.is_dir():
            parser.error(f"no incident directory found: {incident_dir}")

    stage_runners = build_live_stage_runners(
        sandbox_root=Path(args.sandbox_root), claude_cli=args.claude_cli,
        gh_cli=args.gh_cli, jobs=args.jobs,
    )

    try:
        outcome = orchestrator.run_incident(incident_dir, stage_runners=stage_runners)
    except orchestrator.OrchestratorError as exc:
        print(f"ORCHESTRATOR ERROR: {exc}", file=sys.stderr)
        return 2

    print(f"INCIDENT {outcome['incidentId']}: {outcome['terminalState']}")
    print(f"  report: {incident_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
