import hashlib
import socket
import threading
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

    def test_scripted_peer_records_handshake_states_and_authorized_blocks(self):
        fixture = build_single_file_torrent()
        actions = [
            PeerAction.handshake(),
            PeerAction.bitfield(b"\x80"),
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
            client.recv(4 + 8 + 16384)
        server.join(timeout=2)
        self.assertEqual(server.error, None)
        self.assertEqual(server.ledger[0]["piece"], 0)
        self.assertEqual(server.ledger[0]["length"], 16384)
        self.assertTrue(server.ledger[0]["authorized"])
        self.assertEqual(server.ledger[0]["order"], 0)
        self.assertTrue(all(entry["loopback"] for entry in server.ledger))


if __name__ == "__main__":
    unittest.main()
