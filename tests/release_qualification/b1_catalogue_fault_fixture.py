"""QA-only loopback publication fault fixture. Import is socket-free.
Run only in the authorized hosted runner. Never run on the restricted executor.
Control is an owned JSON file, not a remote endpoint. No outbound requests.
"""
import argparse
import hashlib
import http.server
import json
from pathlib import Path
import time


def response(raw, mode):
    if mode == 'healthy':
        return 200, raw, len(raw)
    if mode == 'zero':
        return 200, b'', 0
    if mode == 'truncated-json':
        body = raw[:max(1, len(raw)//2)]
        return 200, body, len(body)
    if mode == 'short-transfer':
        return 200, raw[:max(1, len(raw)//2)], len(raw)
    if mode == 'unavailable':
        return 503, b'', 0
    raise ValueError('Unsupported fixture mode')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--publication', type=Path, required=True)
    p.add_argument('--control', type=Path, required=True)
    p.add_argument('--publication-b', type=Path)
    p.add_argument('--evidence', type=Path, required=True)
    a = p.parse_args()
    root = a.publication.resolve(strict=True)
    current = json.loads((root/'catalogue/current.json').read_text())
    index_path = current['index']['path']
    a.evidence.mkdir(parents=True, exist_ok=False)
    def log(row):
        with (a.evidence/'requests.jsonl').open('a') as f:
            f.write(json.dumps(row) + '\n')
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'
        def log_message(self, *_):
            pass
        def do_POST(self):
            self.send_response(503); self.send_header('Content-Length', '0'); self.end_headers()
            log({'path': self.path, 'method': 'POST', 'status': 503, 'workerFallbackBlocked': True})
        def do_GET(self):
            path = self.path.split('?', 1)[0]
            control = json.loads(a.control.read_text())
            selected_root = a.publication_b.resolve(strict=True) if control.get('publication') == 'B' and a.publication_b else root
            file = (selected_root/path.lstrip('/')).resolve()
            if not file.is_relative_to(selected_root) or not path.startswith(('/catalogue/', '/assets/content/')) or not file.is_file():
                self.send_response(404); self.send_header('Content-Length','0'); self.end_headers()
                log({'path':path,'status':404,'bytes':0}); return
            target = index_path if control['target'] == 'index' else control['target']
            mode = control['mode'] if path == target else 'healthy'
            raw = file.read_bytes()
            status, body, advertised = response(raw, mode)
            self.send_response(status)
            self.send_header('Content-Length', str(advertised))
            self.send_header('Content-Type', 'application/json' if path.endswith('.json') else 'application/octet-stream')
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            sent = False
            try:
                self.wfile.write(body); self.wfile.flush(); sent = True
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
            log({'timeNs':time.monotonic_ns(),'path':path,'mode':mode,'status':status,
                 'bytes':len(body),'advertisedBytes':advertised,'writeCompleted':sent,
                 'bodySha256':hashlib.sha256(body).hexdigest()})
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0), Handler)
    (a.evidence/'endpoint.json').write_text(json.dumps({'origin':f'http://127.0.0.1:{server.server_port}', 'indexPath':index_path}))
    try:
        server.serve_forever()
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
