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


def serve(args):
    info_hash = binascii.unhexlify(args.info_hash)
    if len(info_hash) != 20:
        raise ValueError("info hash must be 20 bytes")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", args.port))
        server.listen(1)
        port = server.getsockname()[1]
        log(args.log, f"LISTEN port={port}")
        conn, address = server.accept()
        with conn:
            conn.settimeout(8)
            handshake = recv_exact(conn, 68)
            if handshake is None or handshake[28:48] != info_hash:
                raise RuntimeError("handshake mismatch")
            peer_id = b"-K10H01-raw-upload1".ljust(20, b"0")[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + (b"\x00" * 8) + info_hash + peer_id)
            conn.sendall(frame(5, b"\x00"))
            conn.sendall(frame(2))
            log(args.log, f"INTERESTED_SENT remote={address[0]}:{address[1]}")

            saw_have = False
            saw_unchoke = False
            request_sent = False
            retry_sent = False
            while True:
                header = recv_exact(conn, 4)
                if header is None:
                    raise RuntimeError("adapter disconnected before PIECE")
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = recv_exact(conn, length)
                if body is None:
                    raise RuntimeError("truncated adapter message")
                message_id = body[0]
                payload = body[1:]
                if message_id == 1:
                    saw_unchoke = True
                    log(args.log, "UNCHOKE_RECEIVED")
                elif message_id == 4 and len(payload) == 4:
                    piece = struct.unpack(">I", payload)[0]
                    log(args.log, f"HAVE_RECEIVED piece={piece} raw={body.hex()}")
                    saw_have = piece == args.piece
                elif message_id == 7 and len(payload) >= 8:
                    piece, offset = struct.unpack(">II", payload[:8])
                    block = payload[8:]
                    log(args.log, f"PIECE_RECEIVED piece={piece} offset={offset} length={len(block)} raw={body.hex()}")
                    if piece != args.piece or offset != args.offset or len(block) != args.length:
                        raise RuntimeError("PIECE coordinates mismatch")
                    if block != bytes([args.byte]) * args.length:
                        raise RuntimeError("PIECE payload mismatch")
                    return

                if saw_have and saw_unchoke and not request_sent:
                    request = struct.pack(">III", args.piece, args.offset, args.length)
                    conn.sendall(frame(6, request))
                    log(args.log, f"REQUEST_SENT piece={args.piece} offset={args.offset} length={args.length}")
                    time.sleep(0.1)
                    conn.sendall(frame(8, request))
                    log(args.log, f"CANCEL_SENT piece={args.piece} offset={args.offset} length={args.length}")
                    time.sleep(0.1)
                    conn.sendall(frame(6, request))
                    log(args.log, f"RETRY_SENT piece={args.piece} offset={args.offset} length={args.length}")
                    request_sent = True
                    retry_sent = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--piece", type=int, default=1)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--length", type=int, default=73)
    parser.add_argument("--byte", type=int, default=82)
    serve(parser.parse_args())


if __name__ == "__main__":
    main()
