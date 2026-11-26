"""Disposable-runner-only rehearsal of the installed Windows updater."""

import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import threading
import time
import urllib.request
import winreg


PORT = 18443
ORIGIN = f"https://127.0.0.1:{PORT}"
FEED = ORIGIN + "/updates/stable.json"
SOURCE = "d73111cddfef5728aa6b714fdb52fdf030b96ec6"
PACKAGE_RUN = "45885892234"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def openssl(*args):
    run(r"C:\Program Files\Git\usr\bin\openssl.exe", *args)


def prepare_tls(work):
    keys = work / "keys"
    keys.mkdir(parents=True)
    ca, leaf = keys / "ca.pem", keys / "server.pem"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=Colosseum Windows replay CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign",
            "-keyout", keys / "ca-key.pem", "-out", ca)
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=127.0.0.1",
            "-keyout", keys / "server-key.pem", "-out", keys / "server.csr")
    extensions = keys / "server-ext.cnf"
    extensions.write_text("subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n"
                          "basicConstraints=critical,CA:FALSE\n", encoding="ascii")
    openssl("x509", "-req", "-in", keys / "server.csr", "-CA", ca, "-CAkey", keys / "ca-key.pem",
            "-CAcreateserial", "-days", "2", "-extfile", extensions, "-out", leaf)
    openssl("verify", "-CAfile", ca, "-verify_ip", "127.0.0.1", leaf)
    openssl("x509", "-in", ca, "-outform", "DER", "-out", keys / "ca.cer")


def validate_received(work):
    incoming = work / "received-evidence"
    identities = json.loads((incoming / "package-identities.json").read_text(encoding="utf-8"))
    require(identities["runtime_source"] == SOURCE and identities["shipping_build_run"] == PACKAGE_RUN,
            "Retained fixture revision changed")
    require(identities["shipping_installer_sha256"] ==
            "2f7d879dcae87255befaa83f76baa689de20c8e624d6bb23f1ed07e5fe1789aa",
            "Production reference hash changed")
    expected = {
        "1.5.0": (249597509, "dcde010ff96655607c7465055777ca8a3494aa307019dab720f6b8ca12ff2fbb"),
        "1.5.1": (249474028, "09c80147305a75f29904d8dbed6457d88c636ec567516efb3d394eb938a21aef"),
    }
    for version, (size, sha) in expected.items():
        installer = work / "fixtures" / f"Colosseum-{version}-setup.exe"
        saved = identities["versions"][version]
        require(installer.stat().st_size == size == saved["installer_bytes"]
                and digest(installer) == sha == saved["installer_sha256"],
                f"Retained {version} installer bytes changed")
    manifest = incoming / "colosseum-update-v1.json"
    signature = incoming / "colosseum-update-v1.json.sig"
    data = json.loads(manifest.read_bytes())
    package_size, package_sha = expected["1.5.1"]
    require(data["version"] == "1.5.1" and data["tag"] == "v1.5.1"
            and data["installer"] == {"asset": "Colosseum-1.5.1-setup.exe", "size": package_size, "sha256": package_sha},
            "Retained manifest does not identify the exact 1.5.1 installer")
    require(signature.stat().st_size == 64, "Retained Ed25519 signature is not 64 bytes")
    openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER", "-inkey",
            incoming / "manifest-public.der", "-in", manifest, "-sigfile", signature)
    evidence = work / "evidence"
    evidence.mkdir()
    for name in ("package-identities.json", manifest.name, signature.name, "manifest-public.der", "fixture-source.diff"):
        shutil.copy2(incoming / name, evidence / name)
    print("Verified both retained NSIS installers and retained signed manifest.", flush=True)


def fixture_feed(work):
    evidence = work / "evidence"
    installer = work / "fixtures/Colosseum-1.5.1-setup.exe"
    files = work / "https"
    downloads = files / "updates/downloads/v1.5.1"
    downloads.mkdir(parents=True)
    manifest = downloads / "colosseum-update-v1.json"
    sig = downloads / "colosseum-update-v1.json.sig"
    shutil.copy2(evidence / manifest.name, manifest)
    installer = Path(shutil.copy2(installer, downloads / installer.name))
    feed = files / "updates/stable.json"

    def configure(corrupt=False):
        valid_signature = (evidence / sig.name).read_bytes()
        sig.write_bytes(bytes([valid_signature[0] ^ 1]) + valid_signature[1:] if corrupt else valid_signature)
        envelope = {"tag_name": "v1.5.1", "draft": False, "prerelease": False, "assets": [
            {"name": p.name, "size": p.stat().st_size, "digest": f"sha256:{digest(p)}",
             "browser_download_url": ORIGIN + "/" + p.relative_to(files).as_posix()}
            for p in (manifest, sig, installer)]}
        feed.write_text(json.dumps(envelope) + "\n", encoding="utf-8")

    def handler(*args, **kwargs):
        return http.server.SimpleHTTPRequestHandler(*args, directory=files, **kwargs)

    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(work / "keys/server.pem", work / "keys/server-key.pem")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), handler)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    configure()
    client_tls = ssl.create_default_context(cafile=work / "keys/ca.pem")
    with urllib.request.urlopen(FEED, context=client_tls, timeout=5) as response:
        require(json.load(response)["tag_name"] == "v1.5.1", "Invalid HTTPS fixture feed")
    return server, configure


