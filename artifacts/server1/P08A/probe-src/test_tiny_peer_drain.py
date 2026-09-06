import socket
import struct
import tempfile
import threading
import time
from pathlib import Path

from tiny_peer import PSTR, TinyPeer


def recv_exact(conn, size):
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError("peer closed during fixture setup")
        data.extend(chunk)
    return bytes(data)


def request_frame(piece):
    body = bytes([6]) + struct.pack(">III", piece, 0, 16 * 1024)
    return struct.pack(">I", len(body)) + body


def main():
    info_hash = b"P08A-drain-proof-001"
    if len(info_hash) != 20:
        raise RuntimeError("fixture info hash must be 20 bytes")

    with tempfile.TemporaryDirectory(prefix="p08a-peer-drain-") as temp_dir:
        log_path = Path(temp_dir) / "wire.log"
        peer = TinyPeer(0, "drain", info_hash, str(log_path), 3000, None)
        server, client = socket.socketpair()
        worker = threading.Thread(target=peer.handle, args=(server, ("fixture", 1)), daemon=True)
        worker.start()
        try:
            peer_id = b"-P08ATEST-0000000000"
            client.sendall(bytes([len(PSTR)]) + PSTR + (b"\x00" * 8) + info_hash + peer_id)
            recv_exact(client, 68 + 6 + 5)
            client.sendall(request_frame(0) + request_frame(1))

            deadline = time.monotonic() + 1.0
            request_count = 0
            while time.monotonic() < deadline:
                if log_path.exists():
                    request_count = sum(
                        line.startswith("REQUEST ")
                        for line in log_path.read_text(encoding="utf-8").splitlines()
                    )
                if request_count == 2:
                    break
                time.sleep(0.01)
            if request_count != 2:
                raise RuntimeError(
                    f"receive loop failed to drain both queued request frames before delayed response; logged={request_count}"
                )
        finally:
            peer.stop.set()
            client.close()
            worker.join(timeout=2)

    print("PASS: tiny peer drained and logged 2 request frames before delayed responses")


if __name__ == "__main__":
    main()
