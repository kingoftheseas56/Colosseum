"""Task 013 private synthetic torrent fixture: make, track, seed and probe.

Python stdlib only. Everything binds to 127.0.0.1. The device reaches the
tracker and seeder through task-owned `adb reverse` mappings of the same
ports, so the torrent announces to 127.0.0.1, the tracker advertises
127.0.0.1 as the only peer, and the identical hash-pinned torrent serves an
emulator or a physical device without editing.

  make   <media> <out.torrent> <announce-url>   write a private single-file torrent
  track  <torrent> <port> <peer-ip> <peer-port> <log.jsonl>
  seed   <torrent> <media> <port> <log.jsonl>
  probe  <torrent> <media> <host> <port>        independent wire-protocol check

Ported from the historical task-012 tracker/seeder/probe (kept untouched in
../../fixtures). Differences: no LAN allow-list, piece hashes are verified
against the torrent before serving, malformed or foreign wire messages close
the connection, and uploads are bounded to the torrent's exact payload.
"""
import datetime
import hashlib
import json
import pathlib
import socket
import struct
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, unquote_to_bytes

PIECE_LENGTH = 16384
PSTR = b"\x13BitTorrent protocol"
MAX_MESSAGE = 16384 + 13


# ---------------------------------------------------------------- bencode
def bencode(value):
    if isinstance(value, int):
        return b"i%de" % value
    if isinstance(value, bytes):
        return b"%d:%s" % (len(value), value)
    if isinstance(value, str):
        return bencode(value.encode())
    if isinstance(value, list):
        return b"l" + b"".join(bencode(v) for v in value) + b"e"
    if isinstance(value, dict):
        items = sorted((k.encode() if isinstance(k, str) else k, v) for k, v in value.items())
        return b"d" + b"".join(bencode(k) + bencode(v) for k, v in items) + b"e"
    raise TypeError(type(value))


def bdecode(data, i=0):
    c = data[i:i + 1]
    if c == b"i":
        end = data.index(b"e", i)
        return int(data[i + 1:end]), end + 1
    if c == b"l":
        i, out = i + 1, []
        while data[i:i + 1] != b"e":
            v, i = bdecode(data, i)
            out.append(v)
        return out, i + 1
    if c == b"d":
        i, out = i + 1, {}
        while data[i:i + 1] != b"e":
            k, i = bdecode(data, i)
            v, i = bdecode(data, i)
            out[k] = v
        return out, i + 1
    colon = data.index(b":", i)
    n = int(data[i:colon])
    return data[colon + 1:colon + 1 + n], colon + 1 + n


def info_span(raw):
    """Byte span of the bencoded info dict exactly as stored in the file."""
    key = b"4:info"
    start = raw.index(key) + len(key)
    _, end = bdecode(raw, start)
    return start, end


class Torrent:
    def __init__(self, path):
        raw = pathlib.Path(path).read_bytes()
        meta, end = bdecode(raw)
        if end != len(raw):
            raise ValueError("trailing bytes after torrent dictionary")
        start, stop = info_span(raw)
        self.info_hash = hashlib.sha1(raw[start:stop]).digest()
        info = meta[b"info"]
        self.name = info[b"name"].decode()
        self.length = info[b"length"]
        self.piece_length = info[b"piece length"]
        pieces = info[b"pieces"]
        if len(pieces) % 20:
            raise ValueError("pieces field is not a multiple of 20 bytes")
        self.hashes = [pieces[i:i + 20] for i in range(0, len(pieces), 20)]
        self.private = info.get(b"private") == 1
        self.announce = meta.get(b"announce", b"").decode()
        self.sha256 = hashlib.sha256(raw).hexdigest()
        self.bytes = len(raw)
        if len(self.hashes) != (self.length + self.piece_length - 1) // self.piece_length:
            raise ValueError("piece count does not match length")

    def piece_size(self, index):
        if index == len(self.hashes) - 1:
            return self.length - index * self.piece_length
        return self.piece_length


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


class JsonLog:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.lock = threading.Lock()

    def __call__(self, event, **kv):
        entry = {"at": now(), "event": event, **kv}
        line = json.dumps(entry, sort_keys=True)
        with self.lock, self.path.open("a", encoding="utf-8") as f:
            f.write(line + "\n")
        print(line[:240], flush=True)


