#!/usr/bin/env python3
"""Two-phase normal-launcher Linux upgrade proof; never seeds fake app state."""
import argparse, configparser, hashlib, json, os, subprocess, time
from pathlib import Path
from smoke_installed import command

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def snapshot(profile):
    files = {}
    settings = {}
    for base in ("config", "data"):
        for p in sorted((profile / base).rglob("*")):
            if not p.is_file() or p.is_symlink():
                continue
            rel = str(p.relative_to(profile))
            files[rel] = digest(p)
            if p.suffix in (".ini", ".conf"):
                cfg = configparser.ConfigParser(interpolation=None, strict=False)
                cfg.optionxform = str
                try:
                    cfg.read_string(p.read_text())
                    settings[rel] = {s: dict(cfg.items(s)) for s in cfg.sections()}
                except (ValueError, UnicodeError, configparser.Error):
                    pass
    return {"files": files, "settings": settings}

def preserved_settings(old, new):
    checked = 0
    for path, sections in old.items():
        for section, values in sections.items():
            for key, value in values.items():
                assert new.get(path, {}).get(section, {}).get(key) == value, (path, section, key)
                checked += 1
    return checked

def wait_ready(address, process):
    deadline = time.monotonic() + 100
    last = None
    while time.monotonic() < deadline:
        assert process.poll() is None, "App exited before ready"
        try:
            state = command(address, "get-state")
            splash = command(address, "qml-get", {"object": "bootSplash", "props": ["visible"]})
            if any(w.get("visible") and w.get("width", 0) > 0 for w in state.get("windows", [])) and not splash["props"]["visible"]:
                return state
        except (OSError, RuntimeError, KeyError) as e:
            last = str(e)
        time.sleep(.5)
    raise RuntimeError("Normal launcher not ready: " + str(last))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=("baseline", "upgraded"), required=True)
    ap.add_argument("--profile", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    assert os.getuid() != 0, "ordinary user required"
    profile = args.profile.resolve()
    profile.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    baseline_path = args.output / "upgrade-baseline.json"
    baseline = json.loads(baseline_path.read_text()) if args.phase == "upgraded" else None
    version = subprocess.check_output(["/usr/bin/colosseum", "--version"], text=True).strip()
    expected = "1.1.10" if baseline is None else "1.2.0"
    assert version == "Colosseum " + expected, version
    identity = json.loads(Path("/opt/colosseum/BUILD-IDENTITY.json").read_text())
    if baseline:
        assert identity["source_commit"] == "74096d5baf629d55f3a58296c39be33b732f3d30"
        before = snapshot(profile)
        assert before == baseline["settled_profile"], "Package replacement modified user profile"
    else:
        assert not snapshot(profile)["files"], "Baseline profile must be empty"
    address = profile / "lanista.sock"
    address.unlink(missing_ok=True)
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(("QT_", "QML_", "QTWEBENGINE", "COLOSSEUM_", "GDK_PIXBUF_")) or key in ("LD_LIBRARY_PATH", "LD_PRELOAD", "QTDIR"):
            env.pop(key)
    env.update(XDG_CONFIG_HOME=str(profile/"config"), XDG_DATA_HOME=str(profile/"data"),
               XDG_CACHE_HOME=str(profile/"cache"), COLOSSEUM_LANISTA_PIPE=str(address),
               COLOSSEUM_LANISTA_DRIVE="1", QT_QPA_PLATFORM="xcb",
               QT_FORCE_STDERR_LOGGING="1", LIBGL_ALWAYS_SOFTWARE="1")
    receipt = {"phase": args.phase, "version": version, "package_identity": identity,
               "profile": str(profile), "argv": ["/usr/bin/colosseum"],
               "scope": "Normal launcher, app-created local-only account choice and settings across package upgrade; Collections/library/progress/audio NOT RUN"}
    try:
        with (args.output / ("upgrade-" + args.phase + ".log")).open("w") as log:
            process = subprocess.Popen(["/usr/bin/colosseum"], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                state = wait_ready(address, process)
                assert Path(state["appDataRoot"]).is_relative_to(profile/"data")
                receipt["state"] = state
                visible = command(address, "qml-get", {"object": "accountHost", "props": ["visible"]})["props"]["visible"]
                receipt["onboarding_initially_visible"] = visible
                if baseline:
                    assert visible is False, "App-owned local-only choice did not survive upgrade"
                    assert state["appDataRoot"] == baseline["state"]["appDataRoot"], "Profile identity changed"
                else:
                    assert visible is True, "Fresh profile did not show onboarding"
                    command(address, "ui-click", {"target": "accountWelcomeContinueLocal"})
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if command(address, "qml-get", {"object": "accountHost", "props": ["visible"]})["props"]["visible"] is False:
                            break
                        time.sleep(.2)
                    else:
                        raise RuntimeError("Local-only UI choice did not complete")
                    receipt["local_only_chosen_via_ui"] = True
                command(address, "ui-keypress", {"key": "Ctrl+Q"})
                process.wait(timeout=30)
                assert process.returncode == 0, process.returncode
                receipt["exit_code"] = process.returncode
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        receipt["settled_profile"] = snapshot(profile)
        assert receipt["settled_profile"]["files"], "No app-created durable files"
        if baseline:
            receipt["preserved_setting_keys"] = preserved_settings(baseline["settled_profile"]["settings"], receipt["settled_profile"]["settings"])
            receipt["package_replacement_preserved_all_baseline_files"] = True
            receipt["local_only_choice_preserved_after_restart"] = True
        receipt["status"] = "PASS"
    except Exception as e:
        receipt["status"] = "FAIL"
        receipt["error"] = str(e)
        raise
    finally:
        (args.output / ("upgrade-" + args.phase + ".json")).write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt))
if __name__ == "__main__":
    main()
