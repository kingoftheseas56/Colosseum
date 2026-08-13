#!/usr/bin/env python3
"""lanista-mcp — stdio MCP adapter over Colosseum's dev-control bridge.

Stdlib only, deliberately: the app carries no new dependency and neither does
the adapter. v0 (Slice F, 2026-08-12) grows the original 3-tool passthrough
into 8 typed tools with real deadlines PLUS the original 3, kept working
unchanged in name and behavior:

  session_start / session_stop  — an INTERACTIVE session this adapter owns
      (spawns colosseum.exe itself, on a unique generated pipe + tagged
      AppData root; one live session at a time in v0).
  act / get / snapshot / wait_for / grab / warnings — drive/read the live
      session the way the Claude Code browser pane is driven: look, decide,
      act, look again.
  lanista_call / lanista_grab / lanista_snapshot — the original 3, unchanged
      in name and target (whatever COLOSSEUM_LANISTA_PIPE resolves to at
      call time — daily app or an externally-managed session), now routed
      through the SAME deadline-safe transport as everything else.

F1-Bridge (Agent Visibility Phase 2, 2026-08-13) adds a 9th typed tool,
12 total: vault_forensics(scope, key?, limit?, timeoutMs?) — shells the same
"vault-forensics" bridge command the CLI/facade both use, on the active
session, preserving v0's deadline/backstop pattern.

PROTOCOL RULING (Agent 0, 2026-08-12, do not deviate): this file stays on
the hand-rolled JSON-RPC 2024-11-05 base — no SDK, no protocol version bump,
no Tasks. All twelve tools are plain tools/call and need nothing newer; hosts
negotiate down. The Night Watch (N0/N1) is where a protocol upgrade belongs,
not here.

THE DEADLINE FIX (the ledger-documented flaw this slice closes): the old
`pipe_call()` opened the named pipe directly and did an UNBOUNDED blocking
read — a hung app hung the adapter forever. v0 NEVER touches the pipe from
Python. Every single interaction — including the 3 legacy tools — shells the
existing `lanista` CLI (native/build-msvc/lanista.exe), which already owns a
hardened, timeout-bounded QLocalSocket client (connect/write/drain-on-
disconnect), with an explicit `--timeout` on every invocation, PLUS a Python-
side subprocess timeout as a backstop. This is one automation stack, not two:
the CLI's socket code is reused, never re-implemented.
"""
import base64
import ctypes
import json
import os
import shutil
import subprocess
import sys
import time
import uuid
from datetime import datetime

# ── paths (resolved from this file, never from inherited cwd) ──────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
LANISTA_EXE = os.path.join(REPO_ROOT, "native", "build-msvc", "lanista.exe")
COLOSSEUM_EXE = os.path.join(REPO_ROOT, "native", "build-msvc", "colosseum.exe")
QML_MAIN = "qml/Main.qml"                      # relative to REPO_ROOT, matches lanista.cpp's default
SESSIONS_DIR = os.path.join(REPO_ROOT, "artifacts", "lanista-sessions")
ACTIVE_POINTER = os.path.join(SESSIONS_DIR, "_mcp-active.json")
WARNING_GATE_PS1 = os.path.join(REPO_ROOT, "tests", "warning_gate.ps1")

# The legacy 3 tools' target — unchanged resolution from the original file:
# whatever COLOSSEUM_LANISTA_PIPE names at call time, default the daily pipe.
LEGACY_PIPE = os.environ.get("COLOSSEUM_LANISTA_PIPE", "ColosseumLanista")

DEFAULT_CMD_TIMEOUT_MS = 8000
READY_TIMEOUT_MS = 60000        # ledger operational note: 30s default times out on a loaded box
GRAB_CLIENT_TIMEOUT_MS = 10000  # mirrors the CLI's own forced floor for --grab calls

# One live session at a time in v0. This in-process dict is the fast path;
# ACTIVE_POINTER on disk is the cross-process/crash-recovery source of truth
# (a second server.py instance, or this one restarted after a crash, must
# still refuse to double-book a session).
SESSION = {"active": False}
LAST_SESSION = None   # kept after session_stop so warnings() can still read its logs


# ── small OS helpers (stdlib/ctypes only — no pywin32) ──────────────────────

def _pid_alive(pid):
    """Ground-truth a pid via tasklist, never assume from a stale file."""
    try:
        out = subprocess.run(
            ["tasklist", "/FI", "PID eq {}".format(pid), "/NH"],
            capture_output=True, text=True, timeout=10)
    except Exception:
        return False
    return str(pid) in out.stdout


