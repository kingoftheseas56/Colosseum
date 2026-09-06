import argparse
import binascii
import os
import socket
import struct
import threading
import time


PIECE_LENGTH = 16 * 1024
PSTR = b"BitTorrent protocol"


class TinyPeer:
    def __init__(self, port, label, info_hash, log_path, delay_ms, release_file):
        self.port = port
        self.label = label
        self.info_hash = info_hash
        self.log_path = log_path
        self.delay_ms = delay_ms
        self.release_file = release_file
        self.stop = threading.Event()
        self.log_lock = threading.Lock()

    def log(self, line):
        with self.log_lock:
            with open(self.log_path, "a", encoding="utf-8") as output:
                output.write(line + "\n")
                output.flush()

    @staticmethod
    def recv_exact(conn, size):
        data = bytearray()
        while len(data) < size:
            chunk = conn.recv(size - len(data))
            if not chunk:
                return None
            data.extend(chunk)
        return bytes(data)

    def send_handshake(self, conn):
        peer_id = (b"-P08A01-" + self.label.encode("ascii") + b"000000000000")[:20]
        conn.sendall(bytes([len(PSTR)]) + PSTR + (b"\x00" * 8) + self.info_hash + peer_id)

    def wait_for_release(self):
        if not self.release_file:
            return False
        for _ in range(400):
            if os.path.exists(self.release_file):
                self.log("RELEASE_OBSERVED")
                return True
            time.sleep(0.01)
        self.log("RELEASE_TIMEOUT")
        return False

    def handle(self, conn, address):
        # Allow the initial handshake to arrive in multiple scheduler slices;
        # the steady-state ledger remains responsive below.
        conn.settimeout(2.0)
        try:
            handshake = self.recv_exact(conn, 68)
            if handshake is None:
                return
            if handshake[0] != len(PSTR) or handshake[1:20] != PSTR:
                self.log(f"NON_PROTOCOL_IGNORED bytes={handshake.hex()}")
                return
            remote_hash = handshake[28:48]
            self.log(f"HANDSHAKE_HEX {handshake.hex()}")
            self.log(f"HANDSHAKE remote={address[0]}:{address[1]} info_hash={remote_hash.hex()} expected={self.info_hash.hex()} info_hash_match={int(remote_hash == self.info_hash)}")
            if remote_hash != self.info_hash:
                return
            conn.settimeout(0.2)
            self.send_handshake(conn)
            # Two advertised pieces; the control phase uses piece 1 and the
            # commanded phase reserves piece 0 for selected peer B.
            conn.sendall(b"\x00\x00\x00\x02\x05\xC0")
            conn.sendall(b"\x00\x00\x00\x01\x01")
            while not self.stop.is_set():
                try:
                    header = self.recv_exact(conn, 4)
                except socket.timeout:
                    continue
                if header is None:
                    self.log("CLIENT_DISCONNECTED")
                    return
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    self.log("KEEPALIVE")
                    continue
                body = self.recv_exact(conn, length)
                if body is None:
                    self.log("CLIENT_DISCONNECTED")
                    return
                message_id = body[0]
                payload = body[1:]
                if message_id == 2:
                    self.log("INTERESTED")
                elif message_id == 6 and len(payload) == 12:
                    piece, start, request_length = struct.unpack(">III", payload)
                    self.log(f"REQUEST piece={piece} start={start} length={request_length}")
                    time.sleep(self.delay_ms / 1000.0)
                    released = self.wait_for_release()
                    block = b"P" * PIECE_LENGTH
                    # PIECE payload: message id, piece index, begin offset, block.
                    piece_body = bytes([7]) + struct.pack(">II", piece, start) + block
                    try:
                        conn.sendall(struct.pack(">I", len(piece_body)) + piece_body)
                        self.log("LATE_PIECE_SENT_AFTER_RELEASE" if released else "PIECE_SENT")
                    except OSError:
                        self.log("LATE_PIECE_DROPPED_AFTER_RELEASE" if released else "PIECE_DROPPED")
                elif message_id == 8:
                    self.log("CANCEL")
                else:
                    self.log(f"MESSAGE id={message_id} bytes={len(payload)}")
        except (OSError, socket.timeout):
            self.log("CLIENT_DISCONNECTED")
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def serve(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", self.port))
            server.listen(4)
            server.settimeout(0.2)
            self.log(f"LISTEN label={self.label} port={self.port}")
            while not self.stop.is_set():
                try:
                    conn, address = server.accept()
                except socket.timeout:
                    continue
                thread = threading.Thread(target=self.handle, args=(conn, address), daemon=True)
                thread.start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--delay-ms", type=int, default=250)
    parser.add_argument("--release-file")
    args = parser.parse_args()
    try:
        info_hash = binascii.unhexlify(args.info_hash)
    except binascii.Error as error:
        raise SystemExit(f"invalid info hash: {error}")
    if len(info_hash) != 20:
        raise SystemExit("info hash must be 20 bytes")
    TinyPeer(args.port, args.label, info_hash, args.log, args.delay_ms, args.release_file).serve()


if __name__ == "__main__":
    main()
