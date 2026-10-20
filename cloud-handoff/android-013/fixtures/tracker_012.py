from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import datetime,json,socket,struct
from urllib.parse import urlsplit,parse_qs,unquote_to_bytes
out=Path(r'C:\nuvio-streaming-fixture-20261020\resume012\tracker-log-012.jsonl')
info_hash=bytes.fromhex('79afbaf5c4e39bca900c8154cf3fa282a5019e8a')
peers=socket.inet_aton('127.0.0.1')+struct.pack('>H',51412)
body=b'd8:intervali30e5:peers6:'+peers+b'e'
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url=urlsplit(self.path)
        raw=[part.split('=',1)[1] for part in url.query.split('&') if part.startswith('info_hash=')]
        got=unquote_to_bytes(raw[0]) if raw else b''
        ok=url.path=='/announce' and got==info_hash and self.client_address[0]=='127.0.0.1'
        entry={'at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'path':url.path,'remote':self.client_address[0],'infohash_ok':got==info_hash,
               'accepted':ok,'user_agent':self.headers.get('User-Agent','')[:70]}
        with out.open('a',encoding='utf-8') as f: f.write(json.dumps(entry)+'\n')
        print(json.dumps(entry),flush=True)
        self.send_response(200 if ok else 403)
        self.send_header('Content-Type','text/plain')
        self.send_header('Content-Length',str(len(body) if ok else 0))
        self.end_headers()
        if ok: self.wfile.write(body)
    def log_message(self,*args): pass
print('TRACKER_LISTEN 127.0.0.1:51413, PEER 127.0.0.1:51412',flush=True)
ThreadingHTTPServer(('127.0.0.1',51413),Handler).serve_forever()

