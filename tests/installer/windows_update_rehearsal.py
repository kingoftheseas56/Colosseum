"""Disposable-runner-only rehearsal of the installed Windows updater."""

import ctypes
import hashlib
import http.server
import json
import msvcrt
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


def replace_once(path, old, new):
    original = path.read_text(encoding="utf-8")
    require(original.count(old) == 1, f"Source anchor changed in {path.name}")
    path.write_text(original.replace(old, new), encoding="utf-8")


def openssl(*args):
    run(r"C:\Program Files\Git\usr\bin\openssl.exe", *args)


def prepare(repo, work):
    require(run("git", "rev-parse", "HEAD", cwd=repo, capture_output=True, text=True)
            .stdout.strip() == SOURCE, "Unexpected runtime source")
    require(not run("git", "status", "--porcelain", cwd=repo, capture_output=True, text=True)
            .stdout.strip(), "Fixture build requires clean source")
    keys = work / "keys"
    keys.mkdir(parents=True)
    openssl("genpkey", "-algorithm", "ED25519", "-out", keys / "manifest.pem")
    openssl("pkey", "-in", keys / "manifest.pem", "-pubout", "-outform", "DER",
            "-out", keys / "manifest-public.der")
    public = (keys / "manifest-public.der").read_bytes()
    require(public[:12].hex() == "302a300506032b6570032100" and len(public) == 44,
            "Unexpected Ed25519 key encoding")
    values = ", ".join(f"0x{byte:02x}" for byte in public[-32:])
    (repo / "native/update/UpdateTestPublicKey.h").write_text(
        "#pragma once\n#include <array>\n#include <cstdint>\n"
        "namespace Colosseum::Update {\n"
        f"inline constexpr std::array<std::uint8_t, 32> kUpdateTestPublicKey = {{{values}}};\n"
        "}\n", encoding="utf-8")

    ca, leaf = keys / "ca.pem", keys / "server.pem"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=Colosseum Windows rehearsal CA", "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", keys / "ca-key.pem", "-out", ca)
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=127.0.0.1",
            "-keyout", keys / "server-key.pem", "-out", keys / "server.csr")
    extensions = keys / "server-ext.cnf"
    extensions.write_text("subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n"
                          "basicConstraints=critical,CA:FALSE\n", encoding="ascii")
    openssl("x509", "-req", "-in", keys / "server.csr", "-CA", ca, "-CAkey", keys / "ca-key.pem",
            "-CAcreateserial", "-days", "2", "-extfile", extensions, "-out", leaf)
    openssl("verify", "-CAfile", ca, "-verify_ip", "127.0.0.1", leaf)
    main = repo / "native/main.cpp"
    replace_once(main, "#include <QNetworkAccessManager>",
                 "#include <QNetworkAccessManager>\n#include <QSslCertificate>\n"
                 "#include <QSslConfiguration>")
    replace_once(main, "    auto *catalogNam = new QNetworkAccessManager(&app);", """
    // This source exists only in the disposable update rehearsal runner.
    const QString fixtureCaPath = qEnvironmentVariable("COLOSSEUM_WINDOWS_REHEARSAL_CA");
    const auto fixtureCa = QSslCertificate::fromPath(fixtureCaPath);
    if (fixtureCaPath.isEmpty() || fixtureCa.size() != 1)
        qFatal("Rehearsal CA file missing or invalid");
    auto fixtureTls = QSslConfiguration::defaultConfiguration();
    fixtureTls.addCaCertificates(fixtureCa);
    QSslConfiguration::setDefaultConfiguration(fixtureTls);
    auto *catalogNam = new QNetworkAccessManager(&app);""".lstrip("\n"))
    print("Fixture TLS will read an ephemeral CA file inside the app process.", flush=True)


