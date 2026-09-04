#!/usr/bin/env python3
"""
Colosseum Guardian Loop - the disposable laboratory (Slice G2).

docs/superpowers/plans/2026-08-14-colosseum-guardian-loop-plan.md, Slice G2 ("The
laboratory - sandbox create/build/diff/destroy + main-repo drift tripwire"). Gives every
later Guardian Loop stage (triage G4, repair G6, verify G7) a disposable Colosseum that
cannot leak back into the real one.

Design decisions this module implements, verbatim from the plan:

  D5 - sandbox = a real local git clone at the failing SHA (`git clone --local
       --no-hardlinks`, then `git remote remove origin`, checkout <sha>): real .git for
       trivial diff extraction, zero shared refs with the main repo. Provisioning of
       runtime DLLs (windeployqt + libmpv/MpvQt/ffmpeg copy) is a SEPARATE provision()
       step so tests can exercise create() without a build.
  D6 - patch mechanics: `git add -A` + `git diff --cached --name-status` in the sandbox,
       classified via the imported policy.Policy.is_forbidden() - this module never
       reimplements the forbidden-path rules (Program ruling 1: "the orchestrator owns
       the laws, not the model" - policy.py owns them, this module only calls it).
  A1  - no shared object store, ever: --local --no-hardlinks (a hardlinked pack rewritten
        by a sandbox `git gc`/`repack` could corrupt the MAIN repo's object DB). This
        module never runs gc/repack/prune anywhere, and the clone root is kept SHORT
        (MAX_PATH discipline) and parameterized, defaulting away from artifacts/.
  A2/A3 - path canonicalization + case-fold before every guard/classification decision.
        This module canonicalizes (backslash normalize, strip './', reject '..') before
        calling into policy.py; the actual case-fold matching is policy.py's own glob
        matcher's job (it already lower()s both sides - see policy.py's _match_segments),
        so this module does not duplicate that logic, only feeds it clean input.
  A9  - Windows process hygiene: destroy() kills the full process tree rooted in the
        clone directory (never a bare process kill) before removing files - an orphaned
        cl.exe/colosseum.exe holds .obj/DLL locks that would make rmtree fail.

Program ruling 7 (containment triad), part (b) DETECT: main_drift_snapshot()/
main_drift_check() capture and compare the MAIN repo's `git status --porcelain` +
untracked listing before/after any agent stage; any mismatch raises DriftViolation. This
module NEVER runs clean/stash/reset/gc/repack/prune on the MAIN repo, and never touches
the main `native/build-msvc` build tree.

Stdlib only (house pattern - see scripts/autorepair/policy.py, scripts/soak-digest.py).
No pip dependencies; only `git`, `cmd.exe`/PowerShell, and the pinned Qt/MSVC toolchain
paths already used by native/build-msvc.bat are shelled out to.

Public API (imported by later Guardian Loop slices - G4 triage, G6 repair, G7 verify):

    create(sha, incident_id, *, main_repo=REPO_ROOT, sandbox_root=DEFAULT_SANDBOX_ROOT) -> Path
    provision(clone, *, main_repo=REPO_ROOT) -> None
    build(clone, *, jobs=1) -> BuildResult
    extract_patch(clone, policy) -> {"testAdds": [...], "forbidden": [...], "production": [...]}
    main_drift_snapshot(main_repo=REPO_ROOT) -> DriftSnapshot
    main_drift_check(before) -> None                      # raises DriftViolation on mismatch
    destroy(clone) -> None

This module performs no automatic cleanup and no daemon/loop of its own - every function
is a single deliberate operation the orchestrator (a later slice) sequences explicitly.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

# scripts/autorepair/sandbox.py -> scripts/autorepair -> scripts -> repo root.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Sibling import (house pattern: flat scripts/autorepair/, no package __init__.py).
# Program ruling 1: policy.py owns the forbidden-path laws; this module only calls
# Policy.is_forbidden(), it never re-derives the rules itself.
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from policy import Policy, load_policy as _load_policy  # noqa: E402  (after sys.path setup, by design)

# Compatibility re-export: callers/tests historically import load_policy through sandbox.
load_policy = _load_policy

__all__ = [
    "REPO_ROOT",
    "DEFAULT_SANDBOX_ROOT",
    "SandboxError",
    "OriginNotRemovedError",
    "DriftViolation",
    "BuildResult",
    "DriftSnapshot",
    "load_policy",
    "create",
    "provision",
    "build",
    "extract_patch",
    "main_drift_snapshot",
    "main_drift_check",
    "destroy",
    "confirm_build_gate_clear",
]

# ── errors ───────────────────────────────────────────────────────────────────


class SandboxError(RuntimeError):
    """Sandbox lifecycle failure (create/provision/build/extract/destroy)."""


class OriginNotRemovedError(SandboxError):
    """D5/A1: a sandbox clone still has a git remote after 'remote remove origin' -
    refuse to proceed rather than risk a shared ref back to the main repo."""


class DriftViolation(RuntimeError):
    """Program ruling 7(b), containment triad DETECT: the MAIN repo's working tree
    changed between two main_drift_snapshot() calls - something leaked out of a sandbox
    stage. This is the tripwire; it is a hard stop, never a warning."""


# ── toolchain pins (mirrors native/build-msvc.bat exactly) ─────────────────────

CMAKE_EXE = Path("C:/Qt/Tools/CMake_64/bin/cmake.exe")
NINJA_EXE = Path("C:/Qt/Tools/Ninja/ninja.exe")
VCVARS64 = Path(
    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"
)
QT_PREFIX = "C:/Qt/6.11.1/msvc2022_64"
WINDEPLOYQT = Path("C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe")
POWERSHELL = Path("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")

# A1's MAX_PATH note: a short, non-artifacts/ default clone root. Parameterized -
# every function below accepts an explicit override; this is only the default.
DEFAULT_SANDBOX_ROOT = Path("C:/arsbx")

# The exact G0-proven runtime-DLL gap: CMake does not deploy these, so provision()
# copies them from the MAIN repo's existing, known-good build-msvc (read-only source).
MPV_DLLS = ("MpvQt.dll", "libmpv-2.dll")
FFMPEG_TOOLS_FILES = (
    "ffmpeg.exe",
    "ffprobe.exe",
    "avcodec-62.dll",
    "avdevice-62.dll",
    "avfilter-11.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "swresample-6.dll",
    "swscale-9.dll",
)

# Exit codes lie (house law) - grep the log instead of trusting the process return code.
_ERROR_SIGNAL_RE = re.compile(
    r"error C[0-9]|error LNK|ninja: build stopped|Cannot find source file", re.IGNORECASE
)

# Machine-wide one-build-at-a-time law (G0 proof: default ninja parallelism OOMs this
# machine - C1060 heap / LNK1102 out of memory).
_BUILD_GATE_PROCESS_NAMES = ("cl.exe", "ninja.exe", "link.exe")


# ── subprocess helper ───────────────────────────────────────────────────────


def _run(
    cmd: list[str],
    *,
    cwd: Path | str | None = None,
    timeout: int | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
    result = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd is not None else None,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if check and result.returncode != 0:
        raise SandboxError(
            f"command failed (exit {result.returncode}): {' '.join(cmd)}\n"
            f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
        )
    return result


# ── path canonicalization (A2/A3) ───────────────────────────────────────────


def _canonicalize(raw_path: str) -> str:
    """Normalize a git-reported path before classification: forward slashes, no leading
    './', and fail closed on any '..' traversal component (git itself should never emit
    one for a tracked diff path, but this stays defensive rather than trusting it). NTFS
    case-folding is deliberately NOT duplicated here - policy.py's own glob matcher
    already lower()s both operands (amendment A2's rule, applied once, at the matcher)."""
    text = raw_path.strip().replace("\\", "/")
    while text.startswith("./"):
        text = text[2:]
    parts = [p for p in text.split("/") if p not in ("", ".")]
    if ".." in parts:
        raise SandboxError(f"refusing a path with a traversal component: {raw_path!r}")
    return "/".join(parts)


def _is_under_tests(canon_path: str) -> bool:
    return canon_path == "tests" or canon_path.startswith("tests/")


# ── create() ─────────────────────────────────────────────────────────────────


def _assert_no_remotes(clone: Path) -> None:
    """The D5/A1 refusal: after 'remote remove origin', assert zero remotes survive.
    Factored out so a hermetic test can exercise this exact guard against a clone where
    origin removal was skipped, without forking create()'s own control flow."""
    remotes = _run(["git", "remote"], cwd=clone).stdout.strip()
    if remotes:
        raise OriginNotRemovedError(
            f"sandbox at {clone} still has git remote(s) after 'remote remove origin': "
            f"{remotes!r} - refusing to proceed (D5: zero shared refs with the main repo)"
        )


def create(
    sha: str,
    incident_id: str,
    *,
    main_repo: Path | str = REPO_ROOT,
    sandbox_root: Path | str = DEFAULT_SANDBOX_ROOT,
) -> Path:
    """
    D5: `git clone --local --no-hardlinks <main_repo> <sandbox_root>/<incident_id>`,
    remove the origin remote and ASSERT it is gone, then checkout <sha>. Never touches
    runtime DLLs - see provision() for that separate step.
    """
    main_repo_resolved = Path(main_repo).resolve()
    root = Path(sandbox_root)
    root.mkdir(parents=True, exist_ok=True)
    clone = (root / incident_id).resolve()
    if clone.exists():
        raise SandboxError(f"sandbox clone path already exists, refusing to overwrite: {clone}")

    _run(["git", "clone", "--local", "--no-hardlinks", str(main_repo_resolved), str(clone)])
    _run(["git", "remote", "remove", "origin"], cwd=clone)
    _assert_no_remotes(clone)
    _run(["git", "checkout", sha], cwd=clone)

    return clone


# ── provision() ──────────────────────────────────────────────────────────────


def provision(clone: Path | str, *, main_repo: Path | str = REPO_ROOT) -> None:
    """
    The exact runtime-DLL gap G0 found: CMake does not deploy Qt/mpv/ffmpeg alongside
    colosseum.exe. Requires build(clone) to have already produced <clone>/native/
    build-msvc/colosseum.exe. Copies from the MAIN repo's existing, known-good
    build-msvc/ - READ-ONLY on the main side, never written to.
    """
    clone = Path(clone).resolve()
    main_repo_resolved = Path(main_repo).resolve()

    build_dir = clone / "native" / "build-msvc"
    exe = build_dir / "colosseum.exe"
    if not exe.is_file():
        raise SandboxError(
            f"provision() requires a built sandbox exe first: {exe} does not exist - "
            "run build(clone) before provision(clone)"
        )

    qmldir = clone / "qml"
    _run([str(WINDEPLOYQT), "--qmldir", str(qmldir), str(exe)], timeout=600)

    main_build = (main_repo_resolved / "native" / "build-msvc").resolve()
    missing: list[str] = []

    for name in MPV_DLLS:
        src = main_build / name
        if not src.is_file():
            missing.append(str(src))
            continue
        shutil.copy2(src, build_dir / name)

    tools_dst = build_dir / "tools"
    tools_dst.mkdir(parents=True, exist_ok=True)
    for name in FFMPEG_TOOLS_FILES:
        src = main_build / "tools" / name
        if not src.is_file():
            missing.append(str(src))
            continue
        shutil.copy2(src, tools_dst / name)

    if missing:
        raise SandboxError(
            "provision(): missing runtime file(s) expected in the main repo's "
            "build-msvc (read-only source, never written to) - " + ", ".join(missing)
        )


# ── build() ──────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class BuildResult:
    ok: bool
    log_path: Path
    returncode: int
    error_lines: tuple[str, ...] = field(default_factory=tuple)
    timed_out: bool = False


def confirm_build_gate_clear() -> None:
    """Machine-wide one-build-at-a-time law: refuse to start a build if cl.exe/
    ninja.exe/link.exe is already running anywhere on this machine."""
    result = subprocess.run(["tasklist"], capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        raise SandboxError(f"could not query tasklist to confirm the build gate is clear: {result.stderr}")
    lowered = result.stdout.lower()
    running = [name for name in _BUILD_GATE_PROCESS_NAMES if name.lower() in lowered]
    if running:
        raise SandboxError(
            "build gate BUSY - refusing to start a sandbox build while another "
            f"compiler is running machine-wide: {running}"
        )


def build(
    clone: Path | str,
    *,
    log_path: Path | str | None = None,
    jobs: int = 1,
    timeout_sec: int = 7200,
) -> BuildResult:
    """
    Serialized behind confirm_build_gate_clear(). Configures with -DBUILD_TESTING=ON
    (mirrors native/build-msvc.bat plus the testing flag) and builds with `-j <jobs>`
    (default 1 - the G0-proven OOM guard). Grep-verifies the log for error signals;
    exit codes lie, per house law.
    """
    clone = Path(clone).resolve()
    confirm_build_gate_clear()

    native_dir = clone / "native"
    resolved_log_path = Path(log_path) if log_path else clone / "sandbox-build.log"

    bat_path = clone / "_sandbox_build.bat"
    bat_text = (
        "@echo off\r\n"
        "setlocal\r\n"
        f'cd /d "{native_dir}"\r\n'
        f'call "{VCVARS64}" >nul || (echo VCVARS_FAILED & exit /b 1)\r\n'
        f'"{CMAKE_EXE}" -S . -B build-msvc -G Ninja '
        f'-DCMAKE_MAKE_PROGRAM="{NINJA_EXE}" -DCMAKE_BUILD_TYPE=Release '
        f'-DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="{QT_PREFIX}" '
        "|| (echo CONFIGURE_FAILED & exit /b 1)\r\n"
        f'"{CMAKE_EXE}" --build build-msvc -- -j {jobs} '
        "|| (echo BUILD_FAILED & exit /b 1)\r\n"
        "echo SANDBOX_BUILD_OK\r\n"
    )
    bat_path.write_text(bat_text, encoding="utf-8")

    timed_out = False
    proc: subprocess.CompletedProcess | None = None
    try:
        proc = subprocess.run(
            ["cmd", "/c", str(bat_path)],
            capture_output=True,
            text=True,
            timeout=timeout_sec,
        )
        combined = proc.stdout + "\n" + proc.stderr
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        combined = (exc.stdout or "") + "\n" + (exc.stderr or "")

    resolved_log_path.write_text(combined, encoding="utf-8", errors="replace")

    error_lines = tuple(line for line in combined.splitlines() if _ERROR_SIGNAL_RE.search(line))
    returncode = proc.returncode if proc is not None else -1
    ok = (not timed_out) and returncode == 0 and not error_lines and "SANDBOX_BUILD_OK" in combined

    return BuildResult(
        ok=ok,
        log_path=resolved_log_path,
        returncode=returncode,
        error_lines=error_lines,
        timed_out=timed_out,
    )


# ── extract_patch() ──────────────────────────────────────────────────────────


def _classify_diff_line(line: str, policy_obj: Policy) -> tuple[str, str]:
    parts = line.split("\t")
    status = parts[0]

    if status[0] in ("R", "C"):
        # Rename/copy: "R100\told\tnew". Classified conservatively - forbidden if
        # EITHER the old path is forbidden to delete or the new path is forbidden to
        # modify; otherwise production. No G-slice test currently exercises a renamed
        # test-add path, so this stays a conservative default, not a modeled case.
        old_canon = _canonicalize(parts[1])
        new_canon = _canonicalize(parts[2])
        if policy_obj.is_forbidden(old_canon, "delete") or policy_obj.is_forbidden(new_canon, "modify"):
            return "forbidden", new_canon
        return "production", new_canon

    if len(parts) != 2:
        raise SandboxError(f"unexpected git diff --name-status line shape: {line!r}")
    canon = _canonicalize(parts[1])

    if status == "A":
        op = "add"
    elif status == "D":
        op = "delete"
    elif status in ("M", "T"):
        op = "modify"
    else:
        raise SandboxError(f"unrecognized git status code {status!r} for path {parts[1]!r}")

    # D6: a path ADDED under tests/ that is_forbidden() forgives (the addExempt door)
    # is a bug test. Everything else forbidden by law is forbidden; the remainder is
    # production. Checking is_forbidden() for the ADD case too (not just modify/delete)
    # correctly catches a forbidden ADD outside tests/ (e.g. a new file dropped straight
    # into docs/autorepair/, which has no addExempt entry of its own).
    if op == "add" and _is_under_tests(canon) and not policy_obj.is_forbidden(canon, "add"):
        return "testAdds", canon
    if policy_obj.is_forbidden(canon, op):
        return "forbidden", canon
    return "production", canon


def extract_patch(clone: Path | str, policy_obj: Policy) -> dict[str, list[str]]:
    """
    D6: `git add -A` then `git diff --cached --name-status` in the sandbox, classified
    per path via the imported policy.Policy.is_forbidden() - never a reimplementation of
    the forbidden-path rules. Returns {"testAdds": [...], "forbidden": [...],
    "production": [...]}, each a list of canonicalized (forward-slash) repo-relative
    paths.
    """
    clone = Path(clone).resolve()
    _run(["git", "add", "-A"], cwd=clone)
    result = _run(["git", "diff", "--cached", "--name-status"], cwd=clone)

    buckets: dict[str, list[str]] = {"testAdds": [], "forbidden": [], "production": []}
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        bucket, canon = _classify_diff_line(line, policy_obj)
        buckets[bucket].append(canon)
    return buckets


# ── drift tripwire (Program ruling 7b) ──────────────────────────────────────


@dataclass(frozen=True)
class DriftSnapshot:
    repo: str
    status_porcelain: str
    untracked: tuple[str, ...]


def main_drift_snapshot(main_repo: Path | str = REPO_ROOT) -> DriftSnapshot:
    """Captures a repo's `git status --porcelain` + untracked listing. Named "main_" for
    the MAIN-repo use case this slice requires, but the function itself is repo-agnostic
    (any git working tree) - later slices (G5) reuse it for sandbox-side drift too."""
    main_repo_resolved = Path(main_repo).resolve()
    status = _run(
        ["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=main_repo_resolved
    ).stdout
    untracked = _run(
        ["git", "ls-files", "--others", "--exclude-standard"], cwd=main_repo_resolved
    ).stdout.splitlines()
    return DriftSnapshot(
        repo=str(main_repo_resolved), status_porcelain=status, untracked=tuple(sorted(untracked))
    )


def main_drift_check(before: DriftSnapshot) -> None:
    """Re-snapshots `before.repo` and compares against `before`. Raises DriftViolation
    naming exactly what changed on any mismatch; returns None (silently) when clean."""
    after = main_drift_snapshot(before.repo)
    if after.status_porcelain == before.status_porcelain and after.untracked == before.untracked:
        return

    added_untracked = sorted(set(after.untracked) - set(before.untracked))
    removed_untracked = sorted(set(before.untracked) - set(after.untracked))
    raise DriftViolation(
        "MAIN repo drifted between snapshots (Program ruling 7b, containment triad "
        f"DETECT) - repo={before.repo}; "
        f"status_changed={after.status_porcelain != before.status_porcelain}; "
        f"added_untracked={added_untracked}; removed_untracked={removed_untracked}"
    )


# ── destroy() ────────────────────────────────────────────────────────────────


def _kill_process_tree_rooted_in(clone: Path) -> None:
    """A9: kill (taskkill /T /F) any process whose executable lives inside the sandbox
    clone - never a bare process kill, because an orphaned cl.exe/colosseum.exe holds
    .obj/DLL file locks that would make the rmtree below fail. `wmic` is unavailable on
    this machine (Windows 11); PowerShell's Win32_Process CIM class is used instead.
    Best-effort: any failure here still falls through to the rmtree attempt."""
    clone_str = str(clone.resolve())
    escaped = clone_str.replace("'", "''")
    ps_cmd = (
        "Get-CimInstance Win32_Process | "
        f"Where-Object {{ $_.ExecutablePath -and $_.ExecutablePath.StartsWith('{escaped}', "
        "[System.StringComparison]::OrdinalIgnoreCase) } | "
        "Select-Object -ExpandProperty ProcessId"
    )
    try:
        result = subprocess.run(
            [str(POWERSHELL), "-NoProfile", "-NonInteractive", "-Command", ps_cmd],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return
    if result.returncode != 0:
        return
    for line in result.stdout.splitlines():
        pid = line.strip()
        if pid.isdigit():
            subprocess.run(
                ["taskkill", "/PID", pid, "/T", "/F"], capture_output=True, text=True, timeout=30
            )


def _rmtree_retry(path: Path, attempts: int = 5, delay_sec: float = 1.0) -> None:
    last_exc: Exception | None = None
    for _ in range(attempts):
        try:
            shutil.rmtree(path)
            return
        except OSError as exc:
            last_exc = exc
            time.sleep(delay_sec)
    raise SandboxError(f"destroy(): could not remove sandbox dir {path} after {attempts} attempts: {last_exc}")


def destroy(clone: Path | str) -> None:
    """Kills any process rooted in the clone directory (A9), then removes the directory
    tree. A no-op (not an error) if the clone directory does not exist."""
    clone = Path(clone).resolve()
    if not clone.exists():
        return
    _kill_process_tree_rooted_in(clone)
    _rmtree_retry(clone)
