from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
ORACLE = Path(r"C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js")
PROFILES = json.loads((ROOT / "tools/server_lab/scenarios/reference_profiles.json").read_text(encoding="utf-8"))["profiles"]


def probe(path: str) -> dict:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:11470{path}", timeout=4) as response:
            body = response.read().decode("utf-8", errors="replace")
            return {"status": response.status, "body": body[:4096]}
    except Exception as exc:
        return {"error": type(exc).__name__, "message": str(exc)}


def run(profile_id: str, profile: dict) -> dict:
    run_root = Path(tempfile.mkdtemp(prefix=f"p02s-a-{profile_id}-", dir=OUT))
    env = os.environ.copy()
    env.update({"APP_PATH": str(run_root / "app"), "SETTINGS_PATH": str(run_root / "settings"), "FFMPEG_BIN": "/usr/bin/ffmpeg", "FFPROBE_BIN": "/usr/bin/ffprobe", "NO_HTTPS_SERVER": "1", "NO_NETWORK_INTERFACES": "1"})
    env.update({key: value for key, value in profile.get("env", {}).items() if not value.startswith("<")})
    started = time.time()
    process = subprocess.Popen(["node", str(ORACLE)], cwd=run_root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    output = ""
    while time.time() - started < 12:
        if process.stdout is not None:
            chunk = process.stdout.readline()
            if chunk:
                output += chunk
                if "EngineFS server started" in output:
                    break
        if process.poll() is not None:
            break
    time.sleep(0.3)
    probes = {path: probe(path) for path in ("/heartbeat", "/settings", "/network-info")}
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    if process.stdout is not None:
        output += process.stdout.read()
    paths_removed_before_cleanup = not (run_root / "app").exists() and not (run_root / "settings").exists()
    shutil.rmtree(run_root, ignore_errors=True)
    return {"profile_id": profile_id, "env": {key: env[key] for key in ("TV_ENV", "UNITY_ENV", "IOS_APP", "NO_HTTPS_SERVER", "NO_NETWORK_INTERFACES") if key in env}, "probes": probes, "started": "EngineFS server started" in output, "exit_code": process.returncode, "paths_removed_after_cleanup": not run_root.exists(), "stdout_tail": output[-6000:]}


results = {"schema": "colosseum-server1-p02s-a-profile-runs/v1", "oracle": str(ORACLE), "oracle_sha256": "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f", "profiles": [run(profile_id, profile) for profile_id, profile in PROFILES.items()]}
(OUT / "profile-runs.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
print(json.dumps(results, indent=2))