def package(repo, work):
    build = repo / "native/build-msvc"
    exe = build / "colosseum.exe"
    require(exe.is_file(), "Missing first fixture binary")
    (work / "bin").mkdir()
    shutil.copy2(exe, work / "bin/colosseum-1.5.0.exe")
    replace_once(repo / "native/main.cpp", '#include <QNetworkAccessManager>',
                 '#undef COLOSSEUM_VERSION\n#define COLOSSEUM_VERSION "1.5.1"\n#include <QNetworkAccessManager>')
    run("cmake", "--build", build, "--target", "colosseum", "--parallel", "2")
    shutil.copy2(exe, work / "bin/colosseum-1.5.1.exe")
    evidence = work / "evidence"
    evidence.mkdir()
    with (evidence / "fixture-source.diff").open("wb") as f:
        run("git", "diff", "--", "native/main.cpp", "native/update/UpdateTestPublicKey.h", cwd=repo, stdout=f)
    receipt = work / "shipping"
    receipt.mkdir()
    installer = receipt / "Colosseum-1.5.0-setup.exe"
    for _ in range(80):
        result = subprocess.run(["gh", "run", "download", PACKAGE_RUN,
                                 "--repo", "kingoftheseas56/Colosseum", "--name", "Colosseum-1.5.0-setup",
                                 "--dir", str(receipt)], capture_output=True, text=True)
        if result.returncode == 0 and installer.is_file():
            break
        time.sleep(15)
    require(installer.is_file() and result.returncode == 0, "Canonical installer artifact unavailable")
    print(f"Canonical runtime reference {digest(installer)}", flush=True)
    stage = work / "stage"
    run("7z", "x", installer, f"-o{stage}", "-y", "-bso0", "-bsp0",
        "-x!$PLUGINSDIR", "-x!uninstall.exe", "-x![NSIS].nsi")
    runtime = stage / "native/build-msvc"
    for name in ("colosseum.exe", "qml-build.manifest", "Qt6Core.dll", "stream_server/stremio-runtime.exe"):
        require((runtime / name).is_file(), f"Missing source package file {name}")
    require((runtime / "qml-build.manifest").read_bytes() == (build / "qml-build.manifest").read_bytes(),
            "Fixture and canonical installer QML manifests differ")
    fixtures = work / "fixtures"
    fixtures.mkdir()
    identity = {"runtime_source": SOURCE, "shipping_build_run": PACKAGE_RUN,
                "shipping_installer_sha256": digest(installer), "versions": {}}
    for version in ("1.5.0", "1.5.1"):
        print(f"Assembling staging installer {version}", flush=True)
        shutil.copy2(work / f"bin/colosseum-{version}.exe", runtime / "colosseum.exe")
        out = fixtures / f"Colosseum-{version}-setup.exe"
        run(r"C:\Program Files (x86)\NSIS\makensis.exe", "/V2", f"/DSTAGE={stage}",
            f"/DVERSION={version}", f"/DOUTFILE={out}", repo / "scripts/installer/colosseum.nsi")
        require(0 < out.stat().st_size <= 300 * 1024 * 1024, "Fixture installer size gate failed")
        identity["versions"][version] = {"installer_sha256": digest(out), "installer_bytes": out.stat().st_size,
                                          "exe_sha256": digest(runtime / "colosseum.exe")}
    (evidence / "package-identities.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")


