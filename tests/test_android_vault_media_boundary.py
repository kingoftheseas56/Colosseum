from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "native/CMakeLists.txt").read_text(encoding="utf-8")
THUMB = (ROOT / "native/engine/VaultThumbnailer.cpp").read_text(encoding="utf-8")
ENRICH = (ROOT / "native/engine/VaultEnricher.cpp").read_text(encoding="utf-8")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def forbid(text: str, token: str, why: str) -> None:
    if token in text:
        raise AssertionError(f"{why}: found forbidden {token!r}")


def main() -> None:
    require(CMAKE, "player/MediaAdmissionProbe_android.cpp",
            "Android must provide admission without desktop libmpv")
    require(CMAKE, "engine/VaultThumbnailer_android.cpp",
            "Android must provide native Vault thumbnail extraction")
    forbid(THUMB, '#include "player/mpvitem.h"',
           "common Vault thumbnail code must not import MpvItem")
    java = ROOT / "native/platform/android/src/org/colosseum/vault/VaultMediaProbe.java"
    probe = ROOT / "native/player/MediaAdmissionProbe_android.cpp"
    thumb_android = ROOT / "native/engine/VaultThumbnailer_android.cpp"
    for path in (java, probe, thumb_android):
        if not path.exists():
            raise AssertionError(f"Android Vault media boundary file missing: {path}")

    java_text = java.read_text(encoding="utf-8")
    probe_text = probe.read_text(encoding="utf-8")
    thumb_text = thumb_android.read_text(encoding="utf-8")
    require(java_text, "MediaMetadataRetriever", "Android must use platform media decoding")
    require(java_text, "getFrameAtTime", "admission must require decoded-frame evidence")
    require(java_text, "Bitmap.CompressFormat.JPEG", "thumbnail bridge must persist a real frame")
    require(probe_text, "VaultMediaProbe", "native admission must call Android media bridge")
    require(thumb_text, "VaultMediaProbe", "native thumbnailer must call Android media bridge")
    forbid(probe_text, "mpv", "Android admission must not link libmpv")
    forbid(thumb_text, "MpvItem", "Android thumbnailer must not link MpvItem")
    require(ENRICH, "if (row.durationSec < 0.0)",
            "Android MediaStore duration must survive enrichment")
    print("android vault media boundary: PASS")


if __name__ == "__main__":
    main()
