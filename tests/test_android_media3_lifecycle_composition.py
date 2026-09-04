from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QML = (ROOT / "qml/PlayerPage.qml").read_text(encoding="utf-8")
JAVA = (ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java").read_text(encoding="utf-8")
CPP = (ROOT / "native/player/androidmedia3item.cpp").read_text(encoding="utf-8")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def method_slice(text: str, start: str, end: str) -> str:
    begin = text.find(start)
    finish = text.find(end, begin + len(start))
    if begin < 0 or finish < 0:
        raise AssertionError(f"unable to isolate {start}")
    return text[begin:finish]


def test_runtime_drives_player_lifecycle_and_surface() -> None:
    require(QML, "target: PlatformRuntime", "PlayerPage must listen to W02 lifecycle authority")
    require(QML, "function onApplicationStateChanged()", "lifecycle signal must be handled")
    require(QML, "mpv.setHostLifecycleState(PlatformRuntime.applicationState)",
            "PlayerItem must receive the exact host lifecycle state")
    require(QML, "function onSurfaceAvailableChanged()", "surface recreation must be handled")
    require(QML, "mpv.releaseVideoSurface()", "surface loss must release Media3 video surface")
    require(QML, "mpv.restoreVideoSurface()", "surface recreation must restore Media3 video surface")


def test_same_generation_restore_prepares_retained_media_source() -> None:
    require(JAVA, "public void prepareForLifecycleRestore()",
            "Java host needs an explicit same-generation restore primitive")
    restore = method_slice(JAVA, "public void prepareForLifecycleRestore()", "public void seekTo(")
    require(restore, "player.prepare();", "restore must prepare the retained MediaSource")
    require(restore, "userWantsPlay", "restore must preserve semantic user play intent")
    if "clearMediaItems" in restore or "setMediaSource" in restore:
        raise AssertionError("same-generation lifecycle restore must not replace/clear the MediaSource")

    lifecycle = method_slice(
        CPP,
        "void AndroidMedia3Item::setHostLifecycleState",
        "void AndroidMedia3Item::setAudioFocusState",
    )
    require(lifecycle, 'callHost("stopForLifecycle")', "background must stop transport non-terminally")
    require(lifecycle, 'callHost("prepareForLifecycleRestore")',
            "foreground must prepare the retained Java MediaSource")
    if 'callMethod<void>("load"' in lifecycle:
        raise AssertionError("same-generation foreground restore must not rebuild the source through load()")


if __name__ == "__main__":
    test_runtime_drives_player_lifecycle_and_surface()
    test_same_generation_restore_prepares_retained_media_source()
    print("android media3 lifecycle composition: PASS")