def fixture_feed(repo, work):
    sys.path.insert(0, str(repo / "scripts/update"))
    from generate_update_manifest import manifest_bytes

    evidence = work / "evidence"
    presentation = work / "fixture-presentation.json"
    presentation.write_text(json.dumps({
        "eyebrow": "WINDOWS UPDATE REHEARSAL", "title": "Colosseum 1.5.1",
        "summary": "Isolated package upgrade fixture.",
        "notes_url": "https://github.com/kingoftheseas56/Colosseum/releases/tag/v1.5.1",
        "highlights": [{"kind": "feature", "section": "REHEARSAL", "title": "Windows upgrade",
                        "body": "Verify the complete in-app upgrade.", "artwork_assets": []}],
    }), encoding="utf-8")
    installer = work / "fixtures/Colosseum-1.5.1-setup.exe"
    files = work / "https"
    downloads = files / "updates/downloads/v1.5.1"
    downloads.mkdir(parents=True)
    manifest = downloads / "colosseum-update-v1.json"
    sig = downloads / "colosseum-update-v1.json.sig"
    manifest.write_bytes(manifest_bytes("1.5.1", installer, presentation))
    openssl("pkeyutl", "-sign", "-rawin", "-inkey", work / "keys/manifest.pem",
            "-in", manifest, "-out", sig)
    openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER", "-inkey",
            work / "keys/manifest-public.der", "-in", manifest, "-sigfile", sig)
    shutil.copy2(manifest, evidence / manifest.name)
    shutil.copy2(sig, evidence / sig.name)
    shutil.copy2(work / "keys/manifest-public.der", evidence / "manifest-public.der")
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

    requests = []
    lock = threading.Lock()

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=files, **kwargs)

        def do_GET(self):
            with lock:
                requests.append({"path": self.path, "user_agent": self.headers.get("User-Agent", "")})
            super().do_GET()

    def count(path):
        with lock:
            return sum(row["path"] == path for row in requests)

    def save_requests():
        with lock:
            (evidence / "fixture-requests.json").write_text(json.dumps(requests, indent=2), encoding="utf-8")

    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(work / "keys/server.pem", work / "keys/server-key.pem")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    configure()
    client_tls = ssl.create_default_context(cafile=work / "keys/ca.pem")
    with urllib.request.urlopen(FEED, context=client_tls, timeout=5) as response:
        require(json.load(response)["tag_name"] == "v1.5.1", "Invalid HTTPS fixture feed")
    return server, configure, count, save_requests


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
            deadline = time.monotonic() + 12
            message = bytearray()
            handle = ctypes.c_void_p(msvcrt.get_osfhandle(pipe.fileno()))
            available = ctypes.c_ulong()
            while b"\n" not in message and time.monotonic() < deadline:
                if not ctypes.windll.kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None):
                    raise OSError(f"Lanista pipe closed during {cmd}")
                if available.value:
                    message.extend(pipe.read(min(available.value, 256 * 1024)))
                    require(len(message) <= 2 * 1024 * 1024, f"Oversized Lanista response to {cmd}")
                else:
                    time.sleep(0.02)
            if b"\n" not in message:
                raise TimeoutError(f"No Lanista reply to {cmd} within 12 seconds")
            reply = json.loads(message.split(b"\n", 1)[0])
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


