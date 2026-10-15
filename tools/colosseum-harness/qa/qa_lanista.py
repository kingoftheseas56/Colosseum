#!/usr/bin/env python3
"""qa_lanista — one-shot CLI over the Lanista MCP adapter, for QA testers
that reach the machine through a shell (ChatGPT via Brotherhood Desktop
Commander) instead of an MCP stdio connection.

Each invocation is a separate process. The live session is recovered from the
adapter's own active-session pointer, so start / act / shot / stop can be
separate commands. All isolation and deadline guarantees stay in server.py;
this file only rehydrates the session and prints JSON.

Usage (from the Colosseum repo root):
  python tools/colosseum-harness/qa/qa_lanista.py start [--seed NAME]
  python tools/colosseum-harness/qa/qa_lanista.py snapshot [--max-chars N]
  python tools/colosseum-harness/qa/qa_lanista.py click TARGET
  python tools/colosseum-harness/qa/qa_lanista.py key KEY
  python tools/colosseum-harness/qa/qa_lanista.py type TARGET TEXT
  python tools/colosseum-harness/qa/qa_lanista.py scroll TARGET [DY]
  python tools/colosseum-harness/qa/qa_lanista.py get TARGET PROP [PROP...]
  python tools/colosseum-harness/qa/qa_lanista.py wait TARGET PROP VALUE [TIMEOUT_MS]
  python tools/colosseum-harness/qa/qa_lanista.py shot [TARGET] [--name LABEL]
  python tools/colosseum-harness/qa/qa_lanista.py warnings
  python tools/colosseum-harness/qa/qa_lanista.py stop
"""

import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "native", "tools", "lanista-mcp"))
import server  # noqa: E402  (the Lanista MCP adapter; stdlib only)

SHOT_MAX_WIDTH = 1280  # keeps each screenshot small enough for a chat context


def emit(obj, code=0):
    print(json.dumps(obj, indent=1))
    sys.exit(code)


def result_json(res):
    """Unwrap an adapter tool result into plain JSON for printing."""
    texts = [c.get("text", "") for c in res.get("content", []) if c.get("type") == "text"]
    body = texts[0] if texts else ""
    try:
        body = json.loads(body)
    except (ValueError, TypeError):
        pass
    return {"ok": not res.get("isError", False), "result": body}


def rehydrate():
    """Point the adapter at the live session recorded on disk, if any."""
    pointer = server._load_active_pointer()
    if not pointer or not server._pid_alive(pointer.get("pid")):
        if pointer:
            server._clear_active_pointer()
        emit({"ok": False, "error": "no live QA session — run `start` first"}, 2)
    manifest_path = os.path.join(pointer["dir"], "session.json")
    manifest = server._read_json(manifest_path) or {}
    server.SESSION = {
        "active": True, "id": pointer["sessionId"], "pipe": pointer["pipe"],
        "tag": pointer.get("tag"), "pid": pointer["pid"], "proc": None,
        "dir": pointer["dir"], "manifest_path": manifest_path,
        "stdoutPath": manifest.get("stdoutPath"), "stderrPath": manifest.get("stderrPath"),
    }
    server.LAST_SESSION = server.SESSION
    return pointer


def to_small_jpeg(src_png, dst_jpg):
    """Downscale a PNG to a <=1280px JPEG with Windows' built-in imaging."""
    ps = (
        "Add-Type -AssemblyName System.Drawing;"
        "$s=[System.Drawing.Image]::FromFile($env:QA_SRC);"
        "$w=[Math]::Min($s.Width,{m});$h=[int]($s.Height*$w/$s.Width);"
        "$b=New-Object System.Drawing.Bitmap $w,$h;"
        "$g=[System.Drawing.Graphics]::FromImage($b);"
        "$g.InterpolationMode='HighQualityBicubic';$g.DrawImage($s,0,0,$w,$h);"
        "$c=[System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders()|?{{$_.MimeType -eq 'image/jpeg'}};"
        "$p=New-Object System.Drawing.Imaging.EncoderParameters 1;"
        "$p.Param[0]=New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality,[long]72);"
        "$b.Save($env:QA_DST,$c,$p);$g.Dispose();$b.Dispose();$s.Dispose()"
    ).format(m=SHOT_MAX_WIDTH)
    env = dict(os.environ, QA_SRC=src_png, QA_DST=dst_jpg)
    subprocess.run(["powershell.exe", "-NoProfile", "-Command", ps], env=env,
                   check=True, capture_output=True, timeout=30)


