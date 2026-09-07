import argparse
import binascii
import os
import socket
import struct
import threading
import time


PSTR = b"BitTorrent protocol"
EXTENDED_ID = 20
# The peer advertises its own ut_metadata id as 1.  Responses use the
# extension id advertised by libtorrent in the client's handshake (2).
UT_METADATA_ID = 1
CLIENT_UT_METADATA_ID = 2


def bencode(value):
    if isinstance(value, bytes):
        return str(len(value)).encode("ascii") + b":" + value
    if isinstance(value, str):
        return bencode(value.encode("utf-8"))
    if isinstance(value, int):
        return b"i" + str(value).encode("ascii") + b"e"
    if isinstance(value, dict):
        output = bytearray(b"d")
        for key in sorted(value):
            output.extend(bencode(key))
            output.extend(bencode(value[key]))
        output.extend(b"e")
        return bytes(output)
    raise TypeError(type(value))


def bdecode(data, offset=0):
    token = data[offset:offset + 1]
    if token == b"i":
        end = data.index(b"e", offset)
        return int(data[offset + 1:end]), end + 1
    if token == b"l":
        values = []
        offset += 1
        while data[offset:offset + 1] != b"e":
            value, offset = bdecode(data, offset)
            values.append(value)
        return values, offset + 1
    if token == b"d":
        values = {}
        offset += 1
        while data[offset:offset + 1] != b"e":
            key, offset = bdecode(data, offset)
            value, offset = bdecode(data, offset)
            values[key] = value
        return values, offset + 1
    if token.isdigit():
        colon = data.index(b":", offset)
        size = int(data[offset:colon])
        start = colon + 1
        return data[start:start + size], start + size
    raise ValueError("invalid bencode token")


