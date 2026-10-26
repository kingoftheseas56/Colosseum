import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("linux_assemble", ROOT / "tools/linux-release/assemble.py")
assembly = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assembly)
SMOKE_SPEC = importlib.util.spec_from_file_location("linux_smoke", ROOT / "tools/linux-release/smoke_installed.py")
smoke = importlib.util.module_from_spec(SMOKE_SPEC)
SMOKE_SPEC.loader.exec_module(smoke)


class LinuxReleasePolicyTests(unittest.TestCase):
    def test_positioning_keeps_its_matching_serialport_sdk_dependency(self):
        workflow = (ROOT / ".github/workflows/release-linux-deb.yml").read_text()
        modules = next(line for line in workflow.splitlines() if "modules:" in line)
        self.assertIn("qtpositioning", modules.split())
        self.assertIn("qtserialport", modules.split())
        self.assertNotIn("--exclude-plugin position/", workflow)

    def test_package_declares_the_system_ca_trust_dependency(self):
        packaging = (ROOT / "tools/linux-release/package_deb.py").read_text()
        # The Depends statement is a multi-line concatenated literal in the
        # source; join its fragments before reading the declared entries.
        literal = re.search(r"'Depends:(.*?)\\n'", packaging, re.S).group(1)
        depends = re.sub(r"'\s*'", "", literal)
        self.assertIn("ca-certificates", [entry.strip() for entry in depends.split(",")])

    def test_clean_install_proves_the_trust_dependency(self):
        workflow = (ROOT / ".github/workflows/release-linux-deb.yml").read_text()
        install = next(line for line in workflow.splitlines() if re.search(r"\./Colosseum-\d+\.\d+\.\d+-linux-amd64\.deb", line))
        # A recommendation or a preinstalled store would mask a missing Depends
        # entry, so the clean container must install with recommendations off.
        self.assertIn("--no-install-recommends", install)
        self.assertIn("ca-certificates-before.txt", workflow)
        self.assertIn("ca-certificates-after.txt", workflow)
        self.assertIn("'ii '", workflow)

    def test_installed_smoke_requires_online_tls_content(self):
        source = (ROOT / "tools/linux-release/smoke_installed.py").read_text()
        self.assertIn("SSL handshake failed", source)
        self.assertIn("'outcome') != 'prepared'", source)
        self.assertIn("networkBytes", source)
        self.assertIn("homeUniverseBannerArt", source)
        self.assertIn("readySource", source)

    def test_catalogue_log_parsing_reads_real_records(self):
        broken_bundle = ('colosseum.catalogue.static: {"bucketAdmissions":0,"dataset":"content",'
                         '"method":"bundle","networkBytes":0,"networkFiles":1,"outcome":"failure"}\n'
                         'file:///opt/colosseum/qml/Main.qml:3906:33: QML QQuickImage*: SSL handshake failed: '
                         'The issuer certificate of a locally looked up certificate could not be found\n')
        records = smoke.parse_catalogue_records(broken_bundle)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["dataset"], "content")
        self.assertEqual(smoke.content_bundle_records(records)[0]["outcome"], "failure")
        self.assertEqual(smoke.count_ssl_handshake_failures(broken_bundle), 1)
        prepared = ('colosseum.catalogue.static: {"dataset":"content","method":"bundle",'
                    '"networkBytes":4096,"networkFiles":2,"outcome":"prepared"}\n')
        records = smoke.parse_catalogue_records(prepared)
        bundle = smoke.content_bundle_records(records)[-1]
        self.assertEqual(bundle["outcome"], "prepared")
        self.assertEqual(smoke.count_ssl_handshake_failures(prepared), 0)
        self.assertEqual(smoke.parse_catalogue_records("not a catalogue line\n"), [])

    def test_version_comes_from_the_compiled_project(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "native").mkdir()
            (source / "native/CMakeLists.txt").write_text("project(colosseum VERSION 1.1.9 LANGUAGES C CXX)\n")
            self.assertEqual(assembly.release_version(source), "1.1.9")

    def test_shipping_configuration_rejects_fixtures_and_wrong_catalogue_origin(self):
        cache = ("CMAKE_BUILD_TYPE:STRING=Release\n"
                 "COLOSSEUM_NO_ACCOUNT_EDITION:BOOL=OFF\n"
                 "COLOSSEUM_VINYL_IN_APP:BOOL=OFF\n"
                 "COLOSSEUM_CONTENT_API_URL:STRING=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev\n")
        assembly.validate_shipping_cache(cache, "1.1.10")
        for flag in ("COLOSSEUM_UPDATE_TESTING", "COLOSSEUM_ENABLE_VINYL_QUALIFICATION",
                     "COLOSSEUM_ENABLE_VINYL_TEST_FIXTURES", "COLOSSEUM_NO_ACCOUNT_EDITION"):
            altered = cache.replace(flag + ":BOOL=OFF", flag + ":BOOL=ON")
            if altered == cache:
                altered += flag + ":BOOL=ON\n"
            with self.assertRaises(ValueError):
                assembly.validate_shipping_cache(altered)
        with self.assertRaises(ValueError):
            assembly.validate_shipping_cache(cache.replace("https://colosseum-content-api.", "https://staging."))
        with self.assertRaisesRegex(ValueError, "Vinyl must be explicitly disabled"):
            assembly.validate_shipping_cache(cache.replace("COLOSSEUM_VINYL_IN_APP:BOOL=OFF",
                                                             "COLOSSEUM_VINYL_IN_APP:BOOL=ON"), "1.1.10")
        with self.assertRaisesRegex(ValueError, "Vinyl must be explicitly disabled"):
            assembly.validate_shipping_cache(cache.replace("COLOSSEUM_VINYL_IN_APP:BOOL=OFF\n", ""), "1.1.10")
        # The shipping-only gate must not silently remove Vinyl from older package policies.
        assembly.validate_shipping_cache(cache.replace("COLOSSEUM_VINYL_IN_APP:BOOL=OFF",
                                                        "COLOSSEUM_VINYL_IN_APP:BOOL=ON"), "1.1.9")


if __name__ == "__main__":
    unittest.main()
