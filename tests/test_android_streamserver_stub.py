"""Android's unavailable torrent server still implements the shared request API.

These source-contract checks prevent interface drift; the Android build remains
the actual compile/link check. They do not claim torrent support or runtime QA.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "native/player/streamserver.h").read_text()
ANDROID = (ROOT / "native/player/streamserver_android.cpp").read_text()


def method_body(name):
    match = re.search(r"void StreamServer::" + re.escape(name) + r"\([^)]*\)\s*\{(.*?)\n\}",
                      ANDROID, re.S)
    if not match:
        raise AssertionError(f"Missing Android StreamServer::{name} definition")
    return match[1]


class AndroidStreamServerStubTests(unittest.TestCase):
    def test_all_shared_virtual_request_methods_are_defined(self):
        methods = re.findall(r"virtual void (\w+)\(", HEADER)
        self.assertEqual({"requestManifest", "requestMagnetManifest", "requestFile", "cancelRequest"},
                         set(methods))
        for name in methods:
            with self.subTest(method=name):
                method_body(name)

    def test_requests_fail_through_token_scoped_error_without_success(self):
        self.assertIn("requestMagnetManifest(token, infoHash, {});", method_body("requestManifest"))
        for name in ("requestMagnetManifest", "requestFile"):
            body = method_body(name)
            self.assertIn("emit torrentRequestError(token, kUnavailableMessage);", body)
            self.assertNotIn("emit streamError", body)
        for forbidden in ("emit torrentManifestReady", "emit torrentStreamReady", "emit streamReady",
                          "ensureStarted(", "launchChild(", "new QNetworkAccessManager"):
            self.assertNotIn(forbidden, ANDROID)

    def test_cancellation_starts_no_work_and_constructor_stays_unavailable(self):
        self.assertIn("Q_UNUSED(token)", method_body("cancelRequest"))
        self.assertNotIn("emit ", method_body("cancelRequest"))
        self.assertIn("m_engineUnavailable = true;", ANDROID)
        self.assertRegex(ANDROID, r"StreamServer::StreamServer\(quint16 servicePort, QObject \*parent\)\s*"
                                 r": StreamServer\(parent\)")
        self.assertIn('"Colosseum Server is not available on Android yet"', ANDROID)


if __name__ == "__main__":
    unittest.main()
