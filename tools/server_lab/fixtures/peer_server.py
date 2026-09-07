"""Loopback-only scripted BitTorrent peer with an auditable wire ledger."""

from __future__ import annotations

from dataclasses import dataclass
import socket
import threading
import time
from typing import Optional

from .torrents import PIECE_LENGTH, TorrentFixture


@dataclass(frozen=True)
class PeerAction:
    kind: str
    piece: int = 0
    begin: int = 0
    length: Optional[int] = None
    delay_seconds: float = 0.0
    payload: bytes = b""

    @classmethod
    def handshake(cls) -> "PeerAction": return cls("handshake")
    @classmethod
    def bitfield(cls, payload: bytes) -> "PeerAction": return cls("bitfield", payload=payload)
    @classmethod
    def choke(cls) -> "PeerAction": return cls("choke")
    @classmethod
    def unchoke(cls) -> "PeerAction": return cls("unchoke")
    @classmethod
    def delay(cls, seconds: float) -> "PeerAction": return cls("delay", delay_seconds=seconds)
    @classmethod
    def corrupt(cls, piece: int, begin: int, length: int) -> "PeerAction": return cls("corrupt", piece, begin, length)
    @classmethod
    def deliver(cls, piece: int, begin: int, length: int) -> "PeerAction": return cls("deliver", piece, begin, length)
    @classmethod
    def disconnect(cls) -> "PeerAction": return cls("disconnect")


class ScriptedPeerServer:
    def __init__(self, fixture: TorrentFixture, actions: list[PeerAction], peer_id: bytes = b"-P06A00-012345678901"):
        self.fixture = fixture
        self.actions = actions
        self.peer_id = peer_id[:20].ljust(20, b"0")
        self.ledger: list[dict] = []
        self.error: Optional[BaseException] = None
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self._thread: Optional[threading.Thread] = None
        self._sequence = 0

    @property
    def address(self) -> tuple[str, int]:
        return self._listener.getsockname()

    def handshake_bytes(self) -> bytes:
        return b"\x13BitTorrent protocol" + b"\x00" * 8 + bytes.fromhex(self.fixture.infohash) + self.peer_id

    def start(self) -> None:
        self._thread = threading.Thread(target=self._serve, name="p06-loopback-peer", daemon=True)
        self._thread.start()

    def join(self, timeout: Optional[float] = None) -> None:
        if self._thread:
            self._thread.join(timeout)
        self._listener.close()

    def _record(self, action: PeerAction, authorized: bool = True) -> None:
        self.ledger.append({"peer": f"{self.address[0]}:{self.address[1]}", "piece": action.piece, "begin": action.begin,
                            "length": action.length, "order": self._sequence, "sequence": self._sequence,
                            "timestamp_ns": time.monotonic_ns(), "authorized": authorized, "loopback": True, "kind": action.kind})
        self._sequence += 1

    def _serve(self) -> None:
        try:
            client, _ = self._listener.accept()
            with client:
                client.settimeout(2)
                if self._recv_exact(client, 68)[:20] != b"\x13BitTorrent protocol":
                    raise ValueError("invalid BitTorrent handshake")
                for action in self.actions:
                    if action.delay_seconds:
                        time.sleep(action.delay_seconds)
                    if action.kind == "delay":
                        continue
                    if action.kind == "handshake":
                        client.sendall(self.handshake_bytes())
                    elif action.kind == "bitfield":
                        self._send(client, 1 + len(action.payload), bytes([5]) + action.payload)
                    elif action.kind == "choke":
                        self._send(client, 1, b"\x00")
                    elif action.kind == "unchoke":
                        self._send(client, 1, b"\x01")
                    elif action.kind in {"deliver", "corrupt"}:
                        self._deliver(client, action)
                    elif action.kind == "disconnect":
                        self._record(action)
                        return
                    else:
                        raise ValueError(f"unsupported peer action: {action.kind}")
        except BaseException as exc:
            self.error = exc

    def _deliver(self, client: socket.socket, action: PeerAction) -> None:
        requested = self.fixture.block_size(action.piece, action.begin)
        length = action.length if action.length is not None else requested
        authorized = length == requested
        self._record(action, authorized)
        data = self.fixture.pieces[action.piece][action.begin:action.begin + length]
        if action.kind == "corrupt" and data:
            data = bytes([data[0] ^ 0xFF]) + data[1:]
        self._send(client, 9 + len(data), b"\x07" + action.piece.to_bytes(4, "big") + action.begin.to_bytes(4, "big") + data)

    @staticmethod
    def _send(client: socket.socket, length: int, payload: bytes) -> None:
        client.sendall(length.to_bytes(4, "big") + payload)

    @staticmethod
    def _recv_exact(client: socket.socket, length: int) -> bytes:
        result = bytearray()
        while len(result) < length:
            chunk = client.recv(length - len(result))
            if not chunk:
                raise ConnectionError("peer disconnected during handshake")
            result.extend(chunk)
        return bytes(result)
