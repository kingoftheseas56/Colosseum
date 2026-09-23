import argparse
import binascii
import os
import socket
import struct
import sys
import threading
import time


PSTR = b"BitTorrent protocol"


class ControlledPeer:
    def __init__(self, args):
        self.args = args
        self.info_hash = binascii.unhexlify(args.info_hash)
        if len(self.info_hash) != 20:
            raise ValueError("info hash must be 20 bytes")
        self.log_lock = threading.Lock()
        self.connection_lock = threading.Lock()
        self.connections = 0

    def log(self, text):
        with self.log_lock:
            with open(self.args.log, "a", encoding="utf-8") as output:
                output.write(text + "\n")
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

    @staticmethod
    def frame(message_id, payload=b""):
        body = bytes([message_id]) + payload
        return struct.pack(">I", len(body)) + body

    def send_bitfield(self, conn):
        bits = 0
        pieces = []
        for text in self.args.pieces.split(","):
            if not text.strip():
                continue
            piece = int(text)
            pieces.append(piece)
            bits |= 1 << (7 - piece)
        conn.sendall(self.frame(5, bytes([bits])))
        self.log("BITFIELD_SENT pieces=" + ",".join(str(x) for x in pieces))

    def send_have_after_marker(self, conn, send_lock, connection_id):
        if self.args.have_piece < 0 or not self.args.have_marker:
            return
        for _ in range(1500):
            if os.path.exists(self.args.have_marker):
                try:
                    with send_lock:
                        conn.sendall(self.frame(4, struct.pack(">I", self.args.have_piece)))
                    self.log(f"HAVE_SENT connection={connection_id} piece={self.args.have_piece}")
                except OSError:
                    self.log(f"HAVE_DROPPED connection={connection_id}")
                return
            time.sleep(0.01)
        self.log(f"HAVE_TIMEOUT connection={connection_id}")

    def handle(self, conn, address, connection_id):
        conn.settimeout(0.2)
        send_lock = threading.Lock()
        try:
            handshake = self.recv_exact(conn, 68)
            if handshake is None or handshake[0] != len(PSTR) or handshake[1:20] != PSTR:
                return
            remote_hash = handshake[28:48]
            self.log(f"HANDSHAKE connection={connection_id} remote={address[0]}:{address[1]} info_hash_match={int(remote_hash == self.info_hash)}")
            if remote_hash != self.info_hash:
                return
            peer_id = (b"-K10R01-" + self.args.peer_tag.encode("ascii")
                       + str(connection_id).encode("ascii") + b"00000000000")[:20]
            conn.sendall(bytes([len(PSTR)]) + PSTR + (b"\x00" * 8) + self.info_hash + peer_id)
            self.send_bitfield(conn)
            conn.sendall(self.frame(1))
            self.log(f"UNCHOKE_SENT connection={connection_id}")
            conn.sendall(self.frame(2))
            self.log(f"INTERESTED_SENT connection={connection_id}")

            if self.args.disconnect_first and connection_id == 1:
                time.sleep(0.1)
                self.log("FORCED_FIRST_DISCONNECT connection=1")
                return

            threading.Thread(target=self.send_have_after_marker,
                             args=(conn, send_lock, connection_id), daemon=True).start()
            while True:
                try:
                    header = self.recv_exact(conn, 4)
                except socket.timeout:
                    continue
                if header is None:
                    self.log(f"CLIENT_DISCONNECTED connection={connection_id}")
                    return
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    continue
                body = self.recv_exact(conn, length)
                if body is None:
                    return
                message_id = body[0]
                payload = body[1:]
                if message_id == 6 and len(payload) == 12:
                    piece, start, request_length = struct.unpack(">III", payload)
                    self.log(f"REQUEST epoch_ms={time.time_ns() // 1000000} connection={connection_id} piece={piece} start={start} length={request_length}")
                    if self.args.disconnect_on_request:
                        self.log(f"FORCED_REQUEST_DISCONNECT connection={connection_id}")
                        return
                    block = self.args.byte.encode("ascii") * request_length
                    with send_lock:
                        conn.sendall(self.frame(7, struct.pack(">II", piece, start) + block))
                    self.log(f"PIECE_SENT connection={connection_id} piece={piece} start={start} length={request_length}")
                elif message_id == 2:
                    self.log(f"INTERESTED connection={connection_id}")
                elif message_id == 8:
                    self.log(f"CANCEL connection={connection_id}")
                else:
                    self.log(f"MESSAGE connection={connection_id} id={message_id} bytes={len(payload)}")
        except (OSError, socket.timeout) as error:
            self.log(f"CLIENT_DISCONNECTED connection={connection_id} error={type(error).__name__}")
        finally:
            try:
                conn.close()
            except OSError:
                pass
            if self.args.disconnect_first and connection_id == 1 and self.args.disconnect_marker:
                with open(self.args.disconnect_marker, "w", encoding="utf-8") as marker:
                    marker.write("disconnected\n")

    def watch_udp(self, probe):
        # The source swarm dials TCP only (M814 passes utp:false to M818), so
        # any datagram on the peer's port is a uTP dial the source never makes.
        while True:
            try:
                data, address = probe.recvfrom(2048)
            except OSError:
                continue
            self.log(f"UDP_DATAGRAM bytes={len(data)} remote={address[0]}:{address[1]}")

    @staticmethod
    def bind_pair(port):
        # A TCP listener plus a UDP probe on the same port number.
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        stage = "tcp"
        try:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", port))
            stage = "udp"
            probe.bind(("127.0.0.1", server.getsockname()[1]))
            return server, probe
        except OSError as error:
            server.close()
            probe.close()
            raise OSError(f"{stage} {error}") from error

    def serve(self):
        # Windows reserves blocks of ports at run time, so a fixed port can be
        # unusable. Fall back to an OS-chosen port; runners read the actual
        # port from the LISTEN line.
        try:
            server, probe = self.bind_pair(self.args.port)
        except OSError as error:
            self.log(f"PORT_UNAVAILABLE port={self.args.port} error={error}")
            server = probe = None
            for _ in range(20):
                try:
                    server, probe = self.bind_pair(0)
                    break
                except OSError as retry:
                    error = retry
            if server is None:
                self.log(f"BIND_FAILED port={self.args.port} error={error}")
                sys.exit(2)
        port = server.getsockname()[1]
        with server, probe:
            server.listen(8)
            server.settimeout(0.2)
            threading.Thread(target=self.watch_udp, args=(probe,), daemon=True).start()
            self.log(f"LISTEN port={port}")
            while True:
                try:
                    conn, address = server.accept()
                except socket.timeout:
                    continue
                with self.connection_lock:
                    self.connections += 1
                    connection_id = self.connections
                threading.Thread(target=self.handle, args=(conn, address, connection_id), daemon=True).start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--pieces", default="0,1")
    parser.add_argument("--byte", default="K")
    parser.add_argument("--peer-tag", default="peer")
    parser.add_argument("--have-piece", type=int, default=-1)
    parser.add_argument("--have-marker")
    parser.add_argument("--disconnect-first", action="store_true")
    parser.add_argument("--disconnect-on-request", action="store_true")
    parser.add_argument("--disconnect-marker")
    args = parser.parse_args()
    ControlledPeer(args).serve()


if __name__ == "__main__":
    main()
