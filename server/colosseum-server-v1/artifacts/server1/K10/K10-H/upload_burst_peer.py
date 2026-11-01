import argparse
import binascii
import socket
import struct


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
    parser.add_argument("--requests", type=int, required=True)
    parser.add_argument("--duplicate-first", action="store_true")
    parser.add_argument("--invalid-first", action="store_true")
    parser.add_argument("--tag", required=True)
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
            peer_id = (b"-K10H02-" + args.tag.encode("ascii") + b"000000000000")[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + b"\0" * 8 + info_hash + peer_id)
            conn.sendall(frame(5, b"\0"))
            conn.sendall(frame(2))
            saw_unchoke = False
            haves = set()
            sent = False
            while True:
                header = recv_exact(conn, 4)
                if header is None:
                    return
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = recv_exact(conn, length)
                if body is None:
                    return
                message_id = body[0]
                payload = body[1:]
                if message_id == 1:
                    saw_unchoke = True
                elif message_id == 4 and len(payload) == 4:
                    piece = struct.unpack(">I", payload)[0]
                    haves.add(piece)
                    log(args.log, f"HAVE piece={piece} raw={body.hex()}")
                elif message_id == 7:
                    log(args.log, f"UNEXPECTED_PIECE raw={body.hex()}")
                if saw_unchoke and len(haves) >= 5 and not sent:
                    if args.invalid_first:
                        conn.sendall(frame(6, struct.pack(">III", 0, 1, 100)))
                        log(args.log, "REQUEST_INVALID piece=0 offset=1 length=100")
                        conn.sendall(frame(6, struct.pack(">III", 5, 0, 16384)))
                        log(args.log, "REQUEST_UNADVERTISED piece=5 offset=0 length=16384")
                    sequence = list(range(args.requests))
                    if args.duplicate_first and sequence:
                        sequence.insert(1, sequence[0])
                    for piece in sequence:
                        conn.sendall(frame(6, struct.pack(">III", piece, 0, 16384)))
                        log(args.log, f"REQUEST piece={piece} offset=0 length=16384")
                    sent = True
                    log(args.log, f"BURST_SENT count={len(sequence)}")


if __name__ == "__main__":
    main()
