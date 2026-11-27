"""Receive the exact staged Windows 1.5.1 and verify a fresh signed Check again."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from retained_feed import Feed
from windows_update_rehearsal import App, digest, installed_version, openssl, require, run

PACKAGE = "Colosseum-1.5.1-setup.exe"
MANIFEST = "colosseum-update-v1.json"
SIGNATURE = MANIFEST + ".sig"
PUBLIC = "manifest-public.der"
ASSET_ROOT = "/updates/downloads/v1.5.1/"


def receiving_identity(work, evidence):
    received = work / "received-evidence"
    identities = json.loads((received / "package-identities.json").read_bytes())
    require(identities["runtime_source"] == os.environ["SOURCE_SHA"], "Staged source mismatch")
    require(str(identities["shipping_build_run"]) == os.environ["PRODUCER_RUN"], "Wrong production runtime")
    require(identities["shipping_installer_sha256"] == os.environ["PRODUCTION_INSTALLER_SHA"],
            "Wrong production reference installer digest")
    target = identities["versions"]["1.5.1"]
    installer = work / "fixtures" / PACKAGE
    require(installer.stat().st_size == target["installer_bytes"]
            and digest(installer) == target["installer_sha256"] == os.environ["TARGET_INSTALLER_SHA"]
            and target["exe_sha256"] == os.environ["TARGET_EXE_SHA"], "Staged 1.5.1 identity mismatch")
    for name in (MANIFEST, SIGNATURE, PUBLIC, "package-identities.json"):
        shutil.copy2(received / name, evidence / name)
    require(digest(evidence / MANIFEST) == os.environ["MANIFEST_SHA"]
            and digest(evidence / SIGNATURE) == os.environ["SIGNATURE_SHA"]
            and digest(evidence / PUBLIC) == os.environ["PUBLIC_KEY_SHA"], "Signed feed identity drift")
    require((evidence / SIGNATURE).stat().st_size == 64, "Wrong detached signature size")
    openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER",
            "-inkey", evidence / PUBLIC, "-in", evidence / MANIFEST, "-sigfile", evidence / SIGNATURE)
    signed_installer = json.loads((evidence / MANIFEST).read_bytes())["installer"]
    require(signed_installer == {"asset": PACKAGE, "size": target["installer_bytes"],
                                 "sha256": target["installer_sha256"]}, "Signed package identity mismatch")
    return installer, target


def main():
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted", "Disposable runner required")
    work = Path(os.environ["RUNNER_TEMP"]) / "colosseum-same-version-receiving"
    evidence = work / "evidence"
    evidence.mkdir()
    installer, target = receiving_identity(work, evidence)
    install = Path(os.environ["LOCALAPPDATA"]) / "Programs/Colosseum"
    require(not install.exists(), "Unexpected existing Windows installation on runner")
    try:
        installed_version()
    except FileNotFoundError:
        pass
    else:
        raise RuntimeError("Unexpected existing Windows registration on runner")
    run(installer, "/S", timeout=180)
    exe = install / "native/build-msvc/colosseum.exe"
    require(installed_version() == "1.5.1" and digest(exe) == target["exe_sha256"], "Installed 1.5.1 identity mismatch")
    version = run(exe, "--version", cwd=install, timeout=20, capture_output=True, text=True).stdout.strip()
    require(version == "Colosseum 1.5.1", f"Wrong installed CLI version {version!r}")
    tag = f"staged-windows-same-version-{os.environ['GITHUB_RUN_ID']}"
    os.environ.update(COLOSSEUM_APPDATA_TAG=tag + " Test User's profile", COLOSSEUM_LANISTA_PIPE=tag,
                      COLOSSEUM_LANISTA_DRIVE="1", COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0",
                      QT_QUICK_BACKEND="software")
    feed = Feed(work, evidence)
    process = None
    try:
        feed.configure()
        feed.verify_origin()
        with (evidence / "app.stdout.log").open("wb") as stdout, (evidence / "app.stderr.log").open("wb") as stderr:
            process = subprocess.Popen([exe], cwd=install, stdout=stdout, stderr=stderr)
        app = App(evidence)
        boot = app.booted("Exact Windows 1.5.1 installed fixture booted")
        app.enter_local_profile()
        app.open_updates()
        before_state = app.check_state("UpToDate", "1.5.1")
        require(before_state["primaryLabel"] == "Check again", "Same version unexpectedly offered an update")
        app.visible("colosseumTopbarUpdateBadge", False)

        def package_and_signature_counts():
            with feed.lock:
                rows = [row for row in feed.requests if row.get("user_agent") == "Colosseum/1.5.1"]
                return {asset: sum(row.get("path") == ASSET_ROOT + asset for row in rows)
                        for asset in (MANIFEST, SIGNATURE, PACKAGE)}

        before = package_and_signature_counts()
        require(before[MANIFEST] > 0 and before[SIGNATURE] > 0 and before[PACKAGE] == 0,
                "Initial installed check did not verify the signed feed without fetching the package")
        app.click("colosseumUpdatePrimaryAction")
        deadline = time.monotonic() + 45
        while package_and_signature_counts()[SIGNATURE] == before[SIGNATURE] and time.monotonic() < deadline:
            time.sleep(0.1)
        after = package_and_signature_counts()
        require(after[MANIFEST] > before[MANIFEST] and after[SIGNATURE] > before[SIGNATURE]
                and after[PACKAGE] == 0, "Check again did not freshly fetch both signed objects without the package")
        after_state = app.check_state("UpToDate", "1.5.1")
        require(after_state["primaryLabel"] == "Check again", "Fresh check offered a same-version download")
        app.visible("colosseumTopbarUpdateBadge", False)
        app.screenshot("up-to-date-after-fresh-signed-check")
        result = {"status": "pass", "scope": "exact retained 1.5.1 same-version receiving",
                  "source": os.environ["SOURCE_SHA"], "producer_run": os.environ["PRODUCER_RUN"],
                  "installed_version": installed_version(), "cli_version": version, **target,
                  "app_pid": process.pid, "app_data": boot["appDataRoot"], "badge_visible": False,
                  "before_state": before_state, "after_state": after_state,
                  "requests_before_click": before, "requests_after_click": after}
        (evidence / "same-version-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2), flush=True)
    finally:
        feed.server.shutdown()
        feed.server.server_close()
        (evidence / "fixture-requests.json").write_text(json.dumps(feed.requests, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
