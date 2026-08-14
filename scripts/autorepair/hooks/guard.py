#!/usr/bin/env python3
"""
Colosseum Guardian Loop - the containment guard-hook (Slice G4, ruling 7a REFUSE).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G4 ("Triage -
reproduce or dismiss, and the headless-agent probe") and binding pressure-test amendment
A2 ("path canonicalization before every guard decision"). Program ruling 7's containment
triad, part (a) REFUSE: "headless agents run with cwd pinned to the sandbox, whitelisted
tools, and a PreToolUse guard hook rejecting path escapes."

This module is a pure decision function plus a thin CLI wrapper matching Claude Code's
PreToolUse hook shape (stdin: one JSON object describing the pending tool call; stdout:
one JSON object carrying the decision). WIRING this script into a real headless
`claude -p` session's settings, and the LIVE `claude -p` escape-attempt probe the plan
calls for, are DEFERRED to the Guardian Loop's batched runtime pass - see "DEFERRED"
below. This slice ships the decision mechanics and their deterministic tests only.

A2 enforcement (binding amendment; this module's whole job):

  1. CANONICALIZE every path before deciding - canonicalize(): expand %ENV% vars and
     '~', resolve to an absolute real path (Path.resolve(), the Windows equivalent of
     GetFullPathName + realpath - proven live this slice to follow an NTFS junction/
     reparse point even for a not-yet-existing final path component: creating a real
     `mklink /J` junction inside a temp sandbox and resolving a path through it landed
     outside the sandbox exactly as an escape should), strip a leading `\\\\?\\`
     extended-length prefix, and CASE-FOLD (NTFS is case-insensitive) before any
     comparison.
  2. CONTAINMENT - a Read/Write/Edit/MultiEdit/NotebookEdit/Glob/Grep tool (or a
     path-like token inside a Bash command) whose canonical target resolves outside the
     sandbox root -> DENY. Junction/reparse escapes are caught because canonicalization
     resolves them BEFORE the containment check runs, never by pattern-matching raw text.
  3. EGRESS DENIAL (the no-network v0 ruling, mechanized) - any Bash/command tool call
     whose command text names curl, wget, Invoke-WebRequest, `git fetch|push|pull`, pip,
     or npm -> DENY, independent of path containment.
  4. A1's git hygiene - `git gc`, `git repack`, `git prune` anywhere in a Bash command ->
     DENY (a `--no-hardlinks` clone's pack could still be corrupted by these; A1: "no
     shared object store, ever").
  5. Absolute reads under %USERPROFILE% (outside the sandbox) -> DENY - mechanizes "no
     default acquisition sources" for shell-borne reads, since a `type`/`cat`/
     `Get-Content` of a home-directory file would otherwise dodge the file-tool
     containment check in rule 2 entirely.
  6. FAIL CLOSED - no sandbox root configured -> DENY every tool call, no exceptions.

Stdlib only (house pattern). This module deliberately does NOT import
scripts/autorepair/policy.py: this hook's job is CONTAINMENT (ruling 7a) and network
egress (A2's no-network mechanization) - a different concern from policy.py's forbidden
MODIFY/DELETE law, which belongs to a later slice's repair_contract.py. Keeping this a
separate, zero-dependency module means the hook that gates every tool call in a headless
session has the smallest possible surface to get wrong.

Public API:

    decide(tool_call, *, sandbox_root) -> {"allow": bool, "reason": str}
    canonicalize(raw_path, *, cwd=None) -> str        # A2's canonicalization, exposed for tests
    main(argv=None) -> int                             # stdin/stdout CLI wrapper

CLI usage (the shape a Claude Code PreToolUse `"type": "command"` hook is invoked with -
JSON tool-call payload on stdin, JSON decision on stdout, exit 0):

    echo '{"tool_name": "Read", "tool_input": {"file_path": "C:/arsbx/AR-0001/x.txt"}}' | \\
        python scripts/autorepair/hooks/guard.py --sandbox-root C:/arsbx/AR-0001

DEFERRED to the Guardian Loop's batched runtime pass (not built by this slice, named
plainly rather than left silent):

  - Wiring this script into a real .claude/settings.json PreToolUse hook entry for a
    headless `claude -p` session (the --sandbox-root value would come from the
    orchestrator's own per-incident state, injected at invocation time).
  - The live probe: a scripted `claude -p` run with this hook wired, asserting an
    in-sandbox read succeeds, an escape attempt is refused by the hook, and the
    Guardian Loop's separate main-repo drift tripwire (sandbox.py's main_drift_check)
    stays silent throughout.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import sys
from pathlib import Path
from typing import Any

__all__ = ["GuardError", "decide", "canonicalize", "main"]


class GuardError(RuntimeError):
    """Raised only for a genuinely malformed decide() invocation - never for an ordinary
    allow/deny decision, which is always a normal return value, not an exception."""


# ── tool shape tables ────────────────────────────────────────────────────────

# Tool names whose tool_input carries a single file-path-shaped field to contain (rule 2).
_FILE_PATH_TOOLS: dict[str, tuple[str, ...]] = {
    "Read": ("file_path",),
    "Write": ("file_path",),
    "Edit": ("file_path",),
    "MultiEdit": ("file_path",),
    "NotebookEdit": ("notebook_path",),
    "Glob": ("path",),
    "Grep": ("path",),
}

_COMMAND_TOOLS = {"Bash"}

# Rule 3: egress binaries/verbs - the no-network v0 ruling, mechanized. Word-boundary,
# case-insensitive substring matching over the raw command text - a deliberate v0
# heuristic (not a full shell parse), named honestly: ruling 7's own stated threat model
# is "an erring agent, not an adversarial one," so catching the plain, unobfuscated verb
# is the bar this slice clears.
_EGRESS_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"(?<![\w.-])curl(?:\.exe)?(?![\w.-])", re.IGNORECASE),
    re.compile(r"(?<![\w.-])wget(?:\.exe)?(?![\w.-])", re.IGNORECASE),
    re.compile(r"(?<![\w.-])Invoke-WebRequest(?![\w.-])", re.IGNORECASE),
    re.compile(r"\bgit\s+fetch\b", re.IGNORECASE),
    re.compile(r"\bgit\s+push\b", re.IGNORECASE),
    re.compile(r"\bgit\s+pull\b", re.IGNORECASE),
    re.compile(r"(?<![\w.-])pip3?(?:\.exe)?(?![\w.-])", re.IGNORECASE),
    re.compile(r"(?<![\w.-])npm(?:\.cmd)?(?![\w.-])", re.IGNORECASE),
)

# Rule 4 (A1): sandbox git hygiene - never gc/repack/prune a --no-hardlinks clone.
_GIT_GC_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"\bgit\s+gc\b", re.IGNORECASE),
    re.compile(r"\bgit\s+repack\b", re.IGNORECASE),
    re.compile(r"\bgit\s+prune\b", re.IGNORECASE),
)

# Best-effort absolute-path token shapes pulled out of a Bash command string for the
# containment (rule 2) and %USERPROFILE% (rule 5) checks: a Windows drive path
# (`C:\...` / `C:/...`), a UNC path (`\\host\share\...`), a POSIX-style absolute path, or
# a `~`-relative path.
_WINDOWS_ABS_RE = re.compile(r"^[A-Za-z]:[\\/][^\s\"']*$")
_UNC_OR_POSIX_ABS_RE = re.compile(r"^(?:\\\\[^\s\"']+|/[^\s\"']+|~[\\/][^\s\"']*)$")


def _deny(reason: str) -> dict[str, Any]:
    return {"allow": False, "reason": reason}


def _allow(reason: str) -> dict[str, Any]:
    return {"allow": True, "reason": reason}


# ── canonicalize(): A2's path canonicalization ──────────────────────────────


def canonicalize(raw_path: str, *, cwd: str | Path | None = None) -> str:
    """
    A2: expand env vars (`%USERPROFILE%`, `$HOME`, ...) and `~`, resolve relative to
    `cwd` (default: the process cwd) to an absolute real path via Path.resolve() - the
    Windows equivalent of GetFullPathName + realpath, proven live this slice to follow
    NTFS junctions/reparse points even for a not-yet-existing final path component - then
    strip a leading `\\\\?\\` extended-length prefix and case-fold (NTFS is
    case-insensitive). Never raises on a merely nonexistent path (Path.resolve(strict=
    False) normalizes those happily); only genuinely unparsable input can raise, and even
    that is treated as a normal string here, not fatally - garbage input canonicalizes to
    itself and simply fails the containment check like any other outside path.
    """
    text = raw_path.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in ("'", '"'):
        text = text[1:-1]
    text = os.path.expandvars(text)
    text = os.path.expanduser(text)

    base = Path(cwd) if cwd is not None else Path.cwd()
    candidate = Path(text)
    if not candidate.is_absolute():
        candidate = base / candidate

    resolved = candidate.resolve(strict=False)
    s = str(resolved)
    if s.startswith("\\\\?\\"):
        s = s[4:]
    return s.lower()


def _is_contained(canon_target: str, canon_root: str) -> bool:
    if canon_target == canon_root:
        return True
    sep = os.sep.lower()
    return canon_target.startswith(canon_root.rstrip(sep) + sep)


def _extract_path_tokens(command: str) -> list[str]:
    """Best-effort: pull path-LIKE tokens out of a Bash command string for the
    containment (rule 2) and %USERPROFILE% (rule 5) checks. Deliberately NOT a full
    shell parse - no variable expansion beyond what canonicalize() does per-token, no
    glob expansion, no quoting-edge-case handling beyond simple tokenizing. Named
    honestly as a v0 heuristic against an erring agent (ruling 7's own threat model), not
    a guarantee against a determined adversary."""
    try:
        tokens = shlex.split(command, posix=False)
    except ValueError:
        tokens = command.split()

    found: list[str] = []
    for tok in tokens:
        bare = tok
        if len(bare) >= 2 and bare[0] == bare[-1] and bare[0] in ("'", '"'):
            bare = bare[1:-1]
        if _WINDOWS_ABS_RE.match(bare) or _UNC_OR_POSIX_ABS_RE.match(bare):
            found.append(bare)
        elif "%USERPROFILE%" in bare.upper():
            found.append(bare)
    return found


# ── decide(): the pure PreToolUse decision ──────────────────────────────────


def decide(tool_call: dict[str, Any], *, sandbox_root: str | Path | None) -> dict[str, Any]:
    """
    The pure PreToolUse decision (ruling 7a REFUSE). `tool_call` mirrors Claude Code's
    hook payload shape: {"tool_name": ..., "tool_input": {...}, ...other fields ignored}.
    `sandbox_root` is the ONE sandbox this headless session is allowed to touch - a
    required parameter (A2: "the sandbox root is a parameter"), never inferred from the
    tool call itself; unset/empty -> fail closed (rule 6), before anything else runs.
    """
    if sandbox_root is None or str(sandbox_root).strip() == "":
        return _deny("no sandbox root configured for this guard hook - failing closed (A2 rule 6)")

    canon_root = canonicalize(str(sandbox_root))

    tool_name = tool_call.get("tool_name")
    tool_input = tool_call.get("tool_input")
    if not isinstance(tool_input, dict):
        return _deny(
            f"malformed tool call: tool_input is not an object (tool_name={tool_name!r}) - "
            "failing closed"
        )

    if tool_name in _COMMAND_TOOLS:
        command = tool_input.get("command")
        if not isinstance(command, str) or not command.strip():
            return _deny(f"{tool_name}: no command text to evaluate - failing closed")

        for pattern in _GIT_GC_PATTERNS:
            if pattern.search(command):
                return _deny(
                    f"denied: sandbox git-hygiene violation (A1, no shared object store "
                    f"ever) - command matches {pattern.pattern!r}"
                )
        for pattern in _EGRESS_PATTERNS:
            if pattern.search(command):
                return _deny(
                    f"denied: egress binary/verb in command (A2 no-network v0 ruling, "
                    f"mechanized) - command matches {pattern.pattern!r}"
                )

        for token in _extract_path_tokens(command):
            canon = canonicalize(token)
            if not _is_contained(canon, canon_root):
                return _deny(
                    f"denied: command references a path outside the sandbox - {token!r} "
                    f"canonicalizes to {canon!r}, sandbox root is {canon_root!r}"
                )

        return _allow(
            f"{tool_name}: command contains no egress verb, no git gc/repack/prune, and "
            "no out-of-sandbox path reference"
        )

    fields = _FILE_PATH_TOOLS.get(tool_name)
    if fields is None:
        # An unrecognized/unlisted tool (e.g. Task, WebFetch, TodoWrite) is out of this
        # hook's scope - it judges path-bearing and command tools only. What a headless
        # stage can call AT ALL is a separate, narrower gate: D3's --allowedTools
        # whitelist per stage, set at invocation time, not by this hook.
        return _allow(f"{tool_name}: not a path-bearing or command tool - out of this hook's scope")

    for field in fields:
        raw = tool_input.get(field)
        if not isinstance(raw, str) or not raw.strip():
            return _deny(f"{tool_name}: missing or empty {field!r} - failing closed")
        canon = canonicalize(raw)
        if not _is_contained(canon, canon_root):
            return _deny(
                f"denied: {tool_name} targets a path outside the sandbox - {raw!r} "
                f"canonicalizes to {canon!r}, sandbox root is {canon_root!r}"
            )

    return _allow(f"{tool_name}: target path(s) canonicalize inside the sandbox root")


# ── CLI: stdin/stdout PreToolUse hook wrapper ───────────────────────────────


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Guardian Loop PreToolUse containment guard-hook (Slice G4, ruling 7a)."
    )
    parser.add_argument(
        "--sandbox-root",
        default=os.environ.get("AUTOREPAIR_SANDBOX_ROOT"),
        help="the ONE sandbox directory this headless session may touch; falls back to "
        "$AUTOREPAIR_SANDBOX_ROOT; unset -> fail closed (rule 6)",
    )
    args = parser.parse_args(argv)

    raw = sys.stdin.read()
    try:
        payload = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError as exc:
        result = _deny(f"guard hook: unparsable stdin JSON: {exc}")
    else:
        if not isinstance(payload, dict):
            result = _deny("guard hook: stdin JSON must be an object")
        else:
            result = decide(payload, sandbox_root=args.sandbox_root)

    # Claude Code's current PreToolUse hook contract reads hookSpecificOutput.
    # permissionDecision ("allow"|"deny"|"ask") + permissionDecisionReason from stdout
    # JSON on a clean (exit 0) run - this wrapper always exits 0 and communicates the
    # decision purely through that JSON, rather than mixing in the older exit-code-2-
    # blocks convention, to avoid two hook mechanisms disagreeing about the same call.
    # The plain top-level {"decision", "reason"} pair is kept alongside it for this
    # module's own tests/CLI callers who read the simple shape directly.
    hook_output = {
        "decision": "approve" if result["allow"] else "block",
        "reason": result["reason"],
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "allow" if result["allow"] else "deny",
            "permissionDecisionReason": result["reason"],
        },
    }
    print(json.dumps(hook_output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
