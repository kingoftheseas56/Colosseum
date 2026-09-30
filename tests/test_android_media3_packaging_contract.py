from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "native" / "platform" / "android"
GRADLE = ANDROID / "build.gradle"
MANIFEST = ANDROID / "AndroidManifest.xml"
NETWORK_CONFIG = ANDROID / "res" / "xml" / "network_security_config.xml"
ANDROID_NS = "http://schemas.android.com/apk/res/android"

MEDIA3_COORDINATES = (
    "androidx.media3:media3-exoplayer:1.11.0",
    "androidx.media3:media3-exoplayer-hls:1.11.0",
    "androidx.media3:media3-exoplayer-dash:1.11.0",
)


class AndroidMedia3PackagingContract(unittest.TestCase):
    def test_first_phase_media3_dependencies_are_exact(self):
        self.assertTrue(GRADLE.is_file(), f"missing Qt package overlay: {GRADLE}")
        text = GRADLE.read_text(encoding="utf-8-sig")
        for coordinate in MEDIA3_COORDINATES:
            with self.subTest(coordinate=coordinate):
                self.assertEqual(text.count(coordinate), 1)

        self.assertNotIn("media3-session", text)
        self.assertNotIn("media3-ui", text)
        for forbidden in ("media3-cast", "media3-transformer", "media3-ffmpeg"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, text)

    def test_manifest_uses_narrow_network_security_config(self):
        self.assertTrue(MANIFEST.is_file(), f"missing Qt package overlay: {MANIFEST}")
        root = ET.parse(MANIFEST).getroot()
        application = root.find("application")
        self.assertIsNotNone(application, "Android manifest must contain <application>")
        network_attr = f"{{{ANDROID_NS}}}networkSecurityConfig"
        cleartext_attr = f"{{{ANDROID_NS}}}usesCleartextTraffic"
        self.assertEqual(application.get(network_attr), "@xml/network_security_config")
        self.assertNotEqual(application.get(cleartext_attr), "true")

    def test_cleartext_is_numeric_loopback_only(self):
        self.assertTrue(
            NETWORK_CONFIG.is_file(), f"missing network policy: {NETWORK_CONFIG}"
        )
        root = ET.parse(NETWORK_CONFIG).getroot()
        base_configs = root.findall("base-config")
        self.assertEqual(len(base_configs), 1)
        self.assertEqual(base_configs[0].get("cleartextTrafficPermitted"), "false")

        cleartext_configs = [
            node for node in root.iter() if node.get("cleartextTrafficPermitted") == "true"
        ]
        self.assertEqual(len(cleartext_configs), 1)
        self.assertEqual(cleartext_configs[0].tag, "domain-config")

        domains = root.findall(".//domain")
        self.assertEqual(len(domains), 1)
        self.assertEqual((domains[0].text or "").strip(), "127.0.0.1")
        self.assertEqual(domains[0].get("includeSubdomains"), "false")

        policy_text = NETWORK_CONFIG.read_text(encoding="utf-8-sig").lower()
        for forbidden in ("localhost", "10.0.0.0", "172.16.0.0", "192.168.0.0", "*"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, policy_text)


if __name__ == "__main__":
    unittest.main()
