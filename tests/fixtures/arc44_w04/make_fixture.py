from pathlib import Path
import hashlib

def enc(v):
    if isinstance(v, int): return b'i' + str(v).encode() + b'e'
    if isinstance(v, bytes): return str(len(v)).encode() + b':' + v
    if isinstance(v, list): return b'l' + b''.join(enc(x) for x in v) + b'e'
    if isinstance(v, dict):
        return b'd' + b''.join(enc(k) + enc(v[k]) for k in sorted(v)) + b'e'
    raise TypeError(type(v))

info = {
    b'files': [
        {b'length': 700000, b'path': [b'Episode.S01E02.mkv']},
        {b'length': 1000000, b'path': [b'Episode.S01E03.mkv']},
        {b'length': 12345, b'path': [b'notes.txt']},
    ],
    b'name': b'Arc44Pack',
    b'piece length': 1048576,
    b'pieces': bytes.fromhex('11' * 20 + '22' * 20),
}
torrent = {b'announce': b'udp://tracker.example:80/announce', b'info': info}
out = Path(__file__).with_name('multifile.torrent')
out.write_bytes(enc(torrent))
print(hashlib.sha1(enc(info)).hexdigest())
print(len(enc(info)), len(enc(torrent)))