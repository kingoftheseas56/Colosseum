"""Check the unavailable Android StreamServer facade's source contract."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "native/player/streamserver.h").read_text(encoding="utf-8")
ANDROID = (ROOT / "native/player/streamserver_android.cpp").read_text(encoding="utf-8")
STUB = ANDROID.split("#else // stub: Android streaming unavailable", 1)[1]


def method_body(name):
    match = re.search(r"StreamServer::" + re.escape(name) + r"\([^)]*\)(?: const)?\s*\{(.*?)\}",
                      STUB, re.S)
    if not match:
        raise AssertionError(f"Missing Android StreamServer::{name} definition")
    return match[1]


class AndroidStreamServerStubTests(unittest.TestCase):
    def test_token_scoped_torrent_api_stays_desktop_only(self):
        guard = r"#if !defined\(Q_OS_ANDROID\) && !defined\(COLOSSEUM_ANDROID_NUVIO_BACKEND\)"
        block = re.search(guard + r"\n(?=[^#]*virtual void requestManifest)(.*?)#endif", HEADER, re.S)
        self.assertIsNotNone(block)
        self.assertEqual({"requestManifest", "requestMagnetManifest", "requestFile", "cancelRequest"},
                         set(re.findall(r"virtual void (\w+)\(", block[1])))
        for name in re.findall(r"virtual void (\w+)\(", block[1]):
            self.assertNotIn(f"StreamServer::{name}(", ANDROID)

    def test_unavailable_requests_signal_failure_without_success(self):
        for name in ("play", "prefetch", "warmUp"):
            self.assertIn("emit streamError(kUnavailableMessage);", method_body(name))
        self.assertIn("prefetch(infoHash, fileIdx);", method_body("warmSource"))
        for forbidden in ("emit torrentManifestReady", "emit torrentStreamReady", "emit streamReady",
                          "emit fetchReady", "ensureStarted(", "launchChild("):
            self.assertNotIn(forbidden, STUB)

    def test_unavailable_server_neither_starts_nor_returns_a_stream(self):
        self.assertIn("m_engineUnavailable = true;", STUB)
        self.assertIn("return false;", method_body("ready"))
        self.assertIn("return false;", method_body("starting"))
        self.assertRegex(STUB, r"QString StreamServer::streamUrl\([^)]*\) const\s*\{[^}]*return \{\};")
        self.assertIn('"Colosseum Server is not available on Android yet"', STUB)


if __name__ == "__main__":
    unittest.main()