def verify(repo, work):
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
    identities = json.loads((evidence / "package-identities.json").read_text(encoding="utf-8"))["versions"]
    exe = install / "native/build-msvc/colosseum.exe"
    installer = work / "fixtures/Colosseum-1.5.0-setup.exe"
    require(digest(installer) == identities["1.5.0"]["installer_sha256"], "Prior installer digest drift")
    print("Installing exact staged NSIS 1.5.0", flush=True)
    run(installer, "/S", timeout=180)
    require(installed_version() == "1.5.0" and digest(exe) == identities["1.5.0"]["exe_sha256"],
            "Initial fixture install identity mismatch")
    tag = f"staged-windows-upgrade-{os.environ['GITHUB_RUN_ID']}"
    os.environ.update(COLOSSEUM_WINDOWS_REHEARSAL_CA=str(work / "keys/ca.pem"),
                      COLOSSEUM_APPDATA_TAG=tag + "-bad-feed", COLOSSEUM_LANISTA_PIPE=tag, COLOSSEUM_LANISTA_DRIVE="1",
                      COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0", QT_QUICK_BACKEND="software")
    app = App(evidence)
    server, configure, count, save_requests = fixture_feed(repo, work)
    signature_path = "/updates/downloads/v1.5.1/colosseum-update-v1.json.sig"
    package_path = "/updates/downloads/v1.5.1/Colosseum-1.5.1-setup.exe"

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
        require(count(signature_path) > 0 and count(package_path) == 0,
                "Negative case did not fetch the signature or unexpectedly fetched an installer")
        app.screenshot("01-invalid-signature")
        bad.terminate()
        bad.wait(20)

        configure()
        verified_state_request_count = count(signature_path)
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
        require(count(signature_path) > verified_state_request_count,
                "Visible badge was not backed by a fresh signed release check")
        app.screenshot("02-discovered-badge")
        app.open_updates()
        available = app.check_state("Available", "1.5.1")
        require(available["primaryLabel"] == "Download update", "Unexpected update action")
        app.click("colosseumUpdatePrimaryAction")
        ready = app.check_state("Ready", "1.5.1", seconds=180)
        require(ready["primaryLabel"] == "Restart and update", "Verified installer action unavailable")
        require(count(package_path) > 0, "No actual installer download observed")
        app.screenshot("03-installer-verified")
        print("Launching the verified installer through the app's Restart and update action", flush=True)
        try:
            app.click("colosseumUpdatePrimaryAction")
        except (OSError, json.JSONDecodeError):
            pass  # The old process may close its pipe during the real restart.
        require(prior.wait(90) == 0, "Prior process did not shut down normally")
        post = app.booted("Relaunched application has a visible window", 180)
        require(post["appDataRoot"] == boot["appDataRoot"], "Relaunch answered on a different profile")
        require(post["cacheRoot"] == boot["cacheRoot"], "Relaunch answered on a different cache root")
        app.visible("bootSplash", False)
        app.visible("accountHost", False)
        require(installed_version() == "1.5.1" and digest(exe) == identities["1.5.1"]["exe_sha256"],
                "Running installer did not install the expected 1.5.1 binary")
        process_version = run(exe, "--version", cwd=install, capture_output=True, text=True).stdout.strip()
        require(process_version == "Colosseum 1.5.1", f"Installed build reported {process_version!r}")
        require(digest(record) == record_sha, "Prior user data changed across the upgrade")
        require(not install.with_name("Colosseum.__update-old").exists(), "Healthy restart did not clean its prior payload")
        installer_log = Path(boot["cacheRoot"]).parent / "updates/update-1.5.1.log"
        if installer_log.is_file():
            shutil.copy2(installer_log, evidence / "actual-nsis-update.log")
        print("Actual in-app restart installed the expected 1.5.1 and retained the profile.", flush=True)
        app.visible("colosseumTopbarUpdateBadge", False)
        app.screenshot("04-after-restart-no-badge")
        app.open_updates()
        current = app.check_state("UpToDate", "1.5.1")
        require(current["primaryLabel"] == "Check again", "Same version offered a download")
        same_version_requests = count(signature_path)
        app.click("colosseumUpdatePrimaryAction")
        deadline = time.monotonic() + 35
        while count(signature_path) == same_version_requests and time.monotonic() < deadline:
            time.sleep(0.1)
        require(count(signature_path) > same_version_requests, "Check again did not fetch a fresh signature")
        app.check_state("UpToDate", "1.5.1")
        app.screenshot("05-same-version")
        (evidence / "windows-rehearsal.json").write_text(json.dumps({
            "status": "pass", "installed_version": process_version, "invalid_signature": rejected,
            "discovery": available, "verified_download": ready, "same_version": current,
            "preserved_user_data_sha256": record_sha, "appdata_before": boot["appDataRoot"],
            "appdata_after": post["appDataRoot"], "cache_before": boot["cacheRoot"],
            "cache_after": post["cacheRoot"], "badge_before": True, "badge_after": False,
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
        save_requests()


if __name__ == "__main__":
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "Disposable runner required")
    action = {"prepare": prepare, "package": package, "verify": verify}[sys.argv[1]]
    action(Path(os.environ["GITHUB_WORKSPACE"]), Path(os.environ["RUNNER_TEMP"]) / "colosseum-update-rehearsal")
