"""Check remote/native separation through the real built application's context.

Set PATH and Qt plugin/QML/helper paths for the intended compatible runtime.
Uses a disposable app-data tag and profile. No installed-shell or sandbox claim.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid

import psutil


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--expose-control", action="store_true",
                        help="Negative control: attach only a harmless fixture QtObject, expect detection")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    exe = args.exe.resolve(strict=True)
    for process in psutil.process_iter(["name", "pid"]):
        if process.info["name"].lower() in ("qtwebengineprocess.exe", "feria_foundation_runtime.exe"):
            raise RuntimeError("Existing browser test/helper must finish before this run")
    secret = out / "native-only.txt"
    secret.write_text("native-only-" + uuid.uuid4().hex)
    names = ["FeriaAccount", "FeriaBrowserPolicy", "Sessions", "LocalLaunch",
             "Downloads", "LocalDownloads", "Reader2Bridge", "porticoWebBridge", "Updates",
             "Extensions", "Qt", "QWebChannel"]
    config = {"profile": str(out / "profile"), "exposeControl": args.expose_control}
    hits = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            hits.append({"port": self.server.server_port, "path": self.path})
            script = """
const names = NAMES;
const exposed = names.filter(name => typeof window[name] !== 'undefined');
// Clipboard is also a standard web constructor; only the QML helper has copy().
if (window.Clipboard && typeof window.Clipboard.copy === 'function') exposed.push('Clipboard.copy');
if (window.qt && window.qt.webChannelTransport) exposed.push('qt.webChannelTransport');
if (window.chrome && window.chrome.webview) exposed.push('chrome.webview');
window.boundaryReport = {ready:false,path:location.pathname,exposed,frames:[]};
window.addEventListener('message', event => {
    const frame = Array.from(document.querySelectorAll('iframe')).find(f => f.contentWindow === event.source);
    if (frame && event.data && event.data.boundary === true)
        boundaryReport.frames.push({label:frame.name,report:event.data.report});
});
Promise.all([fetch(SECRET).then(r=>r.text()).then(()=> 'read').catch(()=> 'blocked'),
    fetch('qrc:///qtwebchannel/qwebchannel.js').then(r=>r.text()).then(()=> 'read').catch(()=> 'blocked')])
    .then(results => {
        Object.assign(boundaryReport,{fileFetch:results[0],resourceFetch:results[1],ready:true});
        if (parent !== window) parent.postMessage({boundary:true,report:boundaryReport},'*');
    });
// These are untrusted page messages, never an application command channel.
window.postMessage({method:'openUrlExternally',url:SECRET},'*');
console.log('FeriaAccount.reset');
""".replace("NAMES", json.dumps(names)).replace("SECRET", json.dumps(secret.as_uri()))
            frames = ""
            if self.path == "/top":
                frames = ('<iframe name="same-frame" src="/frame"></iframe>'
                          '<iframe name="cross-frame" src="' + config["secondary"] + '/frame"></iframe>')
            body = ("<!doctype html><title>Boundary fixture</title><script>" + script
                    + "</script><body>Hostile remote document" + frames + "</body>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    servers = [ThreadingHTTPServer(("127.0.0.1", 0), Handler) for _ in range(2)]
    for name, server in zip(("primary", "secondary"), servers):
        config[name] = "http://127.0.0.1:" + str(server.server_port)
        threading.Thread(target=server.serve_forever, daemon=True).start()
    template = Path(__file__).with_name("BoundaryScene.qml.in")
    scene = out / "BoundaryScene.qml"
    scene.write_text(template.read_text().replace("@FERIA_IMPORT@", json.dumps((repo / "qml/feria").as_uri())))
    (out / "BoundaryConfig.js").write_text("var values = " + json.dumps(config))
    env = dict(os.environ)
    for name in ("QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_DISABLE_SANDBOX",
                 "QTWEBENGINE_REMOTE_DEBUGGING", "FERIA_SKIP_ORDERLY_SHUTDOWN", "COLOSSEUM_DEV"):
        env.pop(name, None)
    env.update(COLOSSEUM_APPDATA_TAG="boundary-" + uuid.uuid4().hex,
               QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1")
    sources = [repo / "qml/feria/FeriaQtWebEngine.qml", repo / "qml/feria/FeriaBrowserRequests.qml",
               template, Path(__file__).resolve()]
    before = {str(p.relative_to(repo)): digest(p) for p in sources}
    receipt = {"exeSha256": digest(exe), "command": [str(exe), str(scene)], "sourceBefore": before,
               "appDataTag": env["COLOSSEUM_APPDATA_TAG"], "profile": config["profile"],
               "negativeControl": args.expose_control, "expectedExit": 7 if args.expose_control else 0}
    (out / "launch.json").write_text(json.dumps(receipt, indent=2))
    try:
        with (out / "stdout.log").open("wb") as stdout, (out / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(receipt["command"], cwd=out, env=env, stdout=stdout, stderr=stderr)
            receipt["pid"] = process.pid
            children = {}
            modules = set()
            started = time.monotonic()
            while process.poll() is None and time.monotonic() - started < 60:
                try:
                    root = psutil.Process(process.pid)
                    for child in root.children(recursive=True):
                        children[child.pid] = {"pid": child.pid, "created": child.create_time(), "exe": child.exe()}
                    for mapping in root.memory_maps():
                        if "qt6webengine" in mapping.path.lower() or "qtwebenginequickplugin" in mapping.path.lower():
                            modules.add(mapping.path)
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
                time.sleep(0.05)
            receipt.update(nativeExit=process.poll(), elapsedSeconds=time.monotonic() - started,
                           modules={path: digest(Path(path)) for path in sorted(modules)},
                           observedChildren=list(children.values()), serverHits=hits)
        remaining = []
        for child in children.values():
            try:
                current = psutil.Process(child["pid"])
                if current.create_time() == child["created"]:
                    current.wait(timeout=5)
            except psutil.TimeoutExpired:
                remaining.append(child)
            except psutil.NoSuchProcess:
                pass
        log = (out / "stderr.log").read_text(errors="replace")
        scene_passed = ("FERIA_BOUNDARY_FAIL top: no privileged globals or transport" in log
                  and '"qt.webChannelTransport"' in log) if args.expose_control else (
                  "FERIA_BOUNDARY_PASS 20" in log and "FERIA_BOUNDARY_FAIL" not in log)
        receipt.update(sourceStable=all(digest(repo / path) == sha for path, sha in before.items()),
                       remainingOwnedChildren=remaining, scenePassed=scene_passed)
        receipt["passed"] = (receipt["nativeExit"] == receipt["expectedExit"] and scene_passed
                             and receipt["sourceStable"] and not remaining
                             and len(modules) >= 3 and bool(children))
        (out / "result.json").write_text(json.dumps(receipt, indent=2))
        if not receipt["passed"]:
            raise RuntimeError("Boundary check incomplete or failed; preserve receipts and inspect native PID/logs")
        print("Expected exposed-channel control detected; native exit 7" if args.expose_control
              else "Production-context boundary checks passed; native exit 0")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
