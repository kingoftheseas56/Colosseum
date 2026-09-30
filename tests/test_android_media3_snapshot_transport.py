from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = (ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java").read_text(encoding="utf-8")
CPP = (ROOT / "native/player/androidmedia3item.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "native/player/androidmedia3item.h").read_text(encoding="utf-8")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def test_snapshot_transport() -> None:
    for token in ("TELEMETRY_INTERVAL_MS", "telemetryRunnable", "startTelemetry()", "stopTelemetry()"):
        require(JAVA, token, "Media3 host needs bounded periodic telemetry")
    require(JAVA, "nativeOnPlaybackSnapshot(", "Java must publish playback snapshots")
    require(JAVA, "public void stopForLifecycle()", "lifecycle transport stop must be explicit")
    require(JAVA, "userWantsPlay", "user intent must survive lifecycle stop")
    require(JAVA, "mainHandler.removeCallbacks(telemetryRunnable)", "release/stop must cancel telemetry")
    require(HEADER, "handlePlaybackSnapshot", "native facade must consume snapshots")
    require(CPP, "Media3PlayerHost_nativeOnPlaybackSnapshot", "JNI snapshot bridge must exist")
    forbidden = 'callHost("pause");\n            callHost("stopForLifecycle");'
    if forbidden in CPP:
        raise AssertionError("lifecycle host stop must preserve user play intent without Java pause()")


if __name__ == "__main__":
    test_snapshot_transport()
    print("android media3 snapshot transport: PASS")
