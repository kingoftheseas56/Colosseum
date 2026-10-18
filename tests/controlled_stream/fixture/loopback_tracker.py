#!/usr/bin/env python3
"""One owned info-hash; loopback only; compact peer response; 180s lifetime."""
import argparse, http.server, socket, struct, time, urllib.parse

def bencode(x):
    if isinstance(x, bytes): return str(len(x)).encode()+b':'+x
    if isinstance(x, int): return b'i'+str(x).encode()+b'e'
    if isinstance(x, dict): return b'd'+b''.join(bencode(k)+bencode(v) for k,v in sorted(x.items()))+b'e'
    raise TypeError(type(x))

def serve(expected):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_GET(self):
            u=urllib.parse.urlsplit(self.path)
            fields={}
            for pair in u.query.split('&'):
                k,_,v=pair.partition('=');fields[k]=urllib.parse.unquote_to_bytes(v)
            if self.client_address[0]!='127.0.0.1' or u.path!='/announce' or fields.get('info_hash')!=expected:
                self.send_error(403);return
            body=bencode({b'interval':30,b'complete':1,b'incomplete':0,b'peers':socket.inet_aton('127.0.0.1')+struct.pack('!H',49021)})
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
            print('ANNOUNCE accepted fixture hash',flush=True)
    server=http.server.HTTPServer(('127.0.0.1',49022),Handler)
    server.timeout=1
    end=time.monotonic()+180
    try:
        while time.monotonic()<end: server.handle_request()
    finally: server.server_close()
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('info_hash');args=ap.parse_args()
    h=bytes.fromhex(args.info_hash)
    if len(h)!=20: raise SystemExit('expected 20-byte info hash')
    serve(h)
