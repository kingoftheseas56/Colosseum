import argparse
import binascii
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
    parser.add_argument("--mode", choices=("choke", "detach", "close"), required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--piece", type=int, required=True)
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
            peer_id = (b"-K10H03-" + args.tag.encode("ascii") + b"000000000000")[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + b"\0" * 8 + info_hash + peer_id)
            conn.sendall(frame(5, b"\0"))
            conn.sendall(frame(2))
            haves = set()
            unchoked = False
            sent = False
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
                payload = body[1:]
                if message_id == 1:
                    unchoked = True
                    log(args.log, "UNCHOKE")
                elif message_id == 0:
                    log(args.log, "CHOKE")
                elif message_id == 4 and len(payload) == 4:
                    piece = struct.unpack(">I", payload)[0]
                    haves.add(piece)
                    log(args.log, f"HAVE piece={piece}")
                elif message_id == 7:
                    log(args.log, f"UNEXPECTED_PIECE raw={body.hex()}")
                if unchoked and args.piece in haves and not sent:
                    conn.sendall(frame(6, struct.pack(">III", args.piece, 0, 16384)))
                    log(args.log, f"REQUEST piece={args.piece} offset=0 length=16384")
                    sent = True
                    if args.mode == "detach":
                        time.sleep(0.2)
                        log(args.log, "PEER_DETACH")
                        return


if __name__ == "__main__":
    main()