def _post_wm_close(pid):
    """Broadcast WM_CLOSE to every top-level window owned by pid — the same
    graceful-shutdown path Qt's QProcess::terminate() takes on Windows
    (posts WM_CLOSE to toplevel windows, then the main thread). Python's own
    Popen.terminate() calls TerminateProcess directly (immediate kill, no
    grace) on Windows, so this is done by hand via ctypes (stdlib) to give
    session_stop a genuine graceful-then-kill shape, not terminate-then-kill."""
    user32 = ctypes.windll.user32
    WM_CLOSE = 0x0010
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def _enum(hwnd, _lparam):
        pid_out = ctypes.c_ulong(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid_out))
        if pid_out.value == pid:
            found.append(hwnd)
        return True

    user32.EnumWindows(_enum, 0)
    for hwnd in found:
        user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    return len(found)


def _kill_and_wait(proc, timeout=5):
    """proc.kill() then bound the wait — a slow-to-die child (crashed, or a
    Windows Error Reporting dialog holding it open) must never make a kill
    PATH itself raise an uncaught TimeoutExpired: that would turn a clean
    refusal/self-kill into an ugly protocol-level error instead of a coded
    tool result. Ground-truthed live 2026-08-12: an app crash mid-session
    left exactly this call site throwing before this fix."""
    try:
        proc.kill()
    except Exception:
        pass
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        pass


def _new_session_id():
    return (datetime.now().strftime("%Y%m%d-%H%M%S") + "-"
            + uuid.uuid4().hex[:8])


