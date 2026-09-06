"""Tests for qualification of the authenticated Stremio reference runtime."""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
import unittest
from pathlib import Path


ORACLE = os.environ.get("STREMIO_SERVER_JS")
HAS_ORACLE = bool(ORACLE and Path(ORACLE).is_file())
HAS_WSL = bool(shutil.which("wsl.exe"))


@unittest.skipUnless(HAS_ORACLE, "STREMIO_SERVER_JS must name the supplied oracle")
class ReferenceIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        from tools.server_lab.adapters.stremio import StremioReference

        self.reference = StremioReference(Path(ORACLE))

    def test_fingerprint_preserves_bundle_identity_and_runtime_metadata(self) -> None:
        identity = self.reference.fingerprint()

        self.assertEqual(identity["oracle"]["bytes"], 6_676_503)
        self.assertEqual(
            identity["oracle"]["sha256"],
            "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f",
        )
        self.assertEqual(identity["embedded"]["name"], "stremio-server")
        self.assertEqual(identity["embedded"]["version"], "4.21.0")
        self.assertEqual(identity["embedded"]["stremioRuntimeVersion"], "4.0")
        self.assertFalse(identity["embedded"]["stremioRuntimeVersionIsNodeSemver"])
        self.assertEqual(identity["runtime"]["kind"], "node")
        self.assertRegex(identity["runtime"]["version"], r"^v\d+\.\d+\.\d+$")
        self.assertIn(identity["runtime"]["intended_stremio_runtime"]["status"], {"missing", "present"})

    def test_reference_serves_heartbeat_and_settings_in_disposable_paths(self) -> None:
        result = self.reference.run_probe(("/heartbeat", "/settings"))

        if not result["ready"]:
            self.assertIn(result["failure"]["kind"], {"bind-denied", "port-exhausted"})
            self.assertFalse(result["teardown"]["alive_after"])
            self.skipTest("required loopback port policy prevents reference launch")

        self.assertEqual(result["port"], 11470)
        self.assertEqual(result["responses"]["/heartbeat"]["status"], 200)
        self.assertEqual(result["responses"]["/heartbeat"]["json"], {"success": True})
        self.assertEqual(result["responses"]["/settings"]["status"], 200)
        self.assertEqual(result["responses"]["/settings"]["json"]["baseUrl"], "http://127.0.0.1:11470")
        self.assertNotEqual(result["paths"]["app"], result["paths"]["settings"])
        self.assertEqual(result["oracle_after"], result["oracle_before"])
        self.assertEqual(result["teardown"]["alive_after"], False)

    def test_reference_reports_port_exhaustion_without_trying_port_11475(self) -> None:
        occupied = []
        try:
            for port in range(11470, 11475):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                try:
                    sock.bind(("127.0.0.1", port))
                except PermissionError:
                    sock.close()
                    self.skipTest("host policy denies the required loopback port fixture")
                sock.listen(1)
                occupied.append(sock)

            result = self.reference.run_probe(("/heartbeat",), timeout=2.5)
        finally:
            for sock in occupied:
                sock.close()

        self.assertFalse(result["ready"])
        self.assertEqual(result["port"], None)
        self.assertEqual(result["failure"]["kind"], "port-exhausted")
        self.assertIn("11474", result["failure"]["raw"])
        self.assertNotIn("11475", result["failure"]["attempted_ports"])
        self.assertFalse(result["teardown"]["alive_after"])

    def test_missing_companions_are_recorded_without_silent_repair(self) -> None:
        report = self.reference.companions(
            environment={"PATH": "", "FFMPEG_BIN": "", "FFPROBE_BIN": ""},
            search_roots=(),
        )

        self.assertEqual(report["ffmpeg"]["status"], "missing")
        self.assertEqual(report["ffprobe"]["status"], "missing")
        self.assertTrue(report["ffmpeg"]["candidates"])
        self.assertTrue(report["ffprobe"]["candidates"])
        self.assertEqual(report["repair"], "none")


@unittest.skipUnless(HAS_ORACLE and HAS_WSL, "oracle and WSL Ubuntu are required")
class WslReferenceQualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        from tools.server_lab.adapters.stremio import StremioReference

        available = subprocess.run(
            ["wsl.exe", "-d", "Ubuntu", "--", "true"],
            check=False,
            capture_output=True,
            text=True,
        )
        if available.returncode != 0:
            raise unittest.SkipTest("WSL Ubuntu is unavailable")
        cls.result = StremioReference(Path(ORACLE)).qualify_wsl("Ubuntu")

    def test_portable_node_is_official_verified_and_disposable(self) -> None:
        node = self.result["runtime"]

        self.assertEqual(node["version"], "v22.16.0")
        self.assertEqual(
            node["source_url"],
            "https://nodejs.org/dist/v22.16.0/node-v22.16.0-linux-x64.tar.xz",
        )
        self.assertEqual(
            node["archive_sha256"],
            "f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e",
        )
        self.assertTrue(node["official_checksum_match"])
        self.assertTrue(node["disposable_storage_removed"])
        self.assertEqual(node["kind"], "portable-node")

    def test_wsl_reference_serves_heartbeat_and_settings(self) -> None:
        case = self.result["cases"]["P02-01-WSL"]

        self.assertEqual(case["state"], "PASS")
        self.assertEqual(case["port"], 11470)
        self.assertEqual(case["responses"]["/heartbeat"]["status"], 200)
        self.assertEqual(case["responses"]["/heartbeat"]["json"], {"success": True})
        self.assertEqual(case["responses"]["/settings"]["status"], 200)
        self.assertRegex(
            case["responses"]["/settings"]["json"]["baseUrl"],
            r"^http://[^/:]+:11470$",
        )
        self.assertEqual(case["oracle_before"], case["oracle_after"])
        self.assertEqual(case["network_observation"]["unexpected_outbound"], [])
        self.assertFalse(case["teardown"]["alive_after"])

    def test_wsl_all_five_occupied_ports_are_exhausted_without_11475(self) -> None:
        case = self.result["cases"]["P02-02-WSL"]

        self.assertEqual(case["state"], "PASS")
        self.assertEqual(case["occupied_ports"], [11470, 11471, 11472, 11473, 11474])
        self.assertEqual(case["failure"]["kind"], "port-exhausted")
        self.assertEqual(case["failure"]["attempted_ports"], [11470, 11471, 11472, 11473, 11474])
        self.assertNotIn(11475, case["failure"]["attempted_ports"])
        self.assertFalse(case["teardown"]["alive_after"])

    def test_wsl_companions_are_hashed_but_stremio_runtime_remains_missing(self) -> None:
        companions = self.result["companions"]

        for name in ("ffmpeg", "ffprobe"):
            self.assertEqual(companions[name]["status"], "present")
            self.assertEqual(companions[name]["path"], f"/usr/bin/{name}")
            self.assertRegex(companions[name]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(companions["stremio-runtime"]["status"], "missing")
        self.assertEqual(companions["stremio-runtime"]["repair"], "none")


if __name__ == "__main__":
    unittest.main()
