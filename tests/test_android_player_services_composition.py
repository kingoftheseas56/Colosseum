from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "native/CMakeLists.txt").read_text(encoding="utf-8")
STREAM = ROOT / "native/player/streamserver_android.cpp"
LIVE = ROOT / "native/player/livestore_android.cpp"


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def test_android_player_services_have_platform_implementations() -> None:
    require(CMAKE, "player/streamserver_android.cpp", "Android must not link desktop Stremio supervisor")
    require(CMAKE, "player/livestore_android.cpp", "Android needs a QML-compatible LiveStore")
    require(CMAKE, "player/powerstore.cpp", "Android must retain real power inhibition")
    if not STREAM.exists() or not LIVE.exists():
        raise AssertionError("Android player service implementations must exist")
    stream = STREAM.read_text(encoding="utf-8")
    live = LIVE.read_text(encoding="utf-8")
    require(stream, "m_engineUnavailable = true", "Android stream seam must report Arc 44 dependency honestly")
    for forbidden in ("QProcess", "server.js", "stremio-runtime"):
        if forbidden in stream:
            raise AssertionError(f"Android StreamServer must not supervise desktop runtime: {forbidden}")
    if "QProcess" in live:
        raise AssertionError("Android LiveStore must not launch a desktop DVR process")


if __name__ == "__main__":
    test_android_player_services_have_platform_implementations()
    print("android player services composition: PASS")
