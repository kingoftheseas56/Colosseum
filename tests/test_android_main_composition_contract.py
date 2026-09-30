from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "native" / "main.cpp").read_text(encoding="utf-8")


def require(pattern: str, reason: str) -> None:
    if re.search(pattern, MAIN, re.DOTALL | re.MULTILINE) is None:
        raise AssertionError(reason)


def test_desktop_player_dependencies_are_platform_guarded() -> None:
    require(r"#if !defined\(Q_OS_ANDROID\).*?#include <QtWebEngineQuick/QtWebEngineQuick>.*?#endif",
            "QtWebEngine include must be desktop-only")
    require(r"#if !defined\(Q_OS_ANDROID\).*?QtWebEngineQuick::initialize\(\);.*?#endif",
            "QtWebEngine initialization must be desktop-only")
    require(r"#if !defined\(Q_OS_ANDROID\).*?#include \"player/MediaAdmissionProbe\.h\".*?#endif",
            "libmpv MediaAdmissionProbe include must be desktop-only")
    require(r"#if !defined\(Q_OS_ANDROID\).*?MediaAdmissionProbe::probe\(r\.path\).*?#endif",
            "libmpv Vault video admission must be desktop-only")


if __name__ == "__main__":
    test_desktop_player_dependencies_are_platform_guarded()
    print("android main composition contract: PASS")
