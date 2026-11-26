"""Replay the retained Windows fixture and capture the NSIS handoff and resulting identity."""

import http.server
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import threading
import time
import winreg

from windows_update_rehearsal import App, digest, openssl, require, run


ORIGIN = "https://127.0.0.1:18443"
PORT = 18443
VERSION_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum"
APP_EXE = Path("native/build-msvc/colosseum.exe")
TARGET = "1.5.1"
PACKAGE = "Colosseum-1.5.1-setup.exe"
EXPECTED = {
    "1.5.0": (249545450, "512144d1cbd5cb5a202bda7b286b731005e17d6306bab1890c3e3d8ef52b51b2",
              "ebaa1bdebaa0e3840d3e09f55fede33d5bfbcb24a2788cdd532c78563a1656e2"),
    "1.5.1": (249540801, "ce94f026a1c97e1447b1861a31d960ebf3ca70862ff244ceaa335afbdfe35139",
              "91d5f9b9ace4aabc332022ba7197eedf6e74b3e72dfe93c5b7b1c4ba6dbf6519"),
}


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def reg_identity():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, VERSION_KEY) as key:
            return {name: winreg.QueryValueEx(key, name)[0] for name in ("DisplayVersion", "InstallLocation")}
    except FileNotFoundError:
        return None


def validate(work, evidence):
    base = work / "received-evidence"
    identities = json.loads((base / "package-identities.json").read_bytes())
    require(identities["runtime_source"] == "d73111cddfef5728aa6b714fdb52fdf030b96ec6", "Fixture source mismatch")
    require(identities["shipping_installer_sha256"] ==
            "2f7d879dcae87255befaa83f76baa689de20c8e624d6bb23f1ed07e5fe1789aa", "Shipping reference mismatch")
    for version, (size, sha, exe_sha) in EXPECTED.items():
        installer = work / "fixtures" / f"Colosseum-{version}-setup.exe"
        require(installer.stat().st_size == size and digest(installer) == sha, f"Retained {version} package mismatch")
        require(identities["versions"][version] == {"installer_bytes": size, "installer_sha256": sha, "exe_sha256": exe_sha},
                f"Retained {version} identity mismatch")
    manifest = base / "colosseum-update-v1.json"
    signature = base / "colosseum-update-v1.json.sig"
    public_key = base / "manifest-public.der"
    require(signature.stat().st_size == 64, "Invalid retained signature length")
    openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER", "-inkey", public_key,
            "-in", manifest, "-sigfile", signature)
    size, sha, _ = EXPECTED[TARGET]
    require(json.loads(manifest.read_bytes())["installer"] == {"asset": PACKAGE, "size": size, "sha256": sha},
            "Valid retained manifest identifies different bytes")
    for name in ("package-identities.json", manifest.name, signature.name, public_key.name, "fixture-source.diff"):
        shutil.copy2(base / name, evidence / name)
    print("Verified exact retained installer pair and valid Ed25519 manifest", flush=True)


def make_tls(work):
    keys = work / "keys"
    keys.mkdir()
    ca, leaf = keys / "ca.pem", keys / "server.pem"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=Colosseum diagnostic loopback CA", "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", keys / "ca-key.pem", "-out", ca)
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=127.0.0.1",
            "-keyout", keys / "server-key.pem", "-out", keys / "server.csr")
    ext = keys / "server-ext.cnf"
    ext.write_text("subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n"
                   "basicConstraints=critical,CA:FALSE\n", encoding="ascii")
    openssl("x509", "-req", "-in", keys / "server.csr", "-CA", ca, "-CAkey", keys / "ca-key.pem",
            "-CAcreateserial", "-days", "2", "-extfile", ext, "-out", leaf)
    openssl("verify", "-CAfile", ca, "-verify_ip", "127.0.0.1", leaf)
    os.environ["COLOSSEUM_WINDOWS_REHEARSAL_CA"] = str(ca)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(leaf, keys / "server-key.pem")
    return context


