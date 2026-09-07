import unittest

from tools.server_lab.probes.http_bytes import ByteProbe, ObservationPolicy
from tools.server_lab.probes.resources import ResourceProbe


class P07AProbeTests(unittest.TestCase):
    def test_headers_without_frame_do_not_win_startup(self):
        probe = ByteProbe(ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5))
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
        ], end_at=0.5)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(result["header_observed"])
        self.assertFalse(result["startup_success"])
        self.assertIsNone(result["first_valid_frame"])

    def test_first_valid_frame_and_cancellation_are_terminal(self):
        probe = ByteProbe(ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=1.0))
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.04, "kind": "body", "data": b"frame"},
            {"at": 0.05, "kind": "cancel", "reason": "caller stopped"},
        ])
        self.assertTrue(result["startup_success"])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["first_valid_byte"], 0.04)
        self.assertEqual(result["first_valid_frame"], 0.04)

    def test_resource_samples_keep_raw_values_and_deltas(self):
        probe = ResourceProbe()
        result = probe.observe([
            {"at": 0.0, "rss_bytes": 10, "cpu_seconds": 1.0},
            {"at": 0.25, "rss_bytes": 14, "cpu_seconds": 1.2},
        ])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["samples"][1]["delta"]["rss_bytes"], 4)
        self.assertEqual(result["samples"][1]["delta"]["cpu_seconds"], 0.2)


if __name__ == "__main__":
    unittest.main()