def cmd_start(argv):
    seed = argv[argv.index("--seed") + 1] if "--seed" in argv else None
    args = {"drive": True}
    if seed:
        args["seedName"] = seed
    res = server.tool_session_start(args)
    emit(result_json(res), 0 if not res.get("isError") else 1)


def cmd_stop(_argv):
    pointer = rehydrate()
    pid = pointer["pid"]
    try:
        server._post_wm_close(pid)
    except Exception:
        pass
    deadline = time.monotonic() + 8.0
    while server._pid_alive(pid) and time.monotonic() < deadline:
        time.sleep(0.25)
    reason = "graceful"
    if server._pid_alive(pid):
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
        reason = "killed after graceful timeout"
    manifest = server._read_json(server.SESSION["manifest_path"]) or {}
    manifest.update({"state": "stopped", "killReason": reason})
    server._write_json(server.SESSION["manifest_path"], manifest)
    server._clear_active_pointer()
    emit({"ok": True, "result": {"sessionId": pointer["sessionId"], "stopped": reason}})


def cmd_snapshot(argv):
    rehydrate()
    max_chars = int(argv[argv.index("--max-chars") + 1]) if "--max-chars" in argv else 60000
    out = result_json(server.tool_snapshot({}))
    text = json.dumps(out["result"])
    if len(text) > max_chars:
        out["result"] = text[:max_chars]
        out["truncated"] = True
    emit(out, 0 if out["ok"] else 1)


def cmd_act(action, extra):
    rehydrate()
    out = result_json(server.tool_act(dict(action=action, **extra)))
    emit(out, 0 if out["ok"] else 1)


def cmd_shot(argv):
    pointer = rehydrate()
    target = argv[0] if argv and not argv[0].startswith("--") else "window"
    label = argv[argv.index("--name") + 1] if "--name" in argv else target
    res = server.tool_grab({"target": target})
    if res.get("isError"):
        emit(result_json(res), 1)
    meta = {}
    for c in res.get("content", []):
        if c.get("type") == "text":
            try:
                meta = json.loads(c["text"])
            except ValueError:
                pass
    shots = os.path.join(pointer["dir"], "shots")
    os.makedirs(shots, exist_ok=True)
    stamp = time.strftime("%H%M%S")
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "-" for ch in label)[:40]
    png = os.path.join(shots, "{}-{}.png".format(stamp, safe))
    import base64
    img = next(c for c in res["content"] if c.get("type") == "image")
    with open(png, "wb") as f:
        f.write(base64.b64decode(img["data"]))
    jpg = png[:-4] + ".jpg"
    to_small_jpeg(png, jpg)
    emit({"ok": True, "result": {"image": jpg, "fullPng": png, "target": target,
                                 "state": {k: meta.get(k) for k in ("route", "page", "width", "height") if k in meta}}})


def main():
    argv = sys.argv[1:]
    if not argv:
        emit({"ok": False, "error": __doc__}, 2)
    cmd, rest = argv[0], argv[1:]
    if cmd == "start":
        cmd_start(rest)
    elif cmd == "stop":
        cmd_stop(rest)
    elif cmd == "snapshot":
        cmd_snapshot(rest)
    elif cmd == "click" and rest:
        cmd_act("click", {"target": rest[0]})
    elif cmd == "key" and rest:
        cmd_act("keypress", {"key": rest[0]})
    elif cmd == "type" and len(rest) >= 2:
        cmd_act("text-input", {"target": rest[0], "text": rest[1]})
    elif cmd == "scroll" and rest:
        cmd_act("scroll", {"target": rest[0], "dy": int(rest[1]) if len(rest) > 1 else -120})
    elif cmd == "get" and len(rest) >= 2:
        rehydrate()
        out = result_json(server.tool_get({"target": rest[0], "props": rest[1:]}))
        emit(out, 0 if out["ok"] else 1)
    elif cmd == "wait" and len(rest) >= 3:
        rehydrate()
        args = {"target": rest[0], "prop": rest[1], "value": rest[2]}
        if len(rest) > 3:
            args["timeoutMs"] = int(rest[3])
        out = result_json(server.tool_wait_for(args))
        emit(out, 0 if out["ok"] else 1)
    elif cmd == "shot":
        cmd_shot(rest)
    elif cmd == "warnings":
        rehydrate()
        out = result_json(server.tool_warnings({}))
        emit(out, 0 if out["ok"] else 1)
    else:
        emit({"ok": False, "error": "bad command; see usage", "usage": __doc__}, 2)


if __name__ == "__main__":
    main()
