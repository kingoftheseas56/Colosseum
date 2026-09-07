import hashlib
import json
from pathlib import Path
import socket
import unittest

from tools.server_lab.fixtures.peer_server import PeerAction, ScriptedPeerServer
from tools.server_lab.fixtures.torrents import (
    PIECE_LENGTH,
    build_multi_file_torrent,
    build_single_file_torrent,
    decode_bencode,
    validate_torrent_fixture,
)


class P06AFixtureTests(unittest.TestCase):
    def test_fixture_goldens_match_packet_case(self):
        repo_root = Path(__file__).resolve().parents[4]
        case = json.loads((repo_root / "tools" / "server_lab" / "cases" / "P06-A.json").read_text())
        single = build_single_file_torrent()
        multi = build_multi_file_torrent()
        self.assertEqual(hashlib.sha256(single.metainfo).hexdigest(), case["fixture_goldens"]["single"]["metainfo_sha256"])
        self.assertEqual(single.infohash, case["fixture_goldens"]["single"]["infohash_sha1"])
        self.assertEqual(hashlib.sha256(multi.metainfo).hexdigest(), case["fixture_goldens"]["multi"]["metainfo_sha256"])
        self.assertEqual(multi.infohash, case["fixture_goldens"]["multi"]["infohash_sha1"])

    def test_single_file_is_canonical_and_independently_verifiable(self):
        fixture = build_single_file_torrent()
        self.assertEqual(fixture.payloads["single.bin"], b"P06-single\n" * 2048)
        self.assertEqual(fixture.infohash, hashlib.sha1(fixture.info_bytes).hexdigest())
        self.assertEqual(fixture.metainfo, fixture.metainfo_again())
        self.assertTrue(validate_torrent_fixture(fixture))

        decoded = decode_bencode(fixture.metainfo)
        self.assertEqual(decoded[b"info"][b"piece length"], PIECE_LENGTH)
        self.assertEqual(decoded[b"info"][b"length"], len(fixture.payloads["single.bin"]))

    def test_multi_file_geometry_carries_piece_across_file_offsets(self):
        fixture = build_multi_file_torrent()
        self.assertTrue(validate_torrent_fixture(fixture))
        self.assertEqual([entry.offset for entry in fixture.files], [0, 7000, 21000])
        self.assertEqual(fixture.total_length, 33000)
        self.assertEqual([len(piece) for piece in fixture.pieces], [16384, 16384, 232])
        self.assertEqual(fixture.block_size(2, 0), 232)

    def test_bitfield_masks_unused_trailing_bits(self):
        self.assertEqual(build_single_file_torrent().bitfield_bytes, b"\xc0")
        self.assertEqual(build_multi_file_torrent().bitfield_bytes, b"\xe0")

    def test_scripted_peer_records_handshake_states_and_authorized_blocks(self):
        fixture = build_single_file_torrent()
        actions = [
            PeerAction.handshake(),
            PeerAction.bitfield(fixture.bitfield_bytes),
            PeerAction.unchoke(),
            PeerAction.delay(0.001),
            PeerAction.deliver(piece=0, begin=0, length=16384),
            PeerAction.disconnect(),
        ]
        server = ScriptedPeerServer(fixture, actions)
        server.start()
        with socket.create_connection(server.address, timeout=2) as client:
            client.sendall(server.handshake_bytes())
            self.assertEqual(client.recv(68)[:20], b"\x13BitTorrent protocol")
            client.recv(5 + len(fixture.bitfield_bytes))
            client.recv(5)
            client.sendall((1).to_bytes(4, "big") + b"\x02")
            request = b"\x06" + (0).to_bytes(4, "big") + (0).to_bytes(4, "big") + (16384).to_bytes(4, "big")
            client.sendall(len(request).to_bytes(4, "big") + request)
            client.recv(4 + 8 + 16384)
        server.join(timeout=2)
        self.assertEqual(server.error, None)
        self.assertEqual([entry["kind"] for entry in server.ledger], ["handshake", "bitfield", "unchoke", "delay", "interested", "request", "deliver", "disconnect"])
        delivered = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertEqual(delivered["piece"], 0)
        self.assertEqual(delivered["length"], 16384)
        self.assertTrue(delivered["authorized"])
        self.assertEqual(delivered["order"], 6)
        self.assertTrue(all(entry["loopback"] for entry in server.ledger))

    def test_cancelled_request_is_not_delivered_as_success(self):
        fixture = build_single_file_torrent()
        server = ScriptedPeerServer(fixture, [PeerAction.handshake(), PeerAction.unchoke(), PeerAction.deliver(0, 0, 16384), PeerAction.disconnect()])
        server.start()
        with socket.create_connection(server.address, timeout=2) as client:
            client.sendall(server.handshake_bytes())
            client.recv(68)
            client.recv(5)
            client.sendall((1).to_bytes(4, "big") + b"\x02")
            request = b"\x06" + (0).to_bytes(4, "big") + (0).to_bytes(4, "big") + (16384).to_bytes(4, "big")
            cancel = b"\x08" + (0).to_bytes(4, "big") + (0).to_bytes(4, "big") + (16384).to_bytes(4, "big")
            client.sendall(len(request).to_bytes(4, "big") + request)
            client.sendall(len(cancel).to_bytes(4, "big") + cancel)
            client.settimeout(0.25)
            self.assertEqual(client.recv(4), b"")
        server.join(timeout=2)
        self.assertIsNone(server.error)
        self.assertEqual([entry["kind"] for entry in server.ledger], ["handshake", "unchoke", "interested", "request", "cancel", "deliver", "disconnect"])
        delivery = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertFalse(delivery["authorized"])

    def test_final_short_block_requires_exact_wire_request(self):
        fixture = build_multi_file_torrent()
        server = ScriptedPeerServer(fixture, [PeerAction.handshake(), PeerAction.unchoke(), PeerAction.deliver(2, 0, 232), PeerAction.disconnect()])
        server.start()
        with socket.create_connection(server.address, timeout=2) as client:
            client.sendall(server.handshake_bytes())
            client.recv(68)
            client.recv(5)
            request = b"\x06" + (2).to_bytes(4, "big") + (0).to_bytes(4, "big") + (232).to_bytes(4, "big")
            client.sendall((1).to_bytes(4, "big") + b"\x02")
            client.sendall(len(request).to_bytes(4, "big") + request)
            payload = client.recv(4)
            self.assertEqual(int.from_bytes(payload, "big"), 241)
            block = client.recv(241)
            self.assertEqual(block[0], 7)
            self.assertEqual(int.from_bytes(block[1:5], "big"), 2)
            self.assertEqual(int.from_bytes(block[5:9], "big"), 0)
            self.assertEqual(len(block[9:]), 232)
        server.join(timeout=2)
        self.assertIsNone(server.error)
        request_entry = next(entry for entry in server.ledger if entry["kind"] == "request")
        delivery = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertEqual((request_entry["piece"], request_entry["begin"], request_entry["length"]), (2, 0, 232))
        self.assertEqual((delivery["piece"], delivery["begin"], delivery["length"]), (2, 0, 232))
        self.assertTrue(delivery["authorized"])

    def test_unrequested_delivery_records_unauthorized_without_wire_success(self):
        fixture = build_single_file_torrent()
        server = ScriptedPeerServer(fixture, [PeerAction.handshake(), PeerAction.deliver(0, 0, 16384), PeerAction.disconnect()])
        server.start()
        with socket.create_connection(server.address, timeout=2) as client:
            client.sendall(server.handshake_bytes())
            client.recv(68)
            client.sendall((1).to_bytes(4, "big") + b"\x02")
            client.settimeout(0.25)
            with self.assertRaises(socket.timeout):
                client.recv(4)
        server.join(timeout=2)
        self.assertIsNone(server.error)
        delivery = next(entry for entry in server.ledger if entry["kind"] == "deliver")
        self.assertFalse(delivery["authorized"])


if __name__ == "__main__":
    unittest.main()
