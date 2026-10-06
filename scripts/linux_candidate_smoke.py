#!/usr/bin/env python3
"""Verify the downloaded candidate outside the build runner, including real WebEngine."""
from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import uuid

from linux_candidate_package import HOST_LIBS, is_elf, parse_ldd, verify_payload, verify_qml


def call(pipe: str, command: str) -> dict:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(2)
        client.connect(str(Path(tempfile.gettempdir()) / pipe))
        client.sendall((json.dumps({"cmd": command, "seq": 1}) + "\n").encode())
        data = b""
        while b"\n" not in data:
            block = client.recv(65536)
            if not block:
                raise RuntimeError("Lanista disconnected without a reply")
            data += block
        return json.loads(data.split(b"\n", 1)[0])


def smoke(root: Path, revision: str, evidence: Path) -> None:
    evidence.mkdir(parents=True, exist_ok=False)
    verify_payload(root)
    verify_qml(root / "qml", root / "usr/bin/qml-build.manifest")
    info = json.loads((root / "BUILD-INFO.json").read_text())
    if info["sourceRevision"] != revision or info["qtVersion"] != "6.11.1":
        raise RuntimeError("Candidate source/Qt identity mismatch")
    subprocess.run(["sha256sum", "--check", "--strict", "SHA256SUMS"], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="colosseum-candidate-") as scratch:
        work = Path(scratch)
        (work / "home").mkdir()
        env = {key: value for key, value in os.environ.items()
               if not (key.startswith("QT_") or key.startswith("QML") or key in
                       ("LD_LIBRARY_PATH", "CMAKE_PREFIX_PATH", "QT_ROOT_DIR"))}
        env.update(HOME=str(work / "home"), XDG_DATA_HOME=str(work / "home/data"),
                   XDG_CACHE_HOME=str(work / "home/cache"), XDG_CONFIG_HOME=str(work / "home/config"),
                   LD_LIBRARY_PATH=str(root / "usr/lib"), QT_QPA_PLATFORM="xcb",
                   LIBGL_ALWAYS_SOFTWARE="1", QT_FORCE_STDERR_LOGGING="1",
                   COLOSSEUM_APPDATA_TAG="candidate-smoke-" + uuid.uuid4().hex)
        # Fail if a developer prefix or runner-installed Qt rescues the package.
        checked = set()
        for path in root.rglob("*"):
            if not is_elf(path) or path.resolve() in checked:
                continue
            checked.add(path.resolve())
            result = subprocess.run(["ldd", str(path)], env=env, text=True, capture_output=True, check=True)
            for dependency in parse_ldd(result.stdout):
                if not HOST_LIBS.match(dependency.name) and not dependency.resolve().is_relative_to(root):
                    raise RuntimeError(f"Dependency escaped candidate: {path.name}: {dependency}")
        # Launch the normal packaged Main.qml without an override: this exercises
        # its production startup manifest and assembled native registrations.
        pipe = "ColosseumLanista-candidate-" + uuid.uuid4().hex
        env["COLOSSEUM_LANISTA_PIPE"] = pipe
        with (evidence / "app.log").open("wb") as log:
            app = subprocess.Popen([str(root / "AppRun")], cwd=work, env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 90
                while True:
                    if app.poll() is not None:
                        raise RuntimeError(f"Packaged app exited before ready: {app.returncode}")
                    try:
                        pong = call(pipe, "ping")
                        if pong.get("pid") != app.pid:
                            raise RuntimeError("Lanista answered from a different process")
                        state = call(pipe, "get-state")
                        if state.get("windows") and state["windows"][0].get("visible"):
                            break
                    except (OSError, ValueError):
                        pass
                    if time.monotonic() > deadline:
                        raise RuntimeError("Packaged app never exposed a visible window")
                    time.sleep(0.2)
                marker = "Colosseum-dltest-" + env["COLOSSEUM_APPDATA_TAG"]
                if not all(marker in state.get(key, "") for key in ("appDataRoot", "cacheRoot")):
                    raise RuntimeError("Candidate app data isolation failed")
                (evidence / "app-state.json").write_text(json.dumps(state, indent=2) + "\n")
            finally:
                app.terminate()
                try:
                    app.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
        # The SAME packaged colosseum binary loads the REAL Feria wrapper. The
        # page is a local deterministic fixture; no live-provider claim is made.
        (work / "index.html").write_text(
            "<!doctype html><title>Candidate WebEngine</title>"
            "<body style='background:#152839;color:#f2c94c;font:48px sans-serif'>"
            "<h1 id='proof'>QtWebEngine package works</h1></body>")
        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=work))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        fixture = work / "Probe.qml"
        capture = evidence / "webengine.png"
        fixture.write_text('''import QtQuick
import QtQuick.Window
import "''' + (root / "qml/feria").as_uri() + '''" as Feria
Window {
    id: win; visible: true; width: 960; height: 640
    property bool captured: false
    Feria.FeriaQtWebEngine {
        id: view; anchors.fill: parent
        sourceUrl: "http://127.0.0.1:''' + str(server.server_port) + '''/index.html"
        profilePath: ''' + json.dumps(str(work / "browser-profile")) + '''
        observationEnabled: false
        onCompleted: function(location, success) {
            if (!success) { console.error("CANDIDATE_WEBENGINE_LOAD_FAILED"); Qt.exit(1); return }
            runJavaScript("document.getElementById('proof').textContent", function(value) {
                if (value !== "QtWebEngine package works") { Qt.exit(2); return }
                captureTimer.start()
            })
        }
    }
    Timer {
        id: captureTimer; interval: 250
        onTriggered: win.contentItem.grabToImage(function(result) {
            if (!result.saveToFile(''' + json.dumps(str(capture)) + ''')) { Qt.exit(3); return }
            console.info("CANDIDATE_WEBENGINE_PASS"); Qt.quit()
        })
    }
    Timer { interval: 45000; running: true; onTriggered: Qt.exit(4) }
}
''')
        try:
            with (evidence / "webengine.log").open("wb") as log:
                subprocess.run([str(root / "AppRun"), str(fixture)], cwd=work, env=env,
                               stdout=log, stderr=log, timeout=60, check=True)
            if not capture.is_file() or "CANDIDATE_WEBENGINE_PASS" not in (evidence / "webengine.log").read_text():
                raise RuntimeError("WebEngine did not produce runtime evidence")
        finally:
            server.shutdown()
            server.server_close()
        (evidence / "qualification.json").write_text(json.dumps({
            "sourceRevision": revision, "qtVersion": info["qtVersion"],
            "normalAppLaunch": "PASS", "qmlBuildMatch": "PASS", "bundledDependencies": "PASS",
            "feriaQtWebEngineLocalPageJavaScriptAndCapture": "PASS",
            "unverified": info["unverified"],
        }, indent=2) + "\n")
        print("LINUX_CANDIDATE_RUNTIME_PASS " + revision)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    args = parser.parse_args()
    smoke(args.root.resolve(), args.revision, args.evidence.resolve())
