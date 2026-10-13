"""Verify the startup policy in a built Colosseum executable, using isolated data.

Set PATH/plugin paths to the intended runtime before invoking. No installed-app
claim: the positive case uses the app's explicit QML fixture entry point.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

import psutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    executable = args.exe.resolve(strict=True)
    with executable.open("rb") as stream:
        executable_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    scene = out / "Startup.qml"
    scene.write_text('''import QtQuick
import QtQuick.Window
import QtWebEngine
Window {
    width: 640; height: 360; visible: false
    WebEngineView {
        id: view
        anchors.fill: parent
        profile: WebEngineProfile { offTheRecord: true }
        Component.onCompleted: loadHtml("<html><body>startup</body></html>")
        onLoadingChanged: function(info) {
            if (info.status !== WebEngineView.LoadSucceededStatus) return
            runJavaScript("document.body.textContent", function(value) {
                if (value === "startup") {
                    console.log("STARTUP_ENGINE_VERIFIED")
                    Qt.quit()
                }
            })
        }
    }
    Timer { interval: 15000; running: true; onTriggered: Qt.exit(9) }
}
''')
    base = dict(os.environ)
    for name in ("QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_DISABLE_SANDBOX",
                 "QTWEBENGINE_REMOTE_DEBUGGING", "FERIA_SKIP_ORDERLY_SHUTDOWN"):
        base.pop(name, None)
    base.update(QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1",
                COLOSSEUM_APPDATA_TAG="startup-" + uuid.uuid4().hex,
                COLOSSEUM_DEV="")
    # Empty presence enables some developer options; remove instead of assigning.
    base.pop("COLOSSEUM_DEV", None)
    cases = [
        ("sandbox-zero", {"QTWEBENGINE_DISABLE_SANDBOX": "0"}, [], 78),
        ("certificate-env", {"QTWEBENGINE_CHROMIUM_FLAGS": "--ignore-certificate-errors"}, [], 78),
        ("remote-debug", {"QTWEBENGINE_REMOTE_DEBUGGING": "0.0.0.0:9222"}, [], 78),
        ("unsafe-argv", {}, ["--webEngineArgs", "--disable-web-security"], 78),
        ("safe-default", {}, [], 0),
        ("safe-graphics", {"QTWEBENGINE_CHROMIUM_FLAGS": "--disable-gpu"}, [], 0),
    ]
    receipts = []
    for name, extra, flags, expected in cases:
        environment = base | extra
        command = [str(executable), str(scene), *flags]
        receipt = {"case": name, "command": command, "expectedExit": expected,
                   "exeSha256": executable_hash, "appDataTag": base["COLOSSEUM_APPDATA_TAG"]}
        with (out / (name + ".stdout")).open("wb") as stdout, (out / (name + ".stderr")).open("wb") as stderr:
            process = subprocess.Popen(command, cwd=out, env=environment, stdout=stdout, stderr=stderr)
            receipt["pid"] = process.pid
            started = time.monotonic()
            children = {}
            while process.poll() is None and time.monotonic() - started < 60:
                try:
                    for child in psutil.Process(process.pid).children(recursive=True):
                        children[child.pid] = {"pid": child.pid, "created": child.create_time(), "exe": child.exe()}
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
                time.sleep(0.02)
            receipt.update(nativeExit=process.poll(), elapsedSeconds=time.monotonic() - started,
                           children=list(children.values()))
        log = (out / (name + ".stderr")).read_text(errors="replace")
        receipt["guardDiagnostic"] = "cannot start WebEngine safely" in log
        receipt["engineVerified"] = "STARTUP_ENGINE_VERIFIED" in log
        receipt["passed"] = (receipt["nativeExit"] == expected
                             and receipt["guardDiagnostic"] == (expected == 78)
                             and receipt["engineVerified"] == (expected == 0)
                             and (expected != 78 or not children))
        receipts.append(receipt)
        (out / "result.json").write_text(json.dumps(receipts, indent=2))
        print(name, receipt["nativeExit"], "PASS" if receipt["passed"] else "FAIL", flush=True)
        if not receipt["passed"]:
            raise RuntimeError("Startup verification stopped at first failure; inspect recorded PID/logs")


if __name__ == "__main__":
    main()