# ------------------------------------------------------------------- make
def make(media, out, announce):
    data = pathlib.Path(media).read_bytes()
    pieces = b"".join(hashlib.sha1(data[i:i + PIECE_LENGTH]).digest()
                      for i in range(0, len(data), PIECE_LENGTH))
    meta = {
        "announce": announce,
        "created by": "colosseum-task013-fixture",
        "info": {
            "length": len(data),
            "name": pathlib.Path(media).name,
            "piece length": PIECE_LENGTH,
            "pieces": pieces,
            "private": 1,
        },
    }
    pathlib.Path(out).write_bytes(bencode(meta))
    t = Torrent(out)
    print(json.dumps({"torrent": str(out), "torrent_sha256": t.sha256, "torrent_bytes": t.bytes,
                      "info_hash_v1": t.info_hash.hex(), "media_sha256": hashlib.sha256(data).hexdigest(),
                      "media_bytes": len(data), "piece_length": PIECE_LENGTH,
                      "piece_count": len(t.hashes), "private": t.private, "announce": t.announce}))


# ------------------------------------------------------------------ track
def track(torrent_path, port, peer_ip, peer_port, log_path):
    t = Torrent(torrent_path)
    log = JsonLog(log_path)
    peers = socket.inet_aton(peer_ip) + struct.pack(">H", peer_port)
    body = b"d8:completei1e10:incompletei0e8:intervali30e5:peers6:" + peers + b"e"

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            url = urlsplit(self.path)
            params = {}
            for part in url.query.split("&"):
                if "=" in part:
                    k, v = part.split("=", 1)
                    params.setdefault(k, []).append(v)
            got = unquote_to_bytes(params["info_hash"][0]) if len(params.get("info_hash", [])) == 1 else b""
            ok = (url.path == "/announce" and got == t.info_hash
                  and self.client_address[0] == "127.0.0.1")
            log("announce", path=url.path, remote=self.client_address[0], accepted=ok,
                infohash_ok=got == t.info_hash, event_param=(params.get("event") or [""])[0],
                left=(params.get("left") or [""])[0], uploaded=(params.get("uploaded") or [""])[0],
                user_agent=self.headers.get("User-Agent", "")[:60])
            payload = body if ok else b"d14:failure reason9:forbiddene"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *args):
            pass

    log("tracker-start", info_hash=t.info_hash.hex(), port=port, advertised_peer=f"{peer_ip}:{peer_port}")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


# ------------------------------------------------------------------- seed
def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return buf


