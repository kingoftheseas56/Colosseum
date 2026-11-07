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
    require(JAVA, "userWantsPlay", "Java host must retain explicit user intent machinery")
    require(JAVA, "mainHandler.removeCallbacks(telemetryRunnable)", "release/stop must cancel telemetry")
    require(HEADER, "handlePlaybackSnapshot", "native facade must consume snapshots")
    require(CPP, "Media3PlayerHost_nativeOnPlaybackSnapshot", "JNI snapshot bridge must exist")
    require(CPP, 'callHost("pause");\n            callHost("stopForLifecycle");',
            "Android background return must park both Java and native play intent")
    require(CPP, "noteLifecyclePrepareSubmitted(generation, lifecycleEpoch)",
            "resume preparation must still be generation/epoch fenced")
    state = (ROOT / "native/player/androidmedia3state.cpp").read_text(encoding="utf-8")
    stop = state.split("AndroidMedia3State::beginLifecycleHostStop(", 1)[1].split("\n}", 1)[0]
    require(stop, "m_snapshot.userWantsPlay = false", "OS return cannot silently issue Play")
    require(stop, "m_snapshot.paused = true", "OS return preserves the parked state")


if __name__ == "__main__":
    test_snapshot_transport()
    print("android media3 snapshot transport: PASS")
