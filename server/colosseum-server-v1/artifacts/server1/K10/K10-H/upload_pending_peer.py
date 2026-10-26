import argparse
import binascii
import os
import socket
import struct
import time


PSTR = b"BitTorrent protocol"


def log(path, text):
    with open(path, "a", encoding="utf-8") as output:
        output.write(text + "\n")
        output.flush()


def recv_exact(conn, size):
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)


def frame(message_id, payload=b""):
    body = bytes([message_id]) + payload
    return struct.pack(">I", len(body)) + body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--trigger", required=True)
    parser.add_argument("--choked-marker", required=True)
    parser.add_argument("--pre-marker", required=True)
    parser.add_argument("--race-choked-marker", required=True)
    parser.add_argument("--race-monitor-trigger", required=True)
    parser.add_argument("--race-monitor-ready", required=True)
    parser.add_argument("--race-complete-marker", required=True)
    parser.add_argument("--stale-unchoke-marker", required=True)
    args = parser.parse_args()
    info_hash = binascii.unhexlify(args.info_hash)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", args.port))
        server.listen(1)
        log(args.log, f"LISTEN port={server.getsockname()[1]}")
        conn, _ = server.accept()
        with conn:
            conn.settimeout(12)
            handshake = recv_exact(conn, 68)
            if handshake is None or handshake[28:48] != info_hash:
                raise RuntimeError("handshake mismatch")
            peer_id = b"-K10H04-pending-gate"[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + b"\0" * 8 + info_hash + peer_id)
            conn.sendall(frame(5, b"\0"))
            conn.sendall(frame(2))
            log(args.log, "INTERESTED_SENT")
            while True:
                header = recv_exact(conn, 4)
                if header is None:
                    raise RuntimeError("adapter closed before explicit CHOKE")
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = recv_exact(conn, length)
                if body is None:
                    raise RuntimeError("truncated precondition message")
                if body[0] == 0:
                    log(args.log, "CHOKE_RECEIVED")
                    with open(args.choked_marker, "w", encoding="utf-8") as marker:
                        marker.write("choked\n")
                    break
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline and not os.path.exists(args.trigger):
                time.sleep(0.005)
            if not os.path.exists(args.trigger):
                raise RuntimeError("pre-dispatch trigger timeout")
            pre_request = struct.pack(">III", 0, 0, 16384)
            conn.sendall(frame(6, pre_request))
            log(args.log, "PRE_WIRE_REQUEST_SENT piece=0 offset=0 length=16384")
            with open(args.pre_marker, "w", encoding="utf-8") as marker:
                marker.write("sent\n")
            retried = False
            while True:
                header = recv_exact(conn, 4)
                if header is None:
                    log(args.log, "ADAPTER_CLOSED")
                    return
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = recv_exact(conn, length)
                if body is None:
                    return
                message_id = body[0]
                if message_id == 1 and not retried:
                    log(args.log, "UNCHOKE_RECEIVED")
                    post_request = struct.pack(">III", 1, 0, 16384)
                    conn.sendall(frame(6, post_request))
                    log(args.log, "POST_WIRE_RETRY_SENT piece=1 offset=0 length=16384")
                    retried = True
                elif message_id == 0 and retried:
                    log(args.log, "RACE_CHOKE_RECEIVED")
                    with open(args.race_choked_marker, "w", encoding="utf-8") as marker:
                        marker.write("choked\n")
                    break
                elif message_id == 7:
                    log(args.log, f"UNEXPECTED_PIECE raw={body.hex()}")

            deadline = time.monotonic() + 8
            while time.monotonic() < deadline and not os.path.exists(args.race_monitor_trigger):
                time.sleep(0.005)
            if not os.path.exists(args.race_monitor_trigger):
                raise RuntimeError("race monitor trigger timeout")
            with open(args.race_monitor_ready, "w", encoding="utf-8") as marker:
                marker.write("ready\n")
            conn.settimeout(0.05)
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                try:
                    header = recv_exact(conn, 4)
                except TimeoutError:
                    continue
                if header is None:
                    break
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = recv_exact(conn, length)
                if body is None:
                    break
                if body[0] == 1:
                    log(args.log, "STALE_UNCHOKE_RECEIVED")
                    with open(args.stale_unchoke_marker, "w", encoding="utf-8") as marker:
                        marker.write("stale\n")
                elif body[0] == 7:
                    log(args.log, f"UNEXPECTED_PIECE raw={body.hex()}")
            with open(args.race_complete_marker, "w", encoding="utf-8") as marker:
                marker.write("complete\n")


if __name__ == "__main__":
    main()
