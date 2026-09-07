"""P06-C deterministic fixture-corpus convergence and rejection tests."""

from __future__ import annotations

import hashlib
import json
import socket
import tempfile
import textwrap
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from tools.server_lab.fixtures.http_server import HttpOriginFixturePort
from tools.server_lab.fixtures.peer_server import PeerAction, ScriptedPeerServer
from tools.server_lab.fixtures.torrents import (
    PIECE_LENGTH,
    build_multi_file_torrent,
    build_single_file_torrent,
    validate_torrent_fixture,
)
from tools.server_lab.fixtures.tracker_server import TrackerFixturePort
from tools.server_lab.evidence import EvidenceSchema
from tools.server_lab.lab import LabRunner


ROOT = Path(__file__).resolve().parents[3]
CASE_PATH = ROOT / "tools" / "server_lab" / "cases" / "P06-C.json"
PROFILE_NAMES = (
    "payload",
    "discovery",
    "corruption",
    "choke",
    "delay",
    "disconnect",
    "cancellation",
    "unauthorized_delivery",
)
FIXTURE_SEED = "P06-C-literal-corpus-v1"


class TorrentFixturePort:
    """Test-owned port over the public P06-A torrent fixture builders."""

    @staticmethod
    def single():
        return build_single_file_torrent()

    @staticmethod
    def multi():
        return build_multi_file_torrent()


class PeerFixturePort(ScriptedPeerServer):
    """Test-owned name for the public P06-A scripted peer port."""


def fixture_profiles() -> tuple[str, ...]:
    return PROFILE_NAMES


def _case() -> dict:
    return json.loads(CASE_PATH.read_text(encoding="utf-8"))


def _get(url: str) -> tuple[int, bytes, dict[str, str]]:
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return response.status, response.read(), dict(response.headers)
    except urllib.error.HTTPError as error:
        return error.code, error.read(), dict(error.headers)


def _recv_exact(client: socket.socket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        chunk = client.recv(length - len(result))
        if not chunk:
            raise ConnectionError("peer closed before the expected wire bytes")
        result.extend(chunk)
    return bytes(result)


def _recv_message(client: socket.socket) -> tuple[int, bytes]:
    length = int.from_bytes(_recv_exact(client, 4), "big")
    payload = _recv_exact(client, length)
    return payload[0], payload[1:]


def _send_message(client: socket.socket, message_id: int, payload: bytes = b"") -> None:
    message = bytes([message_id]) + payload
    client.sendall(len(message).to_bytes(4, "big") + message)


def _block_request(piece: int, begin: int, length: int) -> bytes:
    return bytes([6]) + piece.to_bytes(4, "big") + begin.to_bytes(4, "big") + length.to_bytes(4, "big")


def _stop_cleanly(fixture) -> None:
    port = fixture.port
    fixture.stop()
    assert fixture.port == 0
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.5)
        assert probe.connect_ex(("127.0.0.1", port)) != 0


