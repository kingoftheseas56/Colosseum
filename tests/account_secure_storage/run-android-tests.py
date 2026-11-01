"""Run the isolated storage test APK across Android process restarts."""

import argparse
import base64
import re
import subprocess
import time

PACKAGE = "org.brotherhood.colosseum.accountstoragetest"
ACTIVITY = PACKAGE + "/org.qtproject.qt.android.bindings.QtActivity"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", required=True, help="Debug storage-test APK")
    args = parser.parse_args()

    def adb(*arguments):
        return subprocess.run(
            ["adb", "-s", args.serial, *arguments],
            check=True, capture_output=True, text=True, timeout=60,
        ).stdout

    def run_step(mode=None):
        adb("shell", "am", "force-stop", PACKAGE)
        adb("logcat", "-c")
        arguments = ["shell", "am", "start", "-W", "-n", ACTIVITY]
        if mode:
            encoded = base64.b64encode(mode.encode()).decode()
            arguments += ["--es", "extraappparams", encoded]
        adb(*arguments)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            logs = adb("logcat", "-d", "-s", "default:I", "QTestLib:I", "AndroidRuntime:E")
            if mode:
                if f"ACCOUNT_STORAGE_MODE {mode} PASS" in logs:
                    print(f"{mode}: PASS", flush=True)
                    return
                if f"ACCOUNT_STORAGE_MODE {mode} FAIL" in logs:
                    raise RuntimeError(logs)
            else:
                totals = re.search(r"Totals: (\d+) passed, (\d+) failed", logs)
                if totals:
                    if int(totals[2]) != 0:
                        raise RuntimeError(logs)
                    print(f"contract: {totals[1]} passed", flush=True)
                    return
            time.sleep(0.5)
        raise RuntimeError(f"No result for {mode or 'contract'}: {logs}")

    adb("install", "-r", args.apk)
    try:
        run_step()
        # Clear only fixture entries in this separate package before seeding.
        run_step("--clear")
        for mode in ("--seed", "--verify", "--tamper", "--verify", "--key-loss",
                     "--clear", "--empty", "--seed", "--verify", "--clear", "--empty"):
            run_step(mode)
    finally:
        adb("shell", "am", "force-stop", PACKAGE)


if __name__ == "__main__":
    main()
