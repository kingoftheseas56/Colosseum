"""Task-owned BitTorrent seeder for the nuvio streaming fixture (Laptop 1).

Speaks the minimal wire protocol needed to seed one small private torrent to
the engine on the tablet: handshake, bitfield, choke/unchoke, request/piece.
Discovery: none (no tracker, no DHT, no LSD, no PEX; inbound only). Every
connection, request and served byte range is logged with timestamps so the
transfer itself is evidence.

Run: python nuvio_seed.py <fixture.mp4> <infohash-hex> <port>
"""
import hashlib
import json
import pathlib
import socket
import struct
import sys
import threading
import time
import datetime

FILE = pathlib.Path(sys.argv[1])
INFO_HASH = bytes.fromhex(sys.argv[2])
PORT = int(sys.argv[3])
BIND = sys.argv[4]
PIECE_LENGTH = 16384
DATA = FILE.read_bytes()
PIECES = (len(DATA) + PIECE_LENGTH - 1) // PIECE_LENGTH
PEER_ID = b"-NV0120-seed" + hashlib.sha1(str(time.time()).encode()).digest()[:8]
LOG = []
LOG_LOCK = threading.Lock()
LOGFILE = FILE.parent / "resume012" / "seed-log-012.jsonl"


def log(event, **kv):
    entry = {"at": datetime.datetime.now().astimezone().isoformat(), "event": event, **kv}
    with LOG_LOCK:
        LOG.append(entry)
        with LOGFILE.open("a", encoding="utf-8") as f:
            f.write(json.dumps(entry) + "\n")
    print(json.dumps(entry)[:220], flush=True)


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return buf


def serve(conn, addr):
    conn.settimeout(120)
    hs = recv_exact(conn, 68)
    if hs[:20] != b"\x13BitTorrent protocol":
        raise ConnectionError("not bittorrent")
    if hs[28:48] != INFO_HASH:
        log("reject", peer=addr, reason="infohash mismatch", got=hs[28:48].hex())
        return
    log("handshake", peer=addr, client_peer_id=hs[48:68].decode(errors="replace"))
    conn.sendall(b"\x13BitTorrent protocol" + b"\x00" * 8 + INFO_HASH + PEER_ID)
    bitfield_bytes = (PIECES + 7) // 8
    bitfield = bytearray(bitfield_bytes)
    for i in range(PIECES):
        bitfield[i // 8] |= 0x80 >> (i % 8)
    conn.sendall(struct.pack(">I", 1 + bitfield_bytes) + b"\x05" + bytes(bitfield))
    log("bitfield", peer=addr, pieces=PIECES)
    choked = True
    served = 0
    while True:
        header = recv_exact(conn, 4)
        (length,) = struct.unpack(">I", header)
        if length == 0:
            continue
        if length < 1 or length > 65536:
            raise ValueError("invalid peer message length")
        msg_id = recv_exact(conn, 1)
        payload = recv_exact(conn, length - 1) if length > 1 else b""
        if msg_id == b"\x02":  # interested
            conn.sendall(struct.pack(">IB", 1, 1))  # unchoke
            choked = False
            log("unchoked", peer=addr)
        elif msg_id == b"\x03":  # not interested
            log("not_interested", peer=addr, served_bytes=served)
        elif msg_id == b"\x06":  # request
            index, begin, req_len = struct.unpack(">III", payload)
            if len(payload) != 12 or index >= PIECES or begin >= PIECE_LENGTH or req_len < 1 or req_len > 16384 or begin + req_len > PIECE_LENGTH or index * PIECE_LENGTH + begin + req_len > len(DATA):
                log("reject-request", peer=addr, index=index, begin=begin, length=req_len)
                raise ValueError("invalid piece request")
            start = index * PIECE_LENGTH + begin
            piece = DATA[start:start + req_len]
            block = struct.pack(">IBII", 9 + req_len, 7, index, begin) + piece
            conn.sendall(block)
            served += req_len
            log("piece", peer=addr, index=index, begin=begin, length=req_len,
                served_bytes_total=served)
        elif msg_id == b"\x08":  # cancel
            log("cancel", peer=addr)
        else:
            log("msg", peer=addr, id=msg_id.hex(), length=length)


def main():
    log("seed-start", file=str(FILE), bytes=len(DATA), sha256=hashlib.sha256(DATA).hexdigest(),
        info_hash=INFO_HASH.hex(), pieces=PIECES, port=PORT, peer_id=PEER_ID.decode(errors="replace"))
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((BIND, PORT))
    srv.listen(4)
    log("listening", bind=BIND, port=PORT)
    while True:
        conn, addr = srv.accept()
        if addr[0] not in ("127.0.0.1", "192.168.29.37"):
            log("reject-peer", peer=str(addr))
            conn.close()
            continue
        threading.Thread(target=lambda c, a: handle(c, a), args=(conn, addr), daemon=True).start()


def handle(conn, addr):
    peer = f"{addr[0]}:{addr[1]}"
    try:
        serve(conn, peer)
    except Exception as error:
        log("peer-end", peer=peer, error=repr(error)[:120])


if __name__ == "__main__":
    main()
