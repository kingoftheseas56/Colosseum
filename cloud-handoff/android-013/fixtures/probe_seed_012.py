import hashlib
import socket
import struct

DATA = open(r"C:\nuvio-streaming-fixture-20261020\fixture.mp4", "rb").read()
IH = bytes.fromhex("79afbaf5c4e39bca900c8154cf3fa282a5019e8a")
PL = 16384
HS = b"\x13BitTorrent protocol"


def read_msg(s):
    raw = b""
    while len(raw) < 4:
        chunk = s.recv(4 - len(raw))
        if not chunk:
            raise ConnectionError("closed")
        raw += chunk
    (l,) = struct.unpack(">I", raw)
    if l > 65536: raise ValueError("unbounded peer message")
    if l == 0:
        return None, b""
    m = b""
    while len(m) < 1:
        m += s.recv(1)
    p = b""
    while len(p) < l - 1:
        chunk = s.recv(l - 1 - len(p))
        if not chunk:
            raise ConnectionError("closed")
        p += chunk
    return m, p


s = socket.create_connection(("127.0.0.1", 51412), timeout=15)
s.sendall(HS + b"\x00" * 8 + IH + b"-NV0110-PROBE".ljust(20, b"0"))
resp = b""
while len(resp) < 68:
    chunk = s.recv(68 - len(resp))
    if not chunk:
        raise ConnectionError("closed during handshake")
    resp += chunk
assert resp[:20] == HS and resp[28:48] == IH, "handshake mismatch"
print("handshake ok; seeder peer id:", resp[48:68])
m, p = read_msg(s)
assert m == b"\x05", f"expected bitfield, got {m}"
pieces_have = sum(bin(b).count("1") for b in p)
print("bitfield pieces:", pieces_have)
s.sendall(struct.pack(">IB", 1, 2))
while True:
    m, p = read_msg(s)
    if m == b"\x01":
        print("unchoked")
        break
got = {}
for idx in (0, 80, 160):
    ln = PL if idx < 160 else len(DATA) - 160 * PL
    s.sendall(struct.pack(">IBIII", 13, 6, idx, 0, ln))
    while idx not in got:
        m, p = read_msg(s)
        if m == b"\x07":
            i, bgn = struct.unpack(">II", p[:8])
            got[i] = p[8:]
checks = {
    0: got[0] == DATA[0:PL],
    80: got[80] == DATA[80 * PL:81 * PL],
    160: got[160] == DATA[160 * PL:],
}
print("byte-exact pieces:", checks)
s.close()
assert pieces_have == 161 and all(checks.values())
print("PROBE PASS")