class Feed:
    def __init__(self, work, evidence):
        self.root = work / "https"
        self.downloads = self.root / "updates/downloads/v1.5.1"
        self.downloads.mkdir(parents=True)
        self.original_signature = (evidence / "colosseum-update-v1.json.sig").read_bytes()
        self.manifest = Path(shutil.copy2(evidence / "colosseum-update-v1.json", self.downloads))
        self.signature = self.downloads / "colosseum-update-v1.json.sig"
        self.installer = Path(shutil.copy2(work / "fixtures" / PACKAGE, self.downloads))
        self.requests = []
        self.lock = threading.Lock()

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(inner, *args, **kwargs):
                super().__init__(*args, directory=self.root, **kwargs)

            def do_GET(inner):
                with self.lock:
                    self.requests.append({"time": time.time(), "path": inner.path,
                                          "user_agent": inner.headers.get("User-Agent", "")})
                super().do_GET()

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
        self.server.socket = make_tls(work).wrap_socket(self.server.socket, server_side=True)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def configure(self, *, corrupt):
        good = self.original_signature
        self.signature.write_bytes(bytes([good[0] ^ 1]) + good[1:] if corrupt else good)
        assets = [
            {"name": path.name, "size": path.stat().st_size, "digest": "sha256:" + digest(path),
             "browser_download_url": ORIGIN + "/" + path.relative_to(self.root).as_posix()}
            for path in (self.manifest, self.signature, self.installer)
        ]
        write_json(self.root / "updates/stable.json", {"tag_name": "v1.5.1", "draft": False,
                                                      "prerelease": False, "assets": assets})

    def saw_package(self):
        with self.lock:
            return any(row["path"].endswith("/" + PACKAGE) and row["user_agent"] == "Colosseum/1.5.0"
                       for row in self.requests)


class Capture:
    def __init__(self, work, evidence, install):
        self.work, self.evidence, self.install = work, evidence, install
        self.phase_file = work / "phase.txt"
        self.stop_file = work / "stop-observer"
        self.profile = None
        self.mark("before-staged-install")
        script = Path(__file__).with_name("windows_update_observe.ps1")
        self.log = (evidence / "observer.stderr.log").open("wb")
        self.observer = subprocess.Popen([
            "pwsh", "-NoProfile", "-NonInteractive", "-File", script, "-InstallRoot", install,
            "-PhaseFile", self.phase_file, "-StopFile", self.stop_file,
            "-Timeline", evidence / "process-timeline.jsonl", "-ReadyFile", work / "observer-ready",
        ], cwd=work, stdout=self.log, stderr=self.log)
        limit = time.monotonic() + 15
        while not (work / "observer-ready").is_file() and time.monotonic() < limit and self.observer.poll() is None:
            time.sleep(0.10)
        require((work / "observer-ready").is_file(), "Process observer did not start")

    def mark(self, phase):
        self.phase_file.write_text(phase, encoding="ascii")
        print(phase, flush=True)

    def capture_logs(self, phase):
        local = Path(os.environ["LOCALAPPDATA"])
        locations = {local / "Colosseum-update.log"}
        if self.profile:
            roaming = Path(os.environ["APPDATA"])
            relative = Path(self.profile["appDataRoot"]).relative_to(roaming)
            locations.add(local / relative / "updates/update-1.5.1.log")
            locations.add(Path(self.profile["cacheRoot"]).parent / "updates/update-1.5.1.log")
        locations = locations | {path.with_name(path.name + ".launch") for path in locations}
        log_locations = []
        for index, path in enumerate(sorted(locations)):
            row = {"path": str(path), "exists": path.is_file()}
            if row["exists"]:
                saved = f"{phase}-{index}-{path.name}"
                shutil.copy2(path, self.evidence / saved)
                row.update(file=saved, bytes=(self.evidence / saved).stat().st_size)
            log_locations.append(row)
        write_json(self.evidence / f"{phase}-installer-log-paths.json", log_locations)

    def snapshot(self, phase, *, version=False):
        self.mark(phase)
        state = {"phase": phase, "time": time.time(), "registry": reg_identity(), "paths": []}
        for suffix in ("", ".__update-old", ".__update-new"):
            root = self.install.with_name(self.install.name + suffix)
            exe = root / APP_EXE
            data = {"root": str(root), "root_exists": root.is_dir(), "exe": str(exe), "exe_exists": exe.is_file()}
            if data["exe_exists"]:
                data.update(sha256=digest(exe), bytes=exe.stat().st_size)
            state["paths"].append(data)
        if version and state["paths"][0]["exe_exists"]:
            proc = subprocess.run([self.install / APP_EXE, "--version"], cwd=self.work,
                                  capture_output=True, text=True, timeout=20)
            state["binary_version"] = {"code": proc.returncode, "stdout": proc.stdout.rstrip(),
                                       "stderr": proc.stderr.rstrip()}
        write_json(self.evidence / f"{phase}.json", state)
        self.capture_logs(phase)
        reg = state["registry"] or {}
        print(f"{phase}: registry={reg.get('DisplayVersion')} exe_sha256={state['paths'][0].get('sha256')}", flush=True)
        return state

    def close(self):
        self.stop_file.touch()
        try:
            self.observer.wait(10)
        except subprocess.TimeoutExpired:
            self.observer.terminate()
            self.observer.wait(10)
        self.log.close()