class P06CFixtureCorpusTests(unittest.TestCase):
    def test_corpus_declares_every_required_fault_profile(self) -> None:
        self.assertEqual(fixture_profiles(), PROFILE_NAMES)
        self.assertEqual(_case()["case"], "P06-03")
        self.assertEqual(tuple(_case()["profiles"]), PROFILE_NAMES)

    def test_payload_identity_and_geometry_are_converged(self) -> None:
        case = _case()["fixture_goldens"]
        single = TorrentFixturePort.single()
        multi = TorrentFixturePort.multi()
        self.assertTrue(validate_torrent_fixture(single))
        self.assertTrue(validate_torrent_fixture(multi))
        self.assertEqual(hashlib.sha256(single.metainfo).hexdigest(), case["single"]["metainfo_sha256"])
        self.assertEqual(single.infohash, case["single"]["infohash_sha1"])
        self.assertEqual(
            hashlib.sha256(single.payloads["single.bin"]).hexdigest(),
            case["single"]["payload_sha256"]["single.bin"],
        )
        self.assertEqual(
            [hashlib.sha1(piece).hexdigest() for piece in single.pieces],
            case["single"]["piece_sha1"],
        )
        self.assertEqual(hashlib.sha256(multi.metainfo).hexdigest(), case["multi"]["metainfo_sha256"])
        self.assertEqual(multi.infohash, case["multi"]["infohash_sha1"])
        self.assertEqual(
            {name: hashlib.sha256(payload).hexdigest() for name, payload in multi.payloads.items()},
            case["multi"]["payload_sha256"],
        )
        self.assertEqual(
            [hashlib.sha1(piece).hexdigest() for piece in multi.pieces],
            case["multi"]["piece_sha1"],
        )
        self.assertEqual(single.total_length, 22528)
        self.assertEqual(multi.total_length, 33000)
        self.assertEqual([len(piece) for piece in multi.pieces], [16384, 16384, 232])
        self.assertEqual([file.offset for file in multi.files], [0, 7000, 21000])

    def test_discovery_profile_is_exact_and_deterministic(self) -> None:
        compact_peer = bytes((127, 0, 0, 1)) + (49001).to_bytes(2, "big")
        expected = b"d8:intervali30e5:peers6:" + compact_peer + b"e"
        tracker = TrackerFixturePort({"peers": [["127.0.0.1", 49001]]}).start()
        try:
            status, body, _ = _get(tracker.url("/announce?generation=1&token=discovery"))
            self.assertEqual((status, body), (200, expected))
            self.assertEqual(
                [event["operation"] for event in tracker.events],
                ["ready", "request", "response"],
            )
        finally:
            _stop_cleanly(tracker)

        duplicate = TrackerFixturePort({"peers": [["127.0.0.1", 49001]], "duplicate_peers": True}).start()
        try:
            self.assertIn(b"5:peers12:" + compact_peer + compact_peer + b"e", _get(duplicate.url())[1])
        finally:
            _stop_cleanly(duplicate)

    def test_corruption_profile_is_visible_after_an_authorized_request(self) -> None:
        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(
            fixture,
            [PeerAction.handshake(), PeerAction.unchoke(), PeerAction.corrupt(0, 0, PIECE_LENGTH), PeerAction.disconnect()],
        )
        server.start()
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                self.assertEqual(_recv_exact(client, 68)[:20], b"\x13BitTorrent protocol")
                _send_message(client, 2)
                self.assertEqual(_recv_message(client)[0], 1)
                request = _block_request(0, 0, PIECE_LENGTH)
                client.sendall(len(request).to_bytes(4, "big") + request)
                message_id, payload = _recv_message(client)
                self.assertEqual(message_id, 7)
                self.assertEqual(payload[0:8], b"\x00\x00\x00\x00\x00\x00\x00\x00")
                self.assertNotEqual(payload[8:], fixture.pieces[0][:PIECE_LENGTH])
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)
        delivery = next(entry for entry in server.ledger if entry["kind"] == "corrupt")
        self.assertTrue(delivery["authorized"])

    def test_choke_profile_preserves_wire_order(self) -> None:
        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(fixture, [PeerAction.handshake(), PeerAction.bitfield(fixture.bitfield_bytes), PeerAction.choke(), PeerAction.unchoke(), PeerAction.disconnect()])
        server.start()
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                self.assertEqual(_recv_exact(client, 68)[:20], b"\x13BitTorrent protocol")
                bitfield_id, bitfield = _recv_message(client)
                choke_id, _ = _recv_message(client)
                unchoke_id, _ = _recv_message(client)
                self.assertEqual((bitfield_id, bitfield), (5, fixture.bitfield_bytes))
                self.assertEqual((choke_id, unchoke_id), (0, 1))
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)
        self.assertEqual([entry["kind"] for entry in server.ledger], ["handshake", "bitfield", "choke", "unchoke", "disconnect"])

    def test_delay_profile_is_observable_on_tracker_and_peer_ports(self) -> None:
        tracker = TrackerFixturePort({"delay_ms": 40}).start()
        try:
            started = time.monotonic()
            self.assertEqual(_get(tracker.url())[0], 200)
            self.assertGreaterEqual(time.monotonic() - started, 0.03)
            self.assertIn({"operation": "delay", "delay_ms": 40}, [{key: event.get(key) for key in ("operation", "delay_ms")} for event in tracker.events])
        finally:
            _stop_cleanly(tracker)

        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(fixture, [PeerAction.handshake(), PeerAction.delay(0.04), PeerAction.unchoke(), PeerAction.disconnect()])
        server.start()
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                _recv_exact(client, 68)
                started = time.monotonic()
                message_id, _ = _recv_message(client)
                self.assertEqual(message_id, 1)
                self.assertGreaterEqual(time.monotonic() - started, 0.03)
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)

    def test_disconnect_profile_is_a_clean_eof(self) -> None:
        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(fixture, [PeerAction.handshake(), PeerAction.disconnect()])
        server.start()
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                self.assertEqual(_recv_exact(client, 68)[:20], b"\x13BitTorrent protocol")
                self.assertEqual(client.recv(1), b"")
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)
        self.assertEqual([entry["kind"] for entry in server.ledger], ["handshake", "disconnect"])

    def test_cancellation_profile_rejects_cancelled_tracker_origin_and_peer_work(self) -> None:
        cancellation = {"generation": 7, "token": "p06-c-cancel"}
        tracker = TrackerFixturePort({"cancellation": cancellation}).start()
        origin = HttpOriginFixturePort({"metadata": b"metadata", "cancellation": cancellation}).start()
        try:
            tracker.cancel(7, cancellation["token"])
            self.assertEqual(_get(tracker.url("/announce?generation=7&token=p06-c-cancel"))[0], 499)
            origin.cancel(7, cancellation["token"])
            self.assertEqual(_get(origin.url("/metadata?generation=7&token=p06-c-cancel"))[0], 499)
            self.assertTrue(any(event["operation"] == "request_after_cancellation" for event in tracker.events))
            self.assertTrue(any(event["operation"] == "request_after_cancellation" for event in origin.events))
        finally:
            _stop_cleanly(tracker)
            _stop_cleanly(origin)

        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(fixture, [PeerAction.handshake(), PeerAction.unchoke(), PeerAction.deliver(0, 0, PIECE_LENGTH), PeerAction.disconnect()])
        server.start()
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                _recv_exact(client, 68)
                _send_message(client, 2)
                self.assertEqual(_recv_message(client)[0], 1)
                request = _block_request(0, 0, PIECE_LENGTH)
                cancel = bytes([8]) + (0).to_bytes(4, "big") + (0).to_bytes(4, "big") + PIECE_LENGTH.to_bytes(4, "big")
                client.sendall(len(request).to_bytes(4, "big") + request + len(cancel).to_bytes(4, "big") + cancel)
                client.shutdown(socket.SHUT_WR)
                self.assertEqual(client.recv(4), b"")
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)
        delivery = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertFalse(delivery["authorized"])

    def test_unauthorized_delivery_is_rejected_beyond_ledger_presence(self) -> None:
        fixture = TorrentFixturePort.single()
        server = PeerFixturePort(fixture, [PeerAction.handshake(), PeerAction.deliver(0, 0, PIECE_LENGTH), PeerAction.disconnect()])
        server.start()
        wire_prefix = b""
        try:
            with socket.create_connection(server.address, timeout=2) as client:
                client.sendall(server.handshake_bytes())
                self.assertEqual(_recv_exact(client, 68)[:20], b"\x13BitTorrent protocol")
                _send_message(client, 2)
                unowned_request = _block_request(0, 0, 1)
                client.sendall(len(unowned_request).to_bytes(4, "big") + unowned_request)
                client.shutdown(socket.SHUT_WR)
                wire_prefix = client.recv(4)
        finally:
            server.join(timeout=3)
        self.assertIsNone(server.error)
        kinds = [entry["kind"] for entry in server.ledger]
        self.assertEqual(kinds, ["handshake", "interested", "request", "deliver", "disconnect"])
        request = next(entry for entry in server.ledger if entry["kind"] == "request")
        delivery = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertEqual((request["piece"], request["begin"], request["length"]), (0, 0, 1))
        self.assertFalse(delivery["authorized"], "ledger presence alone is not rejection")
        self.assertEqual(wire_prefix, b"", "unowned delivery must not reach the wire")

    def test_lab_runner_and_evidence_schema_record_a_replayable_receipt(self) -> None:
        subject_code = textwrap.dedent(
            """
            import json, os
            from pathlib import Path
            root = Path(os.environ["LAB_RUN_ROOT"])
            response = {"status": 200, "headers": {"Content-Type": "application/json"}, "body": "expected"}
            (root / "subject-response.json").write_text(json.dumps(response), encoding="utf-8")
            (root / "protocol-response.txt").write_bytes(
                "HTTP/1.1 200 OK\\r\\nContent-Type: application/json\\r\\n\\r\\nexpected".encode("latin-1")
            )
            """
        )
        with tempfile.TemporaryDirectory(prefix="p06-c-lab-") as temporary:
            temp_root = Path(temporary)
            subject = temp_root / "fixture_subject.py"
            subject.write_text(subject_code, encoding="utf-8")
            receipt = LabRunner().run(
                subject=subject,
                mode="pass",
                data_root=temp_root / "data",
                evidence_dir=temp_root / "evidence",
                run_id="p06-c-replay",
            )
            schema = json.loads((ROOT / "tools" / "server_lab" / "schemas" / "run.schema.json").read_text(encoding="utf-8"))
            EvidenceSchema.validate(receipt, schema)
            self.assertEqual(receipt["result"], "PASS")
            self.assertEqual(receipt["source"]["sha256"], hashlib.sha256(subject.read_bytes()).hexdigest())
            self.assertEqual(receipt["replay"]["required_substitutions"], ["python", "subject", "data_root", "evidence_dir", "run_id"])


if __name__ == "__main__":
    unittest.main()