def _write_json(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


# ── the ONE transport: shell the lanista CLI, always with an explicit deadline ──

def _kv(key, value):
    """Render one k=v token the CLI's payloadFromArgs() will type correctly
    (bool -> true/false, else Python's own str())."""
    if isinstance(value, bool):
        return "{}={}".format(key, "true" if value else "false")
    return "{}={}".format(key, value)


def run_lanista(cmd, pipe, extra_args=None, timeout_ms=DEFAULT_CMD_TIMEOUT_MS,
                 grab_target=None):
    """One plain `lanista --pipe P --timeout T <cmd> [k=v...] [--grab target]`
    call. Returns (exitCode, parsedReplyOrNone, rawStdout, rawStderr). NEVER
    touches the pipe itself — subprocess-level timeout is a hard backstop
    beyond the CLI's own internal deadline, in case the exe itself wedges."""
    args = [LANISTA_EXE, "--pipe", pipe, "--timeout", str(timeout_ms), cmd]
    if extra_args:
        args += extra_args
    if grab_target:
        args += ["--grab", grab_target]
    # A grab forces the CLI's OWN client deadline to a flat 10s regardless of
    # --timeout (main() : `grabName.isEmpty() ? g_timeout : 10000`) — our
    # backstop must cover that floor too, or a shorter caller-supplied
    # timeoutMs would make Python kill the subprocess before the CLI's own
    # internal wait finishes.
    effective_ms = max(timeout_ms, 10000) if grab_target else timeout_ms
    backstop_s = (effective_ms / 1000.0) + 5.0
    try:
        proc = subprocess.run(args, cwd=REPO_ROOT, capture_output=True,
                               text=True, timeout=backstop_s)
    except subprocess.TimeoutExpired:
        return 4, None, "", "MCP_ADAPTER_TIMEOUT: lanista.exe did not return within {}s".format(backstop_s)
    reply = None
    if proc.stdout.strip():
        try:
            reply = json.loads(proc.stdout)
        except ValueError:
            reply = None
    return proc.returncode, reply, proc.stdout, proc.stderr


def run_lanista_scenario_verbose(pipe, cmd, payload, timeout_ms=DEFAULT_CMD_TIMEOUT_MS,
                                  scratch_dir=None, label="step"):
    """For payload shapes the plain k=v CLI mode structurally cannot build
    (an array-valued field like qml-get's `props`) — write a one-step scratch
    scenario and run it with --verbose, which prints the step's FULL reply
    body to stdout (documented CLI facility: "use it while iterating instead
    of forcing failures to see values", ledger "Blind gates" trap note). No
    `expect` clause is set, so the step always reports PASS regardless of the
    data — this call is data retrieval, not an assertion. Timeout mirrors the
    scenario engine's own defensive floor+slack rule (10s floor, wait_ms+5s)."""
    scratch_dir = scratch_dir or SESSIONS_DIR
    os.makedirs(scratch_dir, exist_ok=True)
    scratch_path = os.path.join(
        scratch_dir, "scratch-{}-{}.json".format(label, uuid.uuid4().hex[:8]))
    scenario = {"name": "mcp-scratch-" + label,
                "steps": [{"cmd": cmd, "label": label, "payload": payload}]}
    _write_json(scratch_path, scenario)

    wait_ms = int(payload.get("timeout_ms", 0)) if isinstance(payload, dict) else 0
    cli_timeout_ms = max(timeout_ms, 10000, wait_ms + 5000 if wait_ms else 0)
    backstop_s = (cli_timeout_ms / 1000.0) + 5.0
    args = [LANISTA_EXE, "--pipe", pipe, "--verbose", "run", scratch_path]
    try:
        proc = subprocess.run(args, cwd=REPO_ROOT, capture_output=True,
                               text=True, timeout=backstop_s)
    except subprocess.TimeoutExpired:
        return 4, None, "", "MCP_ADAPTER_TIMEOUT: lanista.exe did not return within {}s".format(backstop_s)

    reply = None
    prefix = "  reply {}: ".format(label)
    for line in proc.stdout.splitlines():
        if line.startswith(prefix):
            try:
                reply = json.loads(line[len(prefix):])
            except ValueError:
                reply = None
            break
    return proc.returncode, reply, proc.stdout, proc.stderr


def _err(message, code=None):
    body = {"error": message}
    if code:
        body["code"] = code
    return {"content": [{"type": "text", "text": json.dumps(body, indent=2)}],
            "isError": True}


def _ok(obj):
    return {"content": [{"type": "text", "text": json.dumps(obj, indent=2)}]}


# ── session_start / session_stop ────────────────────────────────────────────

def _load_active_pointer():
    return _read_json(ACTIVE_POINTER)


def _clear_active_pointer():
    try:
        os.remove(ACTIVE_POINTER)
    except FileNotFoundError:
        pass


def tool_session_start(args):
    global SESSION, LAST_SESSION

    # 1. one-live-session-at-a-time — cross-process source of truth first.
    pointer = _load_active_pointer()
    if pointer and _pid_alive(pointer.get("pid")):
        return _err("a Lanista session is already active: id={} pid={} pipe={} "
                     "— stop it first".format(pointer.get("sessionId"),
                                               pointer.get("pid"), pointer.get("pipe")))
    if pointer:
        # Stale pointer: the recorded pid is gone — a crash, not a live session.
        _clear_active_pointer()
    if SESSION.get("active"):
        return _err("a Lanista session is already active in this adapter process: "
                     "id={}".format(SESSION.get("id")))

    seed_name = args.get("seedName")
    tag_arg = args.get("tag")
    drive = bool(args.get("drive", False))
    # Test-only hooks, deliberately NOT part of the public inputSchema — see
    # the negative-control notes in this slice's report. They exercise the
    # exact same guard code a normal call runs, forcing an input the normal
    # generator can never organically produce (mirrors the CLI's own
    # equivalently-unreachable-by-construction default-pipe guard).
    force_pipe = args.get("_forcePipe")
    force_isolation_mismatch = bool(args.get("_forceIsolationMismatch", False))

    session_id = _new_session_id()
    tag = tag_arg or session_id
    pipe = force_pipe if force_pipe else "ColosseumLanista-{}".format(session_id)

    # 2. the daily-app safety line — BEFORE anything is spawned or touched.
    if pipe == "ColosseumLanista":
        return _err("refusing the daily app's default pipe name — a test session "
                     "must never land on it", code="DEFAULT_PIPE_REFUSED")

    # 3. seed resolution.
    seed_dir = None
    if seed_name:
        seed_dir = os.path.join(REPO_ROOT, "tests", "lanista-seeds", seed_name)
        if not os.path.isdir(seed_dir) or not os.path.isfile(
                os.path.join(seed_dir, "seed.json")):
            return _err("seed not found or missing seed.json: {}".format(seed_dir))

    if not os.path.isfile(COLOSSEUM_EXE):
        return _err("exe not found: {}".format(COLOSSEUM_EXE))

    session_dir = os.path.join(SESSIONS_DIR, session_id)
    os.makedirs(session_dir, exist_ok=True)

    # 4. seed placement — the J0 manifest's whole tree copies into the tagged
    # Roaming AppData root, matching commit 4ebec25's proven `--seed` behavior
    # (copies the ENTIRE seed dir, seed.json included — harmless, unread by
    # the app). org=Brotherhood, app=Colosseum-dltest-<tag> under
    # COLOSSEUM_APPDATA_TAG (native/main.cpp:520-560) resolves AppDataLocation
    # to exactly %APPDATA%/Brotherhood/Colosseum-dltest-<tag> on Windows.
    roaming = os.environ.get("APPDATA")
    if not roaming:
        return _err("APPDATA environment variable is not set — cannot place seed or "
                     "predict the isolated AppData root")
    expected_app_data = os.path.join(roaming, "Brotherhood",
                                      "Colosseum-dltest-{}".format(tag))
    if seed_dir:
        try:
            shutil.copytree(seed_dir, expected_app_data, dirs_exist_ok=True)
        except Exception as exc:
            return _err("seed copy failed into {}: {}".format(expected_app_data, exc))

    # 5. spawn — explicit exe path, unique pipe, tagged AppData, optional Drive.
    env = os.environ.copy()
    env["COLOSSEUM_LANISTA_PIPE"] = pipe
    env["COLOSSEUM_APPDATA_TAG"] = tag
    if drive:
        env["COLOSSEUM_LANISTA_DRIVE"] = "1"
    env["QT_FORCE_STDERR_LOGGING"] = "1"

    stdout_path = os.path.join(session_dir, "stdout.log")
    stderr_path = os.path.join(session_dir, "stderr.log")
    stdout_f = open(stdout_path, "wb")
    stderr_f = open(stderr_path, "wb")

    launched_at = datetime.now().isoformat()
    try:
        proc = subprocess.Popen([COLOSSEUM_EXE, QML_MAIN], cwd=REPO_ROOT, env=env,
                                 stdout=stdout_f, stderr=stderr_f)
    except Exception as exc:
        stdout_f.close(); stderr_f.close()
        return _err("process failed to start: {}".format(exc))

    # 6. readiness — ping the SESSION pipe until it answers with OUR pid.
    deadline = time.monotonic() + (READY_TIMEOUT_MS / 1000.0)
    ready_at = None
    while time.monotonic() < deadline:
        rc, reply, _out, _errtext = run_lanista("ping", pipe, timeout_ms=2000)
        if reply and reply.get("type") == "reply":
            reported_pid = int(reply.get("pid", -1))
            if reported_pid != proc.pid:
                _kill_and_wait(proc)
                stdout_f.close(); stderr_f.close()
                return _err("pipe answered with a foreign pid ({} != {}) — a stranger "
                             "owns this pipe name".format(reported_pid, proc.pid))
            ready_at = datetime.now().isoformat()
            break
        if proc.poll() is not None:
            stdout_f.close(); stderr_f.close()
            return _err("app exited before ready (code {}) — see {}".format(
                proc.returncode, stderr_path))
        time.sleep(0.25)
    if ready_at is None:
        _kill_and_wait(proc)
        stdout_f.close(); stderr_f.close()
        return _err("bridge never became ready within {} ms".format(READY_TIMEOUT_MS))

    # 7. isolation proof — from the app's OWN get-state report, never assumed.
    rc, st, _out, _errtext = run_lanista("get-state", pipe, timeout_ms=5000)
    app_data_root = (st or {}).get("appDataRoot", "")
    cache_root = (st or {}).get("cacheRoot", "")
    marker = "Colosseum-dltest-{}".format(tag)
    check_marker = marker
    if force_isolation_mismatch:
        # Simulates a stripped/corrupted isolation tag: check the REAL
        # reported roots (which DO carry the true tag) against a marker that
        # deliberately cannot match, proving the self-kill path fires against
        # a real live child, not a mocked one.
        check_marker = marker + "-forced-mismatch"
    if check_marker not in app_data_root or check_marker not in cache_root:
        killed_pid = proc.pid
        _kill_and_wait(proc)
        stdout_f.close(); stderr_f.close()
        return _err("ISOLATION FAILED — appDataRoot={} cacheRoot={} (expected marker "
                     "{}) — session self-killed (pid {})".format(
                         app_data_root, cache_root, check_marker, killed_pid),
                     code="ISOLATION_FAILED")

    manifest = {
        "schema": "colosseum.mcp-session.v1",
        "sessionId": session_id, "tag": tag, "pipe": pipe,
        "exe": COLOSSEUM_EXE, "qml": QML_MAIN, "pid": proc.pid, "drive": drive,
        "seedName": seed_name, "launchedAt": launched_at, "readyAt": ready_at,
        "appDataRoot": app_data_root, "cacheRoot": cache_root,
        "dir": session_dir, "stdoutPath": stdout_path, "stderrPath": stderr_path,
        "state": "active",
    }
    _write_json(os.path.join(session_dir, "session.json"), manifest)
    _write_json(ACTIVE_POINTER, {"sessionId": session_id, "pid": proc.pid,
                                  "pipe": pipe, "tag": tag, "dir": session_dir,
                                  "startedAt": launched_at})

    SESSION = {"active": True, "id": session_id, "pipe": pipe, "tag": tag,
               "pid": proc.pid, "proc": proc, "dir": session_dir,
               "appDataRoot": app_data_root, "cacheRoot": cache_root,
               "drive": drive, "seedName": seed_name,
               "stdout_f": stdout_f, "stderr_f": stderr_f,
               "stdoutPath": stdout_path, "stderrPath": stderr_path,
               "manifest_path": os.path.join(session_dir, "session.json")}
    LAST_SESSION = SESSION
    return _ok({"sessionId": session_id, "pipe": pipe, "tag": tag, "pid": proc.pid,
                "drive": drive, "seedName": seed_name,
                "appDataRoot": app_data_root, "cacheRoot": cache_root,
                "dir": session_dir, "readyAt": ready_at})


def _require_active_session():
    if not SESSION.get("active"):
        return _err("no active session — call session_start first", code="NO_SESSION")
    proc = SESSION.get("proc")
    if proc is not None and proc.poll() is not None:
        SESSION["active"] = False
        return _err("session process already exited (code {}) — call session_start "
                     "to begin a new one".format(proc.returncode), code="SESSION_DEAD")
    return None


def tool_session_stop(_args):
    global SESSION
    if not SESSION.get("active"):
        return _err("no active session to stop", code="NO_SESSION")

    proc = SESSION["proc"]
    kill_reason = "graceful"
    still_running = proc.poll() is None
    if still_running:
        try:
            closed = _post_wm_close(proc.pid)
        except Exception:
            closed = 0
        deadline = time.monotonic() + 8.0
        while proc.poll() is None and time.monotonic() < deadline:
            time.sleep(0.25)
        if proc.poll() is None:
            _kill_and_wait(proc)
            kill_reason = "killed after graceful timeout (posted WM_CLOSE to {} window(s))".format(closed)
        elif closed == 0:
            kill_reason = "exited on its own before WM_CLOSE reached any window"

    exit_code = proc.returncode
    manifest = _read_json(SESSION["manifest_path"]) or {}
    manifest.update({
        "state": "stopped",
        "exitedAt": datetime.now().isoformat(),
        "exitCode": exit_code,
        "killReason": kill_reason,
    })
    _write_json(SESSION["manifest_path"], manifest)
    _clear_active_pointer()

    try:
        SESSION["stdout_f"].close()
        SESSION["stderr_f"].close()
    except Exception:
        pass

    result = {"sessionId": SESSION["id"], "exitCode": exit_code,
              "killReason": kill_reason, "dir": SESSION["dir"]}
    SESSION["active"] = False
    return _ok(result)


# ── act / get / snapshot / wait_for / grab / warnings ───────────────────────

def tool_act(args):
    guard = _require_active_session()
    if guard:
        return guard
    action = args.get("action")
    target = args.get("target")
    timeout_ms = int(args.get("timeoutMs", DEFAULT_CMD_TIMEOUT_MS))
    pipe = SESSION["pipe"]

    if action == "click":
        if not target:
            return _err("act(click) needs target")
        rc, reply, _out, errtext = run_lanista("ui-click", pipe,
                                                [_kv("target", target)], timeout_ms)
    elif action == "keypress":
        key = args.get("key")
        if not key:
            return _err("act(keypress) needs key")
        rc, reply, _out, errtext = run_lanista("ui-keypress", pipe,
                                                [_kv("key", key)], timeout_ms)
    elif action == "text-input":
        text = args.get("text", "")
        if not target:
            return _err("act(text-input) needs target")
        rc, reply, _out, errtext = run_lanista(
            "ui-text-input", pipe, [_kv("target", target), _kv("text", text)], timeout_ms)
    elif action == "scroll":
        if not target:
            return _err("act(scroll) needs target")
        dy = args.get("dy", -120)
        rc, reply, _out, errtext = run_lanista(
            "ui-scroll", pipe, [_kv("target", target), _kv("dy", dy)], timeout_ms)
    else:
        return _err("unknown act action: {} (expected click/keypress/text-input/scroll)"
                     .format(action))

    if reply is None:
        return _err("act({}) got no parseable reply: {}".format(action, errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": reply.get("type") != "reply"}


def tool_get(args):
    guard = _require_active_session()
    if guard:
        return guard
    target = args.get("target")
    props = args.get("props") or []
    if not target or not isinstance(props, list) or not props:
        return _err("get(target, props) needs a target and a non-empty props array")
    timeout_ms = int(args.get("timeoutMs", DEFAULT_CMD_TIMEOUT_MS))
    payload = {"object": target, "props": props}
    rc, reply, _out, errtext = run_lanista_scenario_verbose(
        SESSION["pipe"], "qml-get", payload, timeout_ms,
        scratch_dir=os.path.join(SESSION["dir"], "scratch"), label="get")
    if reply is None:
        return _err("get() got no parseable reply: {}".format(errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": reply.get("type") != "reply"}


def tool_snapshot(args):
    guard = _require_active_session()
    if guard:
        return guard
    timeout_ms = int(args.get("timeoutMs", DEFAULT_CMD_TIMEOUT_MS))
    rc, reply, _out, errtext = run_lanista("ui-snapshot", SESSION["pipe"],
                                            timeout_ms=timeout_ms)
    if reply is None:
        return _err("snapshot() got no parseable reply: {}".format(errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": reply.get("type") != "reply"}


def tool_wait_for(args):
    guard = _require_active_session()
    if guard:
        return guard
    target = args.get("target")
    prop = args.get("prop")
    value = args.get("value")
    if not target or not prop or "value" not in args:
        return _err("wait_for(target, prop, value, timeoutMs?) needs target, prop, value")
    timeout_ms = int(args.get("timeoutMs", 3000))
    # The client deadline must OUTLIVE the server's own poll deadline — same
    # floor+slack rule the scenario engine uses (10s floor, wait_ms + 5s).
    cli_timeout_ms = max(10000, timeout_ms + 5000)
    extra = [_kv("object", target), _kv("prop", prop), _kv("value", value),
             _kv("timeout_ms", timeout_ms)]
    rc, reply, _out, errtext = run_lanista("ui-wait-for", SESSION["pipe"], extra,
                                            cli_timeout_ms)
    if reply is None:
        return _err("wait_for() got no parseable reply: {}".format(errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": reply.get("type") != "reply"}


def tool_grab(args):
    guard = _require_active_session()
    if guard:
        return guard
    target = args.get("target")
    if not target:
        return _err("grab(target) needs a target")
    timeout_ms = int(args.get("timeoutMs", GRAB_CLIENT_TIMEOUT_MS))
    rc, reply, _out, errtext = run_lanista("get-state", SESSION["pipe"],
                                            timeout_ms=timeout_ms, grab_target=target)
    if reply is None:
        return _err("grab() got no parseable reply: {}".format(errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    path = reply.get("grabPath", "")
    if path and os.path.exists(path):
        with open(path, "rb") as f:
            data = base64.b64encode(f.read()).decode()
        return {"content": [
            {"type": "image", "data": data, "mimeType": "image/png"},
            {"type": "text", "text": json.dumps(
                {k: v for k, v in reply.items() if k != "grabPath"})}]}
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": True}


VAULT_FORENSICS_MIN_TIMEOUT_MS = 200
VAULT_FORENSICS_MAX_TIMEOUT_MS = 30000


def tool_vault_forensics(args):
    """F1-Bridge: one typed call onto F1-Core's bounded Vault projection
    (VaultForensics) through the active session's "vault-forensics" bridge
    command. Same transport/session-ownership contract as act/get/snapshot —
    requires an active session, shells the lanista CLI, never touches the
    pipe directly. The reply is passed through UNCHANGED (schema
    colosseum.vault.forensics.v1), matching the bridge's own pass-through
    contract — this adapter layer does not reshape it either."""
    guard = _require_active_session()
    if guard:
        return guard
    scope = args.get("scope")
    if not scope:
        return _err("vault_forensics(scope, key?, limit?, timeoutMs?) needs scope")

    # Bridge-level deadline (forwarded as the "timeoutMs" payload field the C++ handler
    # reads for VaultForensics::queryMarshalled) — clamped so neither an unbounded wait
    # nor a too-small one (starving the owner-thread degrade path) can be requested.
    timeout_ms = int(args.get("timeoutMs", DEFAULT_CMD_TIMEOUT_MS))
    timeout_ms = max(VAULT_FORENSICS_MIN_TIMEOUT_MS,
                      min(VAULT_FORENSICS_MAX_TIMEOUT_MS, timeout_ms))

    kv = [_kv("scope", scope)]
    if args.get("key") is not None:
        kv.append(_kv("key", args["key"]))
    if args.get("limit") is not None:
        kv.append(_kv("limit", args["limit"]))
    kv.append(_kv("timeoutMs", timeout_ms))

    # The CLI's OWN client deadline must outlive the bridge's bounded wait — same
    # floor+slack rule wait_for() already uses (10s floor, requested + 5s), so a hung
    # owner-thread wait can never make Python give up before the bridge's own bounded
    # wait would have returned a coded error.
    cli_timeout_ms = max(10000, timeout_ms + 5000)
    rc, reply, _out, errtext = run_lanista("vault-forensics", SESSION["pipe"], kv,
                                            cli_timeout_ms)
    if reply is None:
        return _err("vault_forensics() got no parseable reply: {}".format(errtext),
                     code="INFRA" if rc == 4 else "NO_REPLY")
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}],
            "isError": reply.get("type") != "reply"}


def tool_warnings(_args):
    if LAST_SESSION is None:
        return _err("no session has been started yet in this adapter process — "
                     "warnings() reads a session's own logs", code="NO_SESSION")
    log_colosseum = os.path.join(LAST_SESSION["appDataRoot"], "logs", "colosseum.log")
    log_stderr = LAST_SESSION["stderrPath"]
    log_arg = ",".join([log_colosseum, log_stderr])
    args = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", WARNING_GATE_PS1, "-LogPath", log_arg]
    try:
        proc = subprocess.run(args, cwd=REPO_ROOT, capture_output=True,
                               text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return _err("warning_gate.ps1 did not return within 30s", code="MCP_ADAPTER_TIMEOUT")
    verdict = proc.stdout.strip()
    ok = proc.returncode == 0
    return {"content": [{"type": "text", "text": json.dumps(
        {"verdict": verdict, "exitCode": proc.returncode,
         "logPaths": [log_colosseum, log_stderr]}, indent=2)}],
            "isError": not ok}


# ── legacy 3 tools — same names, same target, deadline-safe transport ──────

def tool_lanista_call(args):
    cmd = args.get("cmd")
    payload = args.get("payload") or {}
    if not cmd:
        raise RpcError(-32602, "lanista_call needs cmd")
    wait_ms = int(payload.get("timeout_ms", 0)) if isinstance(payload, dict) else 0
    timeout_ms = max(DEFAULT_CMD_TIMEOUT_MS, wait_ms + 5000 if wait_ms else 0)
    kv = [_kv(k, v) for k, v in payload.items()] if isinstance(payload, dict) else []
    # A payload with any non-scalar value (array/object) cannot ride k=v —
    # fall back to the scratch-scenario+verbose path exactly like get() does.
    if any(isinstance(v, (list, dict)) for v in (payload.values() if isinstance(payload, dict) else [])):
        rc, reply, _out, errtext = run_lanista_scenario_verbose(
            LEGACY_PIPE, cmd, payload, timeout_ms,
            scratch_dir=SESSIONS_DIR, label="legacy-call")
    else:
        rc, reply, _out, errtext = run_lanista(cmd, LEGACY_PIPE, kv, timeout_ms)
    if reply is None:
        reply = {"type": "error", "code": "INFRA" if rc == 4 else "NO_REPLY",
                 "message": errtext}
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}]}


def tool_lanista_snapshot(_args):
    rc, reply, _out, errtext = run_lanista("ui-snapshot", LEGACY_PIPE,
                                            timeout_ms=DEFAULT_CMD_TIMEOUT_MS)
    if reply is None:
        reply = {"type": "error", "code": "INFRA" if rc == 4 else "NO_REPLY",
                 "message": errtext}
    return {"content": [{"type": "text", "text": json.dumps(reply, indent=2)}]}


def tool_lanista_grab(args):
    target = args.get("target")
    if not target:
        raise RpcError(-32602, "lanista_grab needs target")
    rc, reply, _out, errtext = run_lanista("get-state", LEGACY_PIPE,
                                            timeout_ms=GRAB_CLIENT_TIMEOUT_MS,
                                            grab_target=target)
    if reply is None:
        return {"content": [{"type": "text", "text": json.dumps(
            {"error": errtext}, indent=2)}], "isError": True}
    path = reply.get("grabPath", "")
    if path and os.path.exists(path):
        with open(path, "rb") as f:
            data = base64.b64encode(f.read()).decode()
        return {"content": [
            {"type": "image", "data": data, "mimeType": "image/png"},
            {"type": "text", "text": json.dumps(
                {k: v for k, v in reply.items() if k != "grabPath"})}]}
    return {"content": [{"type": "text", "text": json.dumps(reply)}], "isError": True}


TOOLS = [
    {"name": "session_start",
     "description": "Start an isolated, interactive Colosseum session this adapter "
                    "owns: unique generated pipe (the daily default is refused), "
                    "tagged AppData root, optional J0 seed placement, optional "
                    "Drive gate. One live session at a time in v0.",
     "inputSchema": {"type": "object",
                     "properties": {
                         "seedName": {"type": "string",
                                      "description": "a folder under tests/lanista-seeds/, e.g. vault-stale-index-v1"},
                         "tag": {"type": "string",
                                 "description": "COLOSSEUM_APPDATA_TAG override; defaults to the generated session id"},
                         "drive": {"type": "boolean",
                                   "description": "sets COLOSSEUM_LANISTA_DRIVE=1 so act() can click/type/scroll"}}}},
    {"name": "session_stop",
     "description": "Stop the active session: graceful (WM_CLOSE) then kill, logs preserved.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "act",
     "description": "Drive the active session: click / keypress / text-input / scroll.",
     "inputSchema": {"type": "object",
                     "properties": {
                         "action": {"type": "string", "enum": ["click", "keypress", "text-input", "scroll"]},
                         "target": {"type": "string", "description": "objectName (click/text-input/scroll)"},
                         "key": {"type": "string", "description": "keypress only"},
                         "text": {"type": "string", "description": "text-input only"},
                         "dy": {"type": "integer", "description": "scroll only, default -120"},
                         "timeoutMs": {"type": "integer"}},
                     "required": ["action"]}},
    {"name": "get",
     "description": "Read named QML properties off an item in the active session (qml-get).",
     "inputSchema": {"type": "object",
                     "properties": {
                         "target": {"type": "string"},
                         "props": {"type": "array", "items": {"type": "string"}},
                         "timeoutMs": {"type": "integer"}},
                     "required": ["target", "props"]}},
    {"name": "snapshot",
     "description": "List every interactive element on screen right now, in the active session.",
     "inputSchema": {"type": "object", "properties": {"timeoutMs": {"type": "integer"}}}},
    {"name": "wait_for",
     "description": "Poll one property in the active session until it strictly equals a value.",
     "inputSchema": {"type": "object",
                     "properties": {
                         "target": {"type": "string"},
                         "prop": {"type": "string"},
                         "value": {},
                         "timeoutMs": {"type": "integer", "description": "server-side poll deadline, default 3000"}},
                     "required": ["target", "prop", "value"]}},
    {"name": "grab",
     "description": "Photograph a UI element or the whole window of the active session.",
     "inputSchema": {"type": "object",
                     "properties": {"target": {"type": "string"}, "timeoutMs": {"type": "integer"}},
                     "required": ["target"]}},
    {"name": "warnings",
     "description": "Run W0's warning gate against the (active or last-stopped) session's "
                    "colosseum.log + stderr.log; returns WARNING_GATE_OK or the offending lines.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "lanista_call",
     "description": "Send any lanista command to the running Colosseum "
                    "(ping, get-state, qml-get, ui-query, ui-snapshot, "
                    "invoke-read, events-tail, log-mark; ui-* drive commands "
                    "need COLOSSEUM_LANISTA_DRIVE=1 in the app). Legacy tool, "
                    "unchanged target: whatever COLOSSEUM_LANISTA_PIPE resolves to.",
     "inputSchema": {"type": "object",
                     "properties": {"cmd": {"type": "string"},
                                    "payload": {"type": "object"}},
                     "required": ["cmd"]}},
    {"name": "lanista_grab",
     "description": "Photograph a UI element (by objectName) or the whole "
                    "window of the running Colosseum; returns the image. Legacy tool.",
     "inputSchema": {"type": "object",
                     "properties": {"target": {"type": "string"}},
                     "required": ["target"]}},
    {"name": "lanista_snapshot",
     "description": "List every interactive element on screen right now, "
                    "with handles, positions and states. Legacy tool.",
     "inputSchema": {"type": "object", "properties": {}}},
    # F1-Bridge (2026-08-13) — strictly additive: appended after the 11 v0 tools above,
    # which keep their exact names/order/schemas unchanged (legacy_tools_unchanged).
    {"name": "vault_forensics",
     "description": "Read a bounded, typed projection of the live Vault (F1-Core, schema "
                    "colosseum.vault.forensics.v1) through the active session's bridge. "
                    "scope=summary needs no key; root/node/identity need key (a root path, "
                    "browse path, or identity/group key). limit clamps every row list to "
                    "1-100 (default 20). Requires an active session (session_start).",
     "inputSchema": {"type": "object",
                     "properties": {
                         "scope": {"type": "string",
                                   "enum": ["summary", "root", "node", "identity"]},
                         "key": {"type": "string",
                                 "description": "root path (scope=root) or browse/identity key "
                                                "(scope=node/identity); unused for summary"},
                         "limit": {"type": "integer",
                                   "description": "row-list bound, 1-100, default 20"},
                         "timeoutMs": {"type": "integer",
                                       "description": "bridge marshalling deadline, default 8000"}},
                     "required": ["scope"]}},
]

TOOL_IMPLS = {
    "session_start": tool_session_start,
    "session_stop": tool_session_stop,
    "act": tool_act,
    "get": tool_get,
    "snapshot": tool_snapshot,
    "wait_for": tool_wait_for,
    "grab": tool_grab,
    "warnings": tool_warnings,
    "lanista_call": tool_lanista_call,
    "lanista_snapshot": tool_lanista_snapshot,
    "lanista_grab": tool_lanista_grab,
    "vault_forensics": tool_vault_forensics,
}


class RpcError(Exception):
    """A JSON-RPC error with a specific code. Raised by handle(), turned into a
    proper error reply by main() — so a bad method or tool name is a clean coded
    reply, never an uncaught raise that could stall the agent."""
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def handle(method, params, rid):
    if method == "initialize":
        return {"protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "lanista", "version": "2.0"}}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments", {}) or {}
        impl = TOOL_IMPLS.get(name)
        if impl is None:
            raise RpcError(-32602, "unknown tool: " + str(name))
        return impl(args)
    raise RpcError(-32601, "method not found: " + str(method))


def _emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        # ONE try/except around the WHOLE per-line body — parse INCLUDED. This is
        # the agent's only channel to the app, so a single malformed line (bad
        # JSON, or valid JSON that isn't an object) must produce a coded reply and
        # move on, never throw out of the loop and kill the adapter.
        rid = None
        try:
            try:
                msg = json.loads(line)
            except ValueError:                 # not JSON at all
                _emit({"jsonrpc": "2.0", "id": None,
                       "error": {"code": -32700, "message": "Parse error"}})
                continue
            if not isinstance(msg, dict):       # e.g. 5 or [1] — no .get / no id
                _emit({"jsonrpc": "2.0", "id": None,
                       "error": {"code": -32600, "message": "Invalid Request"}})
                continue
            rid = msg.get("id")
            result = handle(msg.get("method", ""), msg.get("params", {}), rid)
            if rid is None:
                continue                       # notification: no reply
            out = {"jsonrpc": "2.0", "id": rid, "result": result}
        except RpcError as exc:                # a method/tool we named a code for
            if rid is None:
                continue
            out = {"jsonrpc": "2.0", "id": rid,
                   "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:               # noqa: BLE001 — any other fault
            if rid is None:
                continue
            out = {"jsonrpc": "2.0", "id": rid,
                   "error": {"code": -32603, "message": str(exc)}}
        _emit(out)


if __name__ == "__main__":
    main()