class TinyPeer:
    def __init__(self, args, info_hash):
        self.port = args.port
        self.label = args.label
        self.info_hash = info_hash
        self.log_path = os.path.abspath(args.log)
        self.delay_ms = args.delay_ms
        self.release_file = os.path.abspath(args.release_file) if args.release_file else None
        self.mode = args.mode
        self.piece_length = args.piece_length
        self.file_length = args.file_length
        self.split_offset = args.split_offset if args.split_offset >= 0 else self.file_length
        self.first_byte = args.first_byte.encode("ascii")
        self.second_byte = args.second_byte.encode("ascii")
        self.hold_after_offset = args.hold_after_offset
        self.hold_all = args.hold_all
        self.advertised_pieces = None if args.pieces is None else {
            int(piece) for piece in args.pieces.split(",") if piece.strip()
        }
        self.duplicate_late = args.duplicate_late
        self.choke_cycle = args.choke_cycle
        self.metadata_path = os.path.abspath(args.metadata_path) if args.metadata_path else None
        self.metadata = b""
        if self.metadata_path:
            with open(self.metadata_path, "rb") as source:
                self.metadata = source.read()
        self.stop = threading.Event()
        self.log_lock = threading.Lock()
        self.connection_lock = threading.Lock()
        self.connection_count = 0

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

    @staticmethod
    def frame(message_id, payload=b""):
        body = bytes([message_id]) + payload
        return struct.pack(">I", len(body)) + body

    def send_handshake(self, conn):
        peer_id = (b"-P08A01-" + self.label.encode("ascii") + b"000000000000")[:20]
        conn.sendall(bytes([len(PSTR)]) + PSTR + (b"\x00" * 8) + self.info_hash + peer_id)

    def send_bitfield(self, conn):
        pieces = (self.file_length + self.piece_length - 1) // self.piece_length
        bits = bytearray((pieces + 7) // 8)
        advertised = range(pieces) if self.advertised_pieces is None else sorted(self.advertised_pieces)
        for piece in advertised:
            if piece < 0 or piece >= pieces:
                raise ValueError(f"advertised piece out of range: {piece} / {pieces}")
            bits[piece // 8] |= 1 << (7 - piece % 8)
        payload = bytes([5]) + bytes(bits)
        conn.sendall(struct.pack(">I", len(payload)) + payload)
        advertised_text = ",".join(str(piece) for piece in advertised)
        self.log(f"BITFIELD_SENT pieces={pieces} advertised={advertised_text}")

    def send_extension_handshake(self, conn):
        # BEP-10: advertise ut_metadata as extension id 1.
        payload = bencode({b"m": {b"ut_metadata": UT_METADATA_ID}})
        conn.sendall(self.frame(EXTENDED_ID, b"\x00" + payload))
        self.log("EXT_HANDSHAKE_SENT ut_metadata=1")

    def wait_for_release(self):
        if not self.release_file:
            return False
        for _ in range(2000):
            if os.path.exists(self.release_file):
                self.log("RELEASE_OBSERVED")
                return True
            time.sleep(0.01)
        self.log("RELEASE_TIMEOUT")
        return False

    def block_for(self, piece, start, request_length):
        absolute = piece * self.piece_length + start
        first = absolute < self.split_offset
        value = self.first_byte if first else self.second_byte
        return value * request_length

    def send_piece(self, conn, send_lock, connection_id, piece, start, request_length, late):
        if self.delay_ms:
            time.sleep(self.delay_ms / 1000.0)
        released = False
        if self.hold_all or (self.hold_after_offset >= 0 and start >= self.hold_after_offset):
            released = self.wait_for_release()
        block = self.block_for(piece, start, request_length)
        piece_body = bytes([7]) + struct.pack(">II", piece, start) + block
        try:
            frame = struct.pack(">I", len(piece_body)) + piece_body
            duplicate = self.duplicate_late and released
            with send_lock:
                # Keep the old response and its duplicate in one TCP write so
                # session teardown cannot erase the second wire frame.
                conn.sendall(frame + (frame if duplicate else b""))
                if late:
                    self.log(f"LATE_PIECE_SENT_AFTER_RELEASE connection={connection_id}")
                else:
                    self.log(f"PIECE_SENT connection={connection_id} piece={piece} start={start} length={request_length}")
                if duplicate:
                    self.log(f"DUPLICATE_LATE_PIECE connection={connection_id} piece={piece} start={start}")
        except OSError:
            self.log(f"PIECE_DROPPED connection={connection_id}")

    def send_metadata(self, conn, send_lock, connection_id):
        header = {b"msg_type": 1, b"piece": 0, b"total_size": len(self.metadata)}
        body = bytes([CLIENT_UT_METADATA_ID]) + bencode(header) + self.metadata
        with send_lock:
            conn.sendall(self.frame(EXTENDED_ID, body))
        self.log(f"METADATA_RESPONSE connection={connection_id} bytes={len(self.metadata)}")

    def maybe_release_choke(self, conn, send_lock, connection_id, released):
        if not self.choke_cycle or released or not self.release_file:
            return released
        if os.path.exists(self.release_file):
            with send_lock:
                conn.sendall(self.frame(1))
                conn.sendall(self.frame(3))
            self.log(f"UNCHOKE_SENT connection={connection_id}")
            self.log(f"NOT_INTERESTED_SENT connection={connection_id}")
            return True
        return False

    def handle(self, conn, address, connection_id):
        conn.settimeout(0.2)
        send_lock = threading.Lock()
        released = False
        try:
            handshake = self.recv_exact(conn, 68)
            if handshake is None:
                return
            if handshake[0] != len(PSTR) or handshake[1:20] != PSTR:
                self.log(f"NON_PROTOCOL_IGNORED connection={connection_id} bytes={handshake.hex()}")
                return
            remote_hash = handshake[28:48]
            self.log(f"HANDSHAKE connection={connection_id} remote={address[0]}:{address[1]} info_hash={remote_hash.hex()} expected={self.info_hash.hex()} info_hash_match={int(remote_hash == self.info_hash)}")
            if remote_hash != self.info_hash:
                return
            self.send_handshake(conn)
            if self.mode == "metadata":
                self.send_extension_handshake(conn)
            else:
                self.send_bitfield(conn)
                if self.choke_cycle:
                    conn.sendall(self.frame(0))
                    self.log(f"CHOKE_SENT connection={connection_id}")
                    conn.sendall(self.frame(2))
                    self.log(f"INTERESTED_SENT connection={connection_id}")
                else:
                    conn.sendall(self.frame(1))
                    self.log(f"UNCHOKE_SENT connection={connection_id}")
                    conn.sendall(self.frame(2))
                    self.log(f"INTERESTED_SENT connection={connection_id}")
            while not self.stop.is_set():
                released = self.maybe_release_choke(conn, send_lock, connection_id, released)
                try:
                    header = self.recv_exact(conn, 4)
                except socket.timeout:
                    continue
                if header is None:
                    self.log(f"CLIENT_DISCONNECTED connection={connection_id}")
                    return
                length = struct.unpack(">I", header)[0]
                if length == 0:
                    self.log(f"KEEPALIVE connection={connection_id}")
                    continue
                body = self.recv_exact(conn, length)
                if body is None:
                    self.log(f"CLIENT_DISCONNECTED connection={connection_id}")
                    return
                message_id = body[0]
                payload = body[1:]
                if message_id == 2:
                    self.log(f"INTERESTED connection={connection_id}")
                elif message_id == 3:
                    self.log(f"NOT_INTERESTED connection={connection_id}")
                elif message_id == 6 and len(payload) == 12:
                    piece, start, request_length = struct.unpack(">III", payload)
                    self.log(f"REQUEST connection={connection_id} piece={piece} start={start} length={request_length}")
                    threading.Thread(
                        target=self.send_piece,
                        args=(conn, send_lock, connection_id, piece, start, request_length, bool(self.release_file)),
                        daemon=True,
                    ).start()
                elif message_id == 8 and len(payload) == 12:
                    piece, start, request_length = struct.unpack(">III", payload)
                    self.log(f"CANCEL connection={connection_id} piece={piece} start={start} length={request_length}")
                elif message_id == EXTENDED_ID and payload:
                    extension_id = payload[0]
                    extension_payload = payload[1:]
                    if self.mode != "metadata":
                        self.log(f"EXTENDED_RECEIVED connection={connection_id} id={extension_id} bytes={len(extension_payload)}")
                    elif extension_id == 0:
                        self.log(f"EXT_HANDSHAKE_RECEIVED connection={connection_id}")
                    elif extension_id == UT_METADATA_ID:
                        header, _ = bdecode(extension_payload)
                        if header.get(b"msg_type") == 0:
                            self.log(f"METADATA_REQUEST connection={connection_id} piece={header.get(b'piece', -1)}")
                            self.send_metadata(conn, send_lock, connection_id)
                        else:
                            self.log(f"METADATA_MESSAGE connection={connection_id} type={header.get(b'msg_type', -1)}")
                else:
                    self.log(f"MESSAGE connection={connection_id} id={message_id} bytes={len(payload)}")
        except (OSError, socket.timeout, ValueError) as error:
            self.log(f"CLIENT_DISCONNECTED connection={connection_id} error={type(error).__name__}")
        finally:
            try:
                with send_lock:
                    conn.close()
            except OSError:
                pass

    def serve(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", self.port))
            server.listen(8)
            server.settimeout(0.2)
            self.log(f"LISTEN label={self.label} port={self.port} mode={self.mode}")
            self.log(f"CONFIG hold_all={int(self.hold_all)} hold_after_offset={self.hold_after_offset} release_file={self.release_file or ''} duplicate_late={int(self.duplicate_late)}")
            while not self.stop.is_set():
                try:
                    conn, address = server.accept()
                except socket.timeout:
                    continue
                with self.connection_lock:
                    self.connection_count += 1
                    connection_id = self.connection_count
                thread = threading.Thread(target=self.handle, args=(conn, address, connection_id), daemon=True)
                thread.start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--info-hash", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--mode", choices=("regular", "metadata"), default="regular")
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--release-file")
    parser.add_argument("--piece-length", type=int, default=16384)
    parser.add_argument("--file-length", type=int, default=16384)
    parser.add_argument("--split-offset", type=int, default=-1)
    parser.add_argument("--first-byte", default="P")
    parser.add_argument("--second-byte", default="P")
    parser.add_argument("--hold-after-offset", type=int, default=-1)
    parser.add_argument("--hold-all", action="store_true")
    parser.add_argument("--pieces")
    parser.add_argument("--duplicate-late", action="store_true")
    parser.add_argument("--choke-cycle", action="store_true")
    parser.add_argument("--metadata-path")
    args = parser.parse_args()
    try:
        info_hash = binascii.unhexlify(args.info_hash)
    except binascii.Error as error:
        raise SystemExit(f"invalid info hash: {error}")
    if len(info_hash) != 20:
        raise SystemExit("info hash must be 20 bytes")
    TinyPeer(args, info_hash).serve()


if __name__ == "__main__":
    main()
