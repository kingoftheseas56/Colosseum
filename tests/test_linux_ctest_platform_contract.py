from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
TEST_CMAKE = ROOT / "tests" / "CMakeLists.txt"


class LinuxCTestPlatformContract(unittest.TestCase):
    def test_powershell_ctests_are_labeled_windows(self):
        cmake = TEST_CMAKE.read_text(encoding="utf-8")
        blocks = re.findall(
            r"add_test\(NAME\s+([^\s\)]+)(.*?)\)\s*"
            r"set_tests_properties\(\1\s+PROPERTIES(.*?)\)",
            cmake,
            re.S,
        )
        offenders = []
        for name, body, props in blocks:
            if not re.search(r"COMMAND\s+powershell(?:\.exe)?\b", body, re.I):
                continue
            match = re.search(r'LABELS\s+"([^"]*)"', props)
            labels = match.group(1).lower().split(";") if match else []
            if "windows" not in labels:
                offenders.append(name)
        self.assertEqual([], offenders, f"PowerShell CTests missing windows label: {offenders}")


if __name__ == "__main__":
    unittest.main()