def diagnose(work):
    require(os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted",
            "Disposable hosted runner required")
    evidence = work / "diagnostic-evidence"
    evidence.mkdir()
    validate(work, evidence)
    install = Path(os.environ["LOCALAPPDATA"]) / "Programs/Colosseum"
    require(not install.exists() and reg_identity() is None, "Expected no existing Colosseum install or registry key")
    exe = install / APP_EXE
    capture = Capture(work, evidence, install)
    feed = None
    try:
        run(work / "fixtures/Colosseum-1.5.0-setup.exe", "/S", cwd=work, timeout=120)
        before = capture.snapshot("staged-installed-before-launch", version=True)
        require(before["registry"]["DisplayVersion"] == "1.5.0" and before["paths"][0]["sha256"] == EXPECTED["1.5.0"][2],
                "Initial install does not match the retained 1.5.0 identity")
        tag = f"staged-windows-upgrade-{os.environ['GITHUB_RUN_ID']}"
        os.environ.update(COLOSSEUM_APPDATA_TAG=tag + "-bad-feed", COLOSSEUM_LANISTA_PIPE=tag,
                          COLOSSEUM_LANISTA_DRIVE="1", COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0", QT_QUICK_BACKEND="software")
        app = App(evidence)
        feed = Feed(work, evidence)

        def start(label):
            with (evidence / f"{label}.stdout.log").open("wb") as stdout, (evidence / f"{label}.stderr.log").open("wb") as stderr:
                proc = subprocess.Popen([exe], cwd=install, stdout=stdout, stderr=stderr)
            capture.mark(f"{label}-launched-pid-{proc.pid}")
            return proc

        feed.configure(corrupt=True)
        bad = start("corrupt-signature")
        app.booted("Corrupt-signature precursor booted")
        app.enter_local_profile()
        app.open_updates()
        app.until("Corrupt release rejected", "qml-get",
                  lambda row: row.get("props", {}).get("text") == "Could not verify the release",
                  object="colosseumUpdateStatusText", props=["text"])
        require(not feed.saw_package(), "Corrupt release downloaded the installer")
        capture.mark("terminating-corrupt-precursor-as-in-original-run")
        bad.terminate()
        bad.wait(20)
        capture.mark("corrupt-precursor-exited")

        feed.configure(corrupt=False)
        os.environ["COLOSSEUM_APPDATA_TAG"] = tag
        prior = start("valid-feed")
        capture.profile = app.booted("Valid-feed 1.5.0 booted")
        app.enter_local_profile()
        profile = Path(capture.profile["appDataRoot"])
        require(tag in str(profile), "Rehearsal profile is outside its isolated root")
        marker = profile / "upgrade-preservation.json"
        marker.write_text('{"library":"fixture","progress":37,"preferences":"retained"}\n', encoding="utf-8")
        marker_digest = digest(marker)
        app.visible("colosseumTopbarUpdateBadge", True)
        app.open_updates()
        app.check_state("Available", TARGET)
        app.click("colosseumUpdatePrimaryAction")
        ready = app.check_state("Ready", TARGET, seconds=180)
        require(ready["primaryLabel"] == "Restart and update" and feed.saw_package(), "Verified installer download never reached Ready")
        capture.snapshot("before-restart-click")
        app.screenshot("before-restart-click")
        capture.mark(f"restart-click-prior-pid-{prior.pid}")
        try:
            app.click("colosseumUpdatePrimaryAction")
        except (OSError, json.JSONDecodeError):
            pass  # The old app can close its automation pipe while handling Restart and update.
        prior_code = prior.wait(90)
        capture.mark(f"prior-pid-{prior.pid}-exited-{prior_code}")
        post = app.booted("Post-restart app has a visible window", 170)
        capture.snapshot("first-relaunch-identity")
        app.visible("bootSplash", False)
        settled = capture.snapshot("settled-identity", version=True)
        app.screenshot("settled-home")
        badge = app.call("ui-query", object="colosseumTopbarUpdateBadge")
        write_json(evidence / "diagnostic-outcome.json", {
            "prior_exit_code": prior_code, "process_roots_before": capture.profile,
            "process_roots_after": post, "registry": settled["registry"], "paths": settled["paths"],
            "binary_version": settled.get("binary_version"), "badge_visible": badge["visible"],
            "marker_before": marker_digest, "marker_after": digest(marker) if marker.is_file() else None,
        })
    finally:
        # Always recover the installer output before any eventual identity comparison can stop the run.
        capture.capture_logs("final")
        if feed is not None:
            feed.server.shutdown()
            feed.server.server_close()
            write_json(evidence / "fixture-requests.json", feed.requests)
        capture.close()


if __name__ == "__main__":
    diagnose(Path(os.environ["RUNNER_TEMP"]) / "colosseum-update-diagnostic")
