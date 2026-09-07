from __future__ import annotations

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]
SERVER = ROOT / "native" / "colosseum_server_v1"


class P03SkeletonContractTests(unittest.TestCase):
    def test_required_candidate_shape_exists(self):
        required = [
            SERVER / "CMakeLists.txt",
            SERVER / "CMakePresets.json",
            SERVER / "include" / "server1" / "Runtime.h",
            SERVER / "src" / "Runtime.cpp",
            SERVER / "host" / "main.cpp",
            SERVER / "tests" / "CMakeLists.txt",
            SERVER / "tests" / "runtime_lifecycle.cpp",
        ]
        missing = [str(p.relative_to(ROOT)) for p in required if not p.is_file()]
        self.assertEqual([], missing, f"P03 candidate missing: {missing}")

    def test_build_contract_is_one_library_one_host_and_one_libtorrent_authority(self):
        cmake_path = SERVER / "CMakeLists.txt"
        if not cmake_path.is_file():
            self.skipTest("candidate absent; required-shape test owns the RED failure")
        text = cmake_path.read_text(encoding="utf-8")
        self.assertRegex(text, r"add_library\s*\(\s*colosseum_server_v1\b")
        self.assertRegex(text, r"add_executable\s*\(\s*colosseum_server_v1_host\b")
        self.assertIn("colosseum_libtorrent", text)
        self.assertNotIn("libtorrent-2.1", text.lower())
        self.assertNotRegex(text, r"add_(library|executable)\s*\([^\n]*(server[_ -]?0[._-]?1|old_server)")

    def test_runtime_contract_cannot_claim_capabilities_or_streaming_readiness(self):
        header = SERVER / "include" / "server1" / "Runtime.h"
        source = SERVER / "src" / "Runtime.cpp"
        if not header.is_file() or not source.is_file():
            self.skipTest("candidate absent; required-shape test owns the RED failure")
        text = header.read_text(encoding="utf-8") + "\n" + source.read_text(encoding="utf-8")
        self.assertIn("streamingReady", text)
        self.assertIn("capabilities", text)
        self.assertNotRegex(text, r"streamingReady\s*\([^)]*\)[^{;]*\{[^}]*return\s+true", "empty P03 shell must never claim streaming readiness")
        for forbidden in ("/stream", "/settings", "/heartbeat", "hlsv2", "torrent/open"):
            self.assertNotIn(forbidden, text)

    def test_presets_are_path_portable(self):
        preset = SERVER / "CMakePresets.json"
        if not preset.is_file():
            self.skipTest("candidate absent; required-shape test owns the RED failure")
        data = json.loads(preset.read_text(encoding="utf-8"))
        text = json.dumps(data)
        self.assertNotRegex(text, re.compile(r"[A-Za-z]:[/\\]"))
        self.assertNotIn("/home/", text)
        self.assertNotIn("C:/tools", text)


if __name__ == "__main__":
    unittest.main()
