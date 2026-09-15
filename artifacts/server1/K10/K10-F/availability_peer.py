import argparse
import binascii
import os
import socket
import struct
import threading
import time


PSTR = b"BitTorrent protocol"


def frame(message_id, payload=b""):
    body = bytes([message_id]) + payload
    return struct.pack(">I", len(body)) + body


def bitfield(spec):
    pieces = sorted({int(value) for value in spec.split(",") if value.strip()})
    highest = max(pieces, default=0)
    payload = bytearray((highest // 8) + 1)
    for piece in pieces:
        payload[piece // 8] |= 1 << (7 - (piece % 8))
    return pieces, bytes(payload)


class AvailabilityPeer:
    def __init__(self, args):
        self.args = args
        self.info_hash = binascii.unhexlify(args.info_hash)
        self.connections = 0
        self.lock = threading.Lock()

    def log(self, message):
        with self.lock:
            with open(self.args.log, "a", encoding="utf-8") as output:
                output.write(message + "\n")

    @staticmethod
    def recv_exact(conn, size):
        data = bytearray()
        while len(data) < size:
            try:
                chunk = conn.recv(size - len(data))
            except socket.timeout:
                continue
            if not chunk:
                return None
            data.extend(chunk)
        return bytes(data)

    def handle(self, conn, connection_id):
        conn.settimeout(0.1)
        try:
            handshake = self.recv_exact(conn, 68)
            if handshake is None or handshake[28:48] != self.info_hash:
                return
            peer_id = (b"-K10F01-availability" + str(connection_id).encode("ascii"))[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + (b"\0" * 8) + self.info_hash + peer_id)
            spec = self.args.first_pieces if connection_id == 1 else self.args.second_pieces
            pieces, payload = bitfield(spec)
            conn.sendall(frame(5, payload))
            conn.sendall(frame(1))
            conn.sendall(frame(2))
            self.log(f"BITFIELD_SENT connection={connection_id} pieces={','.join(map(str, pieces))}")

            have_sent = False
            while True:
                if connection_id == 1 and not have_sent and os.path.exists(self.args.have_marker):
                    conn.sendall(frame(4, struct.pack(">I", self.args.have_piece)))
                    have_sent = True
                    self.log(f"HAVE_SENT connection=1 piece={self.args.have_piece}")
                if connection_id == 1 and os.path.exists(self.args.disconnect_marker):
                    self.log("FORCED_DISCONNECT connection=1")
                    return
                try:
                    header = conn.recv(4)
                    if header == b"":
                        return
                    if header and len(header) == 4:
                        length = struct.unpack(">I", header)[0]
                        if length:
                            self.recv_exact(conn, length)
                except socket.timeout:
                    pass
                time.sleep(0.005)
        finally:
            conn.close()
            if connection_id == 1:
                with open(self.args.disconnected_marker, "w", encoding="utf-8") as marker:
                    marker.write("disconnected\n")

    def serve(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", self.args.port))
            server.listen(4)
            self.log(f"LISTEN port={self.args.port}")
            while True:
                conn, _ = server.accept()
                self.connections += 1
                threading.Thread(target=self.handle, args=(conn, self.connections), daemon=True).start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--first-pieces", default="3,1,3")
    parser.add_argument("--second-pieces", default="")
    parser.add_argument("--have-piece", type=int, default=0)
    parser.add_argument("--have-marker", required=True)
    parser.add_argument("--disconnect-marker", required=True)
    parser.add_argument("--disconnected-marker", required=True)
    args = parser.parse_args()
    AvailabilityPeer(args).serve()


if __name__ == "__main__":
    main()