class App:
    def __init__(self, evidence):
        self.evidence = evidence
        self.pipe = rf"\\.\pipe\{os.environ['COLOSSEUM_LANISTA_PIPE']}"

    def call(self, cmd, **payload):
        line = json.dumps({"seq": 1, "cmd": cmd, "payload": payload}).encode() + b"\n"
        if not ctypes.windll.kernel32.WaitNamedPipeW(self.pipe, 250):
            raise FileNotFoundError(self.pipe)
        with open(self.pipe, "r+b", buffering=0) as pipe:
            pipe.write(line)
            reply = json.loads(pipe.readline())
        require(reply.get("type") != "error", f"{cmd}: {reply}")
        return reply.get("result", reply.get("payload", reply))

    def until(self, message, cmd, predicate, seconds=60, **payload):
        deadline = time.monotonic() + seconds
        last = None
        while time.monotonic() < deadline:
            try:
                last = self.call(cmd, **payload)
                if predicate(last):
                    print(message, flush=True)
                    return last
            except (OSError, RuntimeError, json.JSONDecodeError) as error:
                last = str(error)
            time.sleep(0.5)
        raise RuntimeError(f"{message}: last response {last}")

    def visible(self, name, value):
        return self.until(f"{name} visible={value}", "ui-query", lambda row: row.get("visible") is value,
                          object=name)

    def booted(self, message, seconds=60):
        return self.until(message, "get-state",
                          lambda row: any(w.get("visible") for w in row.get("windows", [])), seconds)

    def check_state(self, name, version=None, seconds=60):
        data = self.until(f"Update page state {name}", "qml-get",
                          lambda row: row.get("props", {}).get("automationState") == name, seconds,
                          object="colosseumUpdatePage", props=["automationState", "automationVersion", "primaryLabel"])
        if version:
            require(data["props"]["automationVersion"] == version, "Version did not match visible offer")
        return data["props"]

    def click(self, target):
        result = self.call("ui-click", target=target)
        require(result.get("clicked") == target, f"Click was not delivered to {target}: {result}")

    def enter_local_profile(self):
        self.visible("bootSplash", False)
        self.visible("accountWelcomeContinueLocal", True)
        self.click("accountWelcomeContinueLocal")
        self.visible("accountHost", False)
        self.visible("colosseumTopbarUpdateButton", True)

    def open_updates(self):
        self.visible("colosseumTopbarUpdateButton", True)
        self.click("colosseumTopbarUpdateButton")
        self.visible("colosseumUpdatePage", True)

    def screenshot(self, name):
        state = self.call("get-state", grab={"target": "window", "timeoutMs": 5000})
        shutil.copy2(state["grabPath"], self.evidence / f"{name}.png")


def installed_version():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                        r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum") as key:
        return winreg.QueryValueEx(key, "DisplayVersion")[0]


