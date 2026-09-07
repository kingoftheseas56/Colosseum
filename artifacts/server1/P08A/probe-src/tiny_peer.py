#!/usr/bin/env python3
"""P08A two-peer BitTorrent wire fixture and acceptance checker."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
from typing import Any

PROTOCOL = b"BitTorrent protocol"
PAYLOAD = b"\x00" * 32768
TARGET = {"piece": 0, "offset": 0, "length": 16384}
CASES = {"P08A-CONTROL", "P08A-01", "P08A-02", "P08A-03"}


def bencode(value: Any) -> bytes:
    if isinstance(value, int):
        return b"i" + str(value).encode("ascii") + b"e"
    if isinstance(value, bytes):
        return str(len(value)).encode("ascii") + b":" + value
    if isinstance(value, dict):
        return (
            b"d"
            + b"".join(bencode(k) + bencode(value[k]) for k in sorted(value))
            + b"e"
        )
    raise TypeError(type(value))


def write_torrent(path: Path) -> bytes:
    info = {
        b"length": len(PAYLOAD),
        b"name": b"p08a.bin",
        b"piece length": len(PAYLOAD),
        b"pieces": hashlib.sha1(PAYLOAD).digest(),
    }
    encoded_info = bencode(info)
    path.write_bytes(bencode({b"info": info}))
    return hashlib.sha1(encoded_info).digest()


def recv_exact(sock: socket.socket, count: int) -> bytes:
    chunks: list[bytes] = []
    remaining = count
    while remaining:
        data = sock.recv(remaining)
        if not data:
            raise EOFError("peer closed")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


class Ledger:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.events: list[dict[str, Any]] = []

    def add(self, peer: str, event: str, **fields: Any) -> None:
        row = {
            "time_ns": time.monotonic_ns(),
            "peer": peer,
            "event": event,
            **fields,
        }
        with self._lock:
            self.events.append(row)

    def snapshot(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self.events)


class PeerServer:
    def __init__(
        self,
        name: str,
        info_hash: bytes,
        case_name: str,
        ledger: Ledger,
    ) -> None:
        self.name = name
        self.info_hash = info_hash
        self.case_name = case_name
        self.ledger = ledger
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(15.0)
        self.port = int(self.listener.getsockname()[1])
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.connection: socket.socket | None = None
        self.error: str | None = None

    def start(self) -> None:
        self.thread.start()

    def close(self) -> None:
        try:
            self.listener.close()
        except OSError:
            pass
        conn = self.connection
        if conn is not None:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                conn.close()
            except OSError:
                pass

    def _send_message(self, sock: socket.socket, message_id: int, payload: bytes = b"") -> None:
        packet = struct.pack(">I", 1 + len(payload)) + bytes([message_id]) + payload
        sock.sendall(packet)
        self.ledger.add(self.name, "send", message_id=message_id, bytes=len(packet))

    def _run(self) -> None:
        try:
            conn, address = self.listener.accept()
            self.connection = conn
            conn.settimeout(10.0)
            self.ledger.add(self.name, "accepted", address=f"{address[0]}:{address[1]}")

            pstrlen = recv_exact(conn, 1)[0]
            if pstrlen != len(PROTOCOL):
                raise RuntimeError(f"bad pstrlen {pstrlen}")
            protocol = recv_exact(conn, pstrlen)
            reserved = recv_exact(conn, 8)
            got_hash = recv_exact(conn, 20)
            remote_peer_id = recv_exact(conn, 20)
            if protocol != PROTOCOL:
                raise RuntimeError("bad protocol")
            if got_hash != self.info_hash:
                raise RuntimeError(
                    f"infohash mismatch {got_hash.hex()} != {self.info_hash.hex()}"
                )
            self.ledger.add(
                self.name,
                "handshake",
                reserved=reserved.hex(),
                remote_peer_id=remote_peer_id.hex(),
            )

            peer_id = (f"-P08A01-{self.name * 12}").encode("ascii")[:20]
            peer_id = peer_id.ljust(20, b"X")
            conn.sendall(
                bytes([len(PROTOCOL)])
                + PROTOCOL
                + (b"\x00" * 8)
                + self.info_hash
                + peer_id
            )
            self.ledger.add(self.name, "handshake_reply")

            # One 32 KiB piece is advertised by both peers. That piece contains
            # two eligible 16 KiB blocks, which is important for the picker-control proof.
            self._send_message(conn, 5, b"\x80")
            self._send_message(conn, 1)

            while True:
                length_bytes = recv_exact(conn, 4)
                (length,) = struct.unpack(">I", length_bytes)
                if length == 0:
                    self.ledger.add(self.name, "keepalive")
                    continue
                body = recv_exact(conn, length)
                message_id = body[0]
                payload = body[1:]

                if message_id == 2:
                    self.ledger.add(self.name, "interested")
                    continue
                if message_id == 3:
                    self.ledger.add(self.name, "not_interested")
                    continue
                if message_id == 8 and len(payload) == 12:
                    piece, offset, request_length = struct.unpack(">III", payload)
                    self.ledger.add(
                        self.name,
                        "cancel",
                        piece=piece,
                        offset=offset,
                        length=request_length,
                    )
                    continue
                if message_id != 6:
                    self.ledger.add(
                        self.name,
                        "message",
                        message_id=message_id,
                        payload_bytes=len(payload),
                    )
                    continue
                if len(payload) != 12:
                    raise RuntimeError(f"bad request payload length {len(payload)}")

                piece, offset, request_length = struct.unpack(">III", payload)
                self.ledger.add(
                    self.name,
                    "request",
                    piece=piece,
                    offset=offset,
                    length=request_length,
                )

                # The control run responds to ordinary picker requests so libtorrent
                # has a fully viable download path. Owned runs respond only to the
                # one authorized request on B. P08A-03 intentionally holds it.
                should_respond = False
                if self.case_name == "P08A-CONTROL":
                    should_respond = (
                        piece == 0
                        and offset in (0, 16384)
                        and request_length == 16384
                    )
                elif (
                    self.name == "B"
                    and self.case_name in {"P08A-01", "P08A-02"}
                    and piece == TARGET["piece"]
                    and offset == TARGET["offset"]
                    and request_length == TARGET["length"]
                ):
                    should_respond = True

                if should_respond:
                    data = PAYLOAD[offset : offset + request_length]
                    piece_payload = struct.pack(">II", piece, offset) + data
                    self._send_message(conn, 7, piece_payload)
                    self.ledger.add(
                        self.name,
                        "piece_response",
                        piece=piece,
                        offset=offset,
                        length=len(data),
                    )
        except (EOFError, ConnectionResetError, BrokenPipeError, OSError) as exc:
            self.ledger.add(self.name, "closed", detail=str(exc))
        except Exception as exc:  # noqa: BLE001
            self.error = f"{type(exc).__name__}: {exc}"
            self.ledger.add(self.name, "fixture_error", detail=self.error)
        finally:
            conn = self.connection
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass
            try:
                self.listener.close()
            except OSError:
                pass


def run_case(probe: Path, case_name: str, out_dir: Path, timeout_ms: int) -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    torrent_path = out_dir / "p08a.torrent"
    save_dir = out_dir / "save"
    save_dir.mkdir(exist_ok=True)
    info_hash = write_torrent(torrent_path)

    ledger = Ledger()
    peer_a = PeerServer("A", info_hash, case_name, ledger)
    peer_b = PeerServer("B", info_hash, case_name, ledger)
    peer_a.start()
    peer_b.start()

    command = [
        str(probe.resolve()),
        case_name,
        str(torrent_path.resolve()),
        str(save_dir.resolve()),
        str(peer_a.port),
        str(peer_b.port),
        str(timeout_ms),
    ]
    (out_dir / "probe-command.txt").write_text(
        subprocess.list2cmdline(command) + "\n", encoding="utf-8"
    )
    started_ns = time.monotonic_ns()
    try:
        proc = subprocess.run(
            command,
            text=True,
            capture_output=True,
            timeout=(timeout_ms / 1000.0) + 12.0,
            check=False,
            env=os.environ.copy(),
        )
        return_code = proc.returncode
        stdout = proc.stdout
        stderr = proc.stderr
    except subprocess.TimeoutExpired as exc:
        return_code = 124
        stdout = exc.stdout or ""
        stderr = (exc.stderr or "") + "\nfixture: probe timed out\n"
    ended_ns = time.monotonic_ns()

    (out_dir / "probe.stdout.txt").write_text(stdout, encoding="utf-8")
    (out_dir / "probe.stderr.txt").write_text(stderr, encoding="utf-8")

    # Allow peer handlers to observe the session close before we stop fixtures.
    peer_a.thread.join(timeout=1.5)
    peer_b.thread.join(timeout=1.5)
    natural_close_a = not peer_a.thread.is_alive()
    natural_close_b = not peer_b.thread.is_alive()
    peer_a.close()
    peer_b.close()
    peer_a.thread.join(timeout=0.5)
    peer_b.thread.join(timeout=0.5)

    events = ledger.snapshot()
    with (out_dir / "peer-wire.jsonl").open("w", encoding="utf-8", newline="\n") as f:
        for event in events:
            f.write(json.dumps(event, sort_keys=True) + "\n")

    requests = [e for e in events if e["event"] == "request"]
    requests_a = [e for e in requests if e["peer"] == "A"]
    requests_b = [e for e in requests if e["peer"] == "B"]
    target_b = [
        e
        for e in requests_b
        if e.get("piece") == TARGET["piece"]
        and e.get("offset") == TARGET["offset"]
        and e.get("length") == TARGET["length"]
    ]
    unowned = [
        e
        for e in requests
        if not (
            e["peer"] == "B"
            and e.get("piece") == TARGET["piece"]
            and e.get("offset") == TARGET["offset"]
            and e.get("length") == TARGET["length"]
        )
    ]

    errors = [e for e in events if e["event"] == "fixture_error"]
    handshakes = [e for e in events if e["event"] == "handshake"]
    two_peer_handshakes = {e["peer"] for e in handshakes} == {"A", "B"}
    validation: dict[str, Any] = {
        "probe_exit_zero": return_code == 0,
        "fixture_errors_zero": not errors,
        "two_peers_advertised_same_piece": two_peer_handshakes,
        "piece_length": 32768,
        "block_size": 16384,
        "eligible_blocks": [
            {"piece": 0, "offset": 0, "length": 16384},
            {"piece": 0, "offset": 16384, "length": 16384},
        ],
    }

    if case_name == "P08A-CONTROL":
        normal_picker_work_observed = any(
            e.get("piece") == 0
            and e.get("offset") in (0, 16384)
            and e.get("length") == 16384
            for e in requests
        )
        validation.update(
            {
                "normal_picker_work_observed": normal_picker_work_observed,
                "request_count": len(requests),
            }
        )
        passed = all(
            [
                validation["probe_exit_zero"],
                validation["fixture_errors_zero"],
                normal_picker_work_observed,
            ]
        )
    else:
        validation.update(
            {
                "target_request_on_b": len(target_b) == 1,
                "requests_on_a_zero": len(requests_a) == 0,
                "unowned_requests_zero": len(unowned) == 0,
                "request_count": len(requests),
                "target_request_count": len(target_b),
            }
        )
        if case_name in {"P08A-01", "P08A-02"}:
            response_seen = any(
                e["peer"] == "B"
                and e["event"] == "piece_response"
                and e.get("offset") == 0
                and e.get("length") == 16384
                for e in events
            )
            validation["target_response_returned"] = response_seen
        if case_name == "P08A-03":
            validation["response_intentionally_held"] = not any(
                e["event"] == "piece_response" for e in events
            )
            validation["no_hidden_replay"] = len(requests) == 1
            validation["session_closed_both_peer_sockets"] = (
                natural_close_a and natural_close_b
            )

        passed = all(
            bool(value)
            for key, value in validation.items()
            if key
            not in {
                "piece_length",
                "block_size",
                "eligible_blocks",
                "request_count",
                "target_request_count",
            }
        )

    result = {
        "schema_version": 1,
        "case": case_name,
        "passed": passed,
        "probe_exit_code": return_code,
        "probe_started_ns": started_ns,
        "probe_ended_ns": ended_ns,
        "peer_ports": {"A": peer_a.port, "B": peer_b.port},
        "torrent_info_hash": info_hash.hex(),
        "validation": validation,
        "requests": requests,
        "fixture_errors": errors,
    }
    (out_dir / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    print(json.dumps(result, sort_keys=True))
    if stdout:
        print("--- probe stdout ---")
        print(stdout, end="" if stdout.endswith("\n") else "\n")
    if stderr:
        print("--- probe stderr ---")
        print(stderr, end="" if stderr.endswith("\n") else "\n")

    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=sorted(CASES))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--timeout-ms", type=int, default=9000)
    args = parser.parse_args()
    return run_case(args.probe, args.case, args.out, args.timeout_ms)


if __name__ == "__main__":
    raise SystemExit(main())