def seed(torrent_path, media, port, log_path):
    t = Torrent(torrent_path)
    data = pathlib.Path(media).read_bytes()
    log = JsonLog(log_path)
    if len(data) != t.length:
        raise SystemExit("media length does not match torrent")
    for i, h in enumerate(t.hashes):
        if hashlib.sha1(data[i * t.piece_length:i * t.piece_length + t.piece_size(i)]).digest() != h:
            raise SystemExit(f"piece {i} does not match torrent hash")
    peer_id = b"-CL0130-" + hashlib.sha1(t.info_hash + b"seed").digest()[:12]
    totals = {"served_bytes": 0, "connections": 0}
    totals_lock = threading.Lock()

    def serve(conn, peer):
        hs = recv_exact(conn, 68)
        if hs[:20] != PSTR:
            log("reject", peer=peer, reason="not bittorrent")
            return
        if hs[28:48] != t.info_hash:
            log("reject", peer=peer, reason="infohash mismatch", got=hs[28:48].hex())
            return
        log("handshake", peer=peer, client_peer_id_prefix=hs[48:56].decode(errors="replace"),
            reserved=hs[20:28].hex())
        conn.sendall(PSTR + b"\x00" * 8 + t.info_hash + peer_id)
        nbytes = (len(t.hashes) + 7) // 8
        bitfield = bytearray(nbytes)
        for i in range(len(t.hashes)):
            bitfield[i // 8] |= 0x80 >> (i % 8)
        conn.sendall(struct.pack(">IB", 1 + nbytes, 5) + bytes(bitfield))
        served = 0
        while True:
            (length,) = struct.unpack(">I", recv_exact(conn, 4))
            if length == 0:
                continue
            if length > MAX_MESSAGE:
                log("reject", peer=peer, reason="oversized message", length=length)
                return
            msg = recv_exact(conn, length)
            mid, payload = msg[0], msg[1:]
            if mid == 2:
                conn.sendall(struct.pack(">IB", 1, 1))
                log("unchoked", peer=peer)
            elif mid == 3:
                log("not_interested", peer=peer, served_bytes=served)
            elif mid == 6:
                if len(payload) != 12:
                    log("reject", peer=peer, reason="malformed request", length=len(payload))
                    return
                index, begin, size = struct.unpack(">III", payload)
                if (index >= len(t.hashes) or size < 1 or size > 16384
                        or begin + size > t.piece_size(index)):
                    log("reject", peer=peer, reason="out-of-bounds request",
                        index=index, begin=begin, length=size)
                    return
                start = index * t.piece_length + begin
                conn.sendall(struct.pack(">IBII", 9 + size, 7, index, begin) + data[start:start + size])
                served += size
                with totals_lock:
                    totals["served_bytes"] += size
                log("piece", peer=peer, index=index, begin=begin, length=size,
                    served_bytes_connection=served, served_bytes_total=totals["served_bytes"])
            elif mid == 8:
                log("cancel", peer=peer)
            elif mid in (0, 1, 4, 5):
                log("msg", peer=peer, id=mid, length=length)
            else:
                log("msg-ignored", peer=peer, id=mid, length=length)

    def handle(conn, addr):
        peer = f"{addr[0]}:{addr[1]}"
        try:
            conn.settimeout(180)
            serve(conn, peer)
        except Exception as error:  # connection end is evidence, not a crash
            log("peer-end", peer=peer, error=repr(error)[:120])
        finally:
            conn.close()
            log("closed", peer=peer)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(8)
    log("seed-start", info_hash=t.info_hash.hex(), media_sha256=hashlib.sha256(data).hexdigest(),
        bytes=len(data), pieces=len(t.hashes), pieces_verified=len(t.hashes), port=port)
    while True:
        conn, addr = srv.accept()
        if addr[0] != "127.0.0.1":
            log("reject-peer", peer=str(addr))
            conn.close()
            continue
        with totals_lock:
            totals["connections"] += 1
        threading.Thread(target=handle, args=(conn, addr), daemon=True).start()


# ------------------------------------------------------------------ probe
def read_msg(s):
    (length,) = struct.unpack(">I", recv_exact(s, 4))
    if length > MAX_MESSAGE:
        raise ValueError("unbounded peer message")
    if length == 0:
        return None, b""
    m = recv_exact(s, length)
    return m[0], m[1:]


def probe(torrent_path, media, host, port):
    t = Torrent(torrent_path)
    data = pathlib.Path(media).read_bytes()
    result = {"info_hash": t.info_hash.hex()}
    # Negative control: a foreign infohash must not get a handshake back.
    s = socket.create_connection((host, port), timeout=10)
    s.sendall(PSTR + b"\x00" * 8 + b"\x11" * 20 + b"-CLPROBE-0000000000"[:20].ljust(20, b"0"))
    try:
        result["foreign_infohash_rejected"] = s.recv(68) == b""
    except (ConnectionError, socket.timeout):
        result["foreign_infohash_rejected"] = True
    s.close()
    s = socket.create_connection((host, port), timeout=10)
    s.sendall(PSTR + b"\x00" * 8 + t.info_hash + b"-CLPROBE-".ljust(20, b"0"))
    resp = recv_exact(s, 68)
    result["handshake_ok"] = resp[:20] == PSTR and resp[28:48] == t.info_hash and len(resp[48:68]) == 20
    m, p = read_msg(s)
    result["bitfield_pieces"] = sum(bin(b).count("1") for b in p) if m == 5 else -1
    s.sendall(struct.pack(">IB", 1, 2))
    while True:
        m, p = read_msg(s)
        if m == 1:
            break
    checks = {}
    last = len(t.hashes) - 1
    for idx in sorted({0, last // 2, last}):
        size = t.piece_size(idx)
        got = b""
        for begin in range(0, size, 16384):
            n = min(16384, size - begin)
            s.sendall(struct.pack(">IBIII", 13, 6, idx, begin, n))
            while True:
                m, p = read_msg(s)
                if m == 7:
                    got += p[8:]
                    break
        checks[idx] = (hashlib.sha1(got).digest() == t.hashes[idx]
                       and got == data[idx * t.piece_length:idx * t.piece_length + size])
    result["byte_exact_pieces"] = {str(k): v for k, v in checks.items()}
    # Negative control: an out-of-bounds request closes the connection.
    s.sendall(struct.pack(">IBIII", 13, 6, last, t.piece_size(last) - 1, 2))
    try:
        result["out_of_bounds_closed"] = s.recv(1) == b""
    except (ConnectionError, socket.timeout):
        result["out_of_bounds_closed"] = True
    s.close()
    result["pass"] = (result["foreign_infohash_rejected"] and result["handshake_ok"]
                      and result["bitfield_pieces"] == len(t.hashes)
                      and all(checks.values()) and result["out_of_bounds_closed"])
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "make":
        make(*args)
    elif cmd == "track":
        track(args[0], int(args[1]), args[2], int(args[3]), args[4])
    elif cmd == "seed":
        seed(args[0], args[1], int(args[2]), args[3])
    elif cmd == "probe":
        sys.exit(probe(args[0], args[1], args[2], int(args[3])))
    else:
        raise SystemExit(__doc__)