def verify(work):
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "Disposable runner required")
    install = Path(os.environ["LOCALAPPDATA"]) / "Programs/Colosseum"
    require(not install.exists(), "Existing Colosseum installation on runner")
    try:
        installed_version()
    except FileNotFoundError:
        pass
    else:
        raise RuntimeError("Existing uninstall registration on runner")

    evidence = work / "evidence"
    validate_received(work)
    identities = json.loads((evidence / "package-identities.json").read_text(encoding="utf-8"))["versions"]
    exe = install / "native/build-msvc/colosseum.exe"
    installer = work / "fixtures/Colosseum-1.5.0-setup.exe"
    require(digest(installer) == identities["1.5.0"]["installer_sha256"], "Prior installer digest drift")
    run(installer, "/S", timeout=180)
    require(installed_version() == "1.5.0" and digest(exe) == identities["1.5.0"]["exe_sha256"],
            "Initial fixture install identity mismatch")
    tag = f"staged-windows-upgrade-{os.environ['GITHUB_RUN_ID']}"
    os.environ.update(COLOSSEUM_APPDATA_TAG=tag + "-bad-feed", COLOSSEUM_LANISTA_PIPE=tag, COLOSSEUM_LANISTA_DRIVE="1",
                      COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0", QT_QUICK_BACKEND="software")
    app = App(evidence)
    server, configure = fixture_feed(work)

    def start(label):
        with (evidence / f"{label}.stdout.log").open("wb") as stdout, (evidence / f"{label}.stderr.log").open("wb") as stderr:
            return subprocess.Popen([exe], cwd=install, stdout=stdout, stderr=stderr)

    try:
        configure(corrupt=True)
        bad = start("corrupt-signature")
        app.booted("Invalid-signature fixture booted")
        app.enter_local_profile()
        app.visible("colosseumTopbarUpdateBadge", False)
        app.open_updates()
        rejected = app.until("Corrupt release signature rejected", "qml-get",
                             lambda row: row.get("props", {}).get("text") == "Could not verify the release",
                             object="colosseumUpdateStatusText", props=["text"])["props"]["text"]
        app.visible("colosseumTopbarUpdateBadge", False)
        app.screenshot("01-invalid-signature")
        bad.terminate()
        bad.wait(20)

        configure()
        os.environ["COLOSSEUM_APPDATA_TAG"] = tag
        prior = start("valid-feed")
        boot = app.booted("1.5.0 fixture booted")
        app.enter_local_profile()
        profile = Path(boot["appDataRoot"])
        require(tag in str(profile), "Rehearsal profile was not isolated")
        record = profile / "upgrade-preservation.json"
        record.write_text('{"library":"fixture","progress":37,"preferences":"retained"}\n', encoding="utf-8")
        record_sha = digest(record)
        app.visible("colosseumTopbarUpdateBadge", True)
        app.screenshot("02-discovered-badge")
        app.open_updates()
        available = app.check_state("Available", "1.5.1")
        require(available["primaryLabel"] == "Download update", "Unexpected update action")
        app.click("colosseumUpdatePrimaryAction")
        ready = app.check_state("Ready", "1.5.1", seconds=180)
        require(ready["primaryLabel"] == "Restart and update", "Verified installer action unavailable")
        app.screenshot("03-installer-verified")
        try:
            app.click("colosseumUpdatePrimaryAction")
        except (OSError, json.JSONDecodeError):
            pass  # The old process may close its pipe during the real restart.
        require(prior.wait(90) == 0, "Prior process did not shut down normally")
        post = app.booted("Relaunched application has a visible window", 180)
        require(post["appDataRoot"] == boot["appDataRoot"], "Relaunch answered on a different profile")
        app.visible("bootSplash", False)
        app.visible("accountHost", False)
        require(installed_version() == "1.5.1" and digest(exe) == identities["1.5.1"]["exe_sha256"],
                "Running installer did not install the expected 1.5.1 binary")
        process_version = run(exe, "--version", cwd=install, capture_output=True, text=True).stdout.strip()
        require(process_version == "Colosseum 1.5.1", f"Installed build reported {process_version!r}")
        require(digest(record) == record_sha, "Prior user data changed across the upgrade")
        require(not install.with_name("Colosseum.__update-old").exists(), "Healthy restart did not clean its prior payload")
        app.visible("colosseumTopbarUpdateBadge", False)
        app.screenshot("04-after-restart-no-badge")
        app.open_updates()
        current = app.check_state("UpToDate", "1.5.1")
        require(current["primaryLabel"] == "Check again", "Same version offered a download")
        app.click("colosseumUpdatePrimaryAction")
        app.check_state("UpToDate", "1.5.1")
        app.screenshot("05-same-version")
        (evidence / "windows-rehearsal.json").write_text(json.dumps({
            "status": "pass", "installed_version": process_version, "invalid_signature": rejected,
            "discovery": available, "verified_download": ready, "same_version": current,
            "preserved_user_data_sha256": record_sha, "appdata_before": boot["appDataRoot"],
            "appdata_after": post["appDataRoot"], "badge_before": True, "badge_after": False,
        }, indent=2) + "\n", encoding="utf-8")
    except Exception:
        try:
            app.screenshot("driver-failure")
            state = {name: app.call("ui-query", object=name) for name in ("bootSplash", "accountHost", "updateLayer")}
            (evidence / "driver-failure-ui.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
        except Exception as error:
            print(f"Driver failure screenshot or UI query unavailable: {error}", flush=True)
        raise
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "Disposable runner required")
    work = Path(os.environ["RUNNER_TEMP"]) / "colosseum-update-rehearsal"
    {"prepare-tls": prepare_tls, "verify": verify}[sys.argv[1]](work)
