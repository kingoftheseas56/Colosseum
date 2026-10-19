import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("linux_assemble", ROOT / "tools/linux-release/assemble.py")
assembly = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assembly)


class LinuxReleasePolicyTests(unittest.TestCase):
    def test_positioning_keeps_its_matching_serialport_sdk_dependency(self):
        workflow = (ROOT / ".github/workflows/release-linux-deb.yml").read_text()
        modules = next(line for line in workflow.splitlines() if "modules:" in line)
        self.assertIn("qtpositioning", modules.split())
        self.assertIn("qtserialport", modules.split())
        self.assertNotIn("--exclude-plugin position/", workflow)

    def test_version_comes_from_the_compiled_project(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "native").mkdir()
            (source / "native/CMakeLists.txt").write_text("project(colosseum VERSION 1.1.9 LANGUAGES C CXX)\n")
            self.assertEqual(assembly.release_version(source), "1.1.9")

    def test_shipping_configuration_rejects_fixtures_and_wrong_catalogue_origin(self):
        cache = ("CMAKE_BUILD_TYPE:STRING=Release\n"
                 "COLOSSEUM_NO_ACCOUNT_EDITION:BOOL=OFF\n"
                 "COLOSSEUM_CONTENT_API_URL:STRING=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev\n")
        assembly.validate_shipping_cache(cache)
        for flag in ("COLOSSEUM_UPDATE_TESTING", "COLOSSEUM_ENABLE_VINYL_QUALIFICATION",
                     "COLOSSEUM_ENABLE_VINYL_TEST_FIXTURES", "COLOSSEUM_NO_ACCOUNT_EDITION"):
            altered = cache.replace(flag + ":BOOL=OFF", flag + ":BOOL=ON")
            if altered == cache:
                altered += flag + ":BOOL=ON\n"
            with self.assertRaises(ValueError):
                assembly.validate_shipping_cache(altered)
        with self.assertRaises(ValueError):
            assembly.validate_shipping_cache(cache.replace("https://colosseum-content-api.", "https://staging."))


if __name__ == "__main__":
    unittest.main()
