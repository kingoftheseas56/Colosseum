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

    def test_headers_plus_unvalidated_junk_do_not_win_startup(self):
        probe = ByteProbe(ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5))
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.04, "kind": "body", "data": b"not-a-media-frame"},
        ], end_at=0.5)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(result["header_observed"])
        self.assertEqual(result["first_valid_byte"], 0.04)
        self.assertIsNone(result["first_valid_frame"])
        self.assertFalse(result["startup_success"])
        self.assertTrue(result["timeout"])

    def test_declared_end_before_deadline_is_not_reported_as_timeout(self):
        probe = ByteProbe(ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5))
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.1, "kind": "body", "data": b""},
        ], end_at=0.1)
        self.assertEqual(result["status"], "FAIL")
        self.assertFalse(result["timeout"])
        self.assertEqual(result["events"][-1], {"at": 0.1, "kind": "end", "reason": "stream_ended_without_frame"})

    def test_cancellation_remains_distinct_from_declared_end_timeout(self):
        probe = ByteProbe(ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5))
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.05, "kind": "cancel", "reason": "caller stopped"},
        ], end_at=0.5)
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(result["cancelled"])
        self.assertFalse(result["timeout"])

    def test_errors_remain_distinct_from_declared_end_timeout(self):
        def failing_frame_validator(_body):
            raise RuntimeError("validator failed")

        probe = ByteProbe(
            ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5),
            frame_validator=failing_frame_validator,
        )
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.04, "kind": "body", "data": b"media-frame"},
        ], end_at=0.5)
        self.assertEqual(result["status"], "ERROR")
        self.assertTrue(result["errors"])
        self.assertFalse(result["timeout"])

    def test_frame_at_deadline_does_not_win_startup(self):
        frame = b"media-frame"
        probe = ByteProbe(
            ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=0.5),
            frame_validator=lambda body: body == frame,
        )
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.5, "kind": "body", "data": frame},
        ])
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(result["timeout"])
        self.assertIsNone(result["first_valid_frame"])
        self.assertFalse(result["startup_success"])

    def test_first_valid_frame_and_cancellation_are_terminal(self):
        frame = b"media-frame"
        probe = ByteProbe(
            ObservationPolicy(header_deadline=0.1, first_byte_deadline=0.2, first_frame_deadline=1.0),
            frame_validator=lambda body: body == frame,
        )
        result = probe.observe([
            {"at": 0.01, "kind": "headers", "data": b"HTTP/1.1 200 OK\r\n\r\n"},
            {"at": 0.04, "kind": "body", "data": frame},
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
