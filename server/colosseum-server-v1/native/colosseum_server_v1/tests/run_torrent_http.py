"""Production native transport -> store -> reader -> real HTTP socket qualification."""
import argparse
import hashlib
import http.client
import http.server
import threading
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

p=argparse.ArgumentParser()
p.add_argument('--host',required=True)
p.add_argument('--peer',required=True)
p.add_argument('--work',required=True)
p.add_argument('--discovery',action='store_true')
a=p.parse_args()
root=Path(tempfile.mkdtemp(prefix='torrent-http-',dir=a.work))
payload=b'K'*16384+b'L'*16384
sha=hashlib.sha1(payload[:16384]).digest()
info=b'd6:lengthi32768e4:name9:movie.mkv12:piece lengthi16384e6:pieces40:'+sha+hashlib.sha1(payload[16384:]).digest()+b'e'
ih=hashlib.sha1(info).hexdigest()
(root/'fixture.torrent').write_bytes(b'd4:info'+info+b'e')
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1',0));peer_port=reserve.getsockname()[1]
peer_log=(root/'peer-process.log').open('w')
peer=subprocess.Popen([sys.executable,a.peer,'--port',str(peer_port),'--label','http','--info-hash',ih,'--log',str(root/'peer.log'),'--file-length','32768','--piece-length','16384','--split-offset','16384','--first-byte','K','--second-byte','L','--hold-all','--release-file',str(root/'release')],stdout=peer_log,stderr=subprocess.STDOUT)
host=None
tracker=None
tracker_requests=[]
if a.discovery:
    class Tracker(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            tracker_requests.append(self.path)
            (root/'tracker.log').write_text("\n".join(tracker_requests))
            raw=b'd8:intervali1800e5:peers6:'+socket.inet_aton('127.0.0.1')+peer_port.to_bytes(2,'big')+b'e'
            self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
        def log_message(self,*args):pass
    tracker=http.server.ThreadingHTTPServer(('127.0.0.1',0),Tracker)
    threading.Thread(target=tracker.serve_forever,daemon=True).start()
host_log=(root/'host.log').open('w')
try:
    deadline=time.monotonic()+5
    while not (root/'peer.log').exists() or 'LISTEN ' not in (root/'peer.log').read_text():
        assert peer.poll() is None and time.monotonic()<deadline, 'peer startup failed'
        time.sleep(.01)
    host=subprocess.Popen([a.host,'--serve','--port','0','--cache',str(root/'cache'),'--torrent',str(root/'fixture.torrent'),*(['--tracker',f'http://127.0.0.1:{tracker.server_port}/announce'] if a.discovery else ['--no-discovery','--peer',f'127.0.0.1:{peer_port}']),'--fixed-interface','--quit-after-ms','6000'],stdout=host_log,stderr=subprocess.STDOUT)
    deadline=time.monotonic()+10
    while 'LISTEN ' not in (root/'host.log').read_text():
        assert host.poll() is None and time.monotonic()<deadline, 'production host does not expose listening torrent composition: '+(root/'host.log').read_text()
        time.sleep(.01)
    port=int(next(line.split()[1] for line in (root/'host.log').read_text().splitlines() if line.startswith('LISTEN ')))
    def get(method='GET',range_=None,path=None):
        conn=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
        conn.request(method,path or '/'+ih+'/0',headers={} if range_ is None else {'Range':range_})
        response=conn.getresponse();body=response.read();headers=dict(response.getheaders());status=response.status;conn.close();return status,headers,body
    # Both responses have headers while the native peer withholds requested blocks.
    # Aborting one must retire its reader without tearing down its sibling engine.
    aborted=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    aborted.request('GET','/'+ih+'/0');aborted_response=aborted.getresponse()
    assert aborted_response.status==200
    live=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    live.request('GET','/'+ih+'/0');live_response=live.getresponse()
    aborted_response.close();aborted.close()
    (root/'release').write_text('release')
    status=live_response.status;headers=dict(live_response.getheaders());body=live_response.read();live.close()
    assert status==200 and body==payload,(status,len(body),headers)
    assert headers['Content-Type']=='video/x-matroska',headers
    status,headers,body=get(path='/'+ih);assert status==200 and body==payload,(status,len(body))
    status,headers,body=get(path='/'+ih+'/999?f='+urllib.parse.quote('/MOVIE/i',safe=''));assert status==200 and body==payload,(status,len(body))
    for query,expected in [('hello+world','hello world'),('hello%2Bworld','hello+world')]:
        status,headers,body=get(path='/'+ih+'/0?subtitles='+query)
        assert status==200 and headers['CaptionInfo.sec']==expected,(status,headers)
    status,headers,body=get(path='/favicon.ico');assert status==404 and headers['Content-Type']=='application/json' and not body
    for query,has_sys in [('?sys=1',True),('?not_sys=10',True),('?sys=0',False)]:
        status,headers,body=get(path='/stats.json'+query);all_stats=json.loads(body)
        assert status==200 and ('sys' in all_stats)==has_sys,all_stats
        if has_sys:assert len(all_stats['sys']['loadavg'])==3 and isinstance(all_stats['sys']['cpus'],list),all_stats
    for header,start,end in [('bytes=0-0',0,0),('bytes=16000-17000',16000,17000),('bytes=-23',32745,32767),('bytes=30000-',30000,32767),('bytes=99-10,2-5',2,5)]:
        status,headers,body=get(range_=header)
        assert status==206 and body==payload[start:end+1],(header,status,len(body))
        assert headers['Content-Range']==f'bytes {start}-{end}/32768',headers
    for header in ['bytes=99999-','malformed','bytes=-99999']:
        status,headers,body=get(range_=header);assert status==200 and body==payload,(header,status,len(body))
    status,headers,body=get('HEAD','bytes=2-5')
    assert status==206 and headers['Content-Length']=='4' and body==b'',(status,headers,body)
    status,headers,body=get(path='/not-a-hash/0');assert status==500 and body==b'',(status,body)
    status,headers,body=get(path='/'+ih+'/999');assert status==500 and body==b'',(status,body)
    for target in ['/'+ih+'/create','/create']:
        conn=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
        data={'fileMustInclude':['/MOVIE/i']} if target!='/create' else {'blob':(root/'fixture.torrent').read_bytes().hex()}
        conn.request('POST',target,json.dumps(data),{'Content-Type':'application/json'})
        response=conn.getresponse();created=json.loads(response.read());conn.close()
        assert response.status==200 and created['infoHash']==ih,created
        if target!='/create':assert created['guessedFileIdx']==0,created
    conn=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    conn.request('POST','/'+ih+'/create',json.dumps({'guessFileIdx':{}}),{'Content-Type':'application/json'})
    response=conn.getresponse();assert response.status==200,(response.status,response.read())
    assert json.loads(response.read())['guessedFileIdx']==0;conn.close()
    class TorrentSource(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            raw=(root/'fixture.torrent').read_bytes()
            self.send_response(404 if self.path=='/notfound' else 200)
            self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
        def log_message(self,*args):pass
    origin=http.server.ThreadingHTTPServer(('127.0.0.1',0),TorrentSource)
    threading.Thread(target=origin.serve_forever,daemon=True).start()
    try:
        for source in [str((root/'fixture.torrent').resolve()),f'http://127.0.0.1:{origin.server_port}/torrent',f'http://127.0.0.1:{origin.server_port}/notfound']:
            conn=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
            conn.request('POST','/create',json.dumps({'from':source}),{'Content-Type':'application/json'})
            response=conn.getresponse();assert response.status==200, (response.status,response.read());assert json.loads(response.read())['infoHash']==ih;conn.close()
    finally:
        origin.shutdown();origin.server_close()
    status,headers,body=get(path='/'+ih+'/0?subtitles=bad%0D%0AInjected%3Ayes');assert status==500 and 'Injected' not in headers
    status,headers,body=get(path='/missing/stats.json');assert status==200 and json.loads(body) is None
    status,headers,body=get(path='/'+ih+'/stats.json');stats=json.loads(body)
    assert status==200 and stats['infoHash']==ih and len(stats['files'])==1 and stats['downloaded']>=32768,stats
    status,headers,body=get(path='/'+ih+'/0/stats.json');stats=json.loads(body)
    assert stats['wires'] is None and stats['streamLen']==32768,stats
    status,headers,body=get(path='/'+ih+'/remove');assert status==200 and json.loads(body)=={}
    status,headers,body=get(path='/'+ih+'/stats.json');assert status==200 and json.loads(body) is None
    status,headers,body=get(path='/'+ih+'/remove');assert status==200 and json.loads(body)=={}
    ledger=(root/'peer.log').read_text()
    assert 'REQUEST ' in ledger, 'HTTP bypassed native wire transport'
    if a.discovery:assert tracker_requests, 'no real tracker announce'
    pending=socket.create_connection(('127.0.0.1',port),timeout=1)
    query='?tr='+urllib.parse.quote(f'tracker:http://127.0.0.1:{tracker.server_port}/announce',safe='') if a.discovery else ''
    pending.sendall(('GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0'+query+' HTTP/1.1\r\nHost: localhost\r\n\r\n').encode())
    pending.settimeout(.1)
    try:
        premature=pending.recv(4096)
        assert False, ('metadata wait emitted bytes or closed early',premature)
    except socket.timeout:
        pass
    if a.discovery:
        deadline=time.monotonic()+1
        while len(tracker_requests)<2 and time.monotonic()<deadline:time.sleep(.01)
        assert any('%AA'*20 in path.upper() for path in tracker_requests), tracker_requests
    abandoned=socket.create_connection(('127.0.0.1',port),timeout=1)
    abandoned.sendall(b'GET /1111111111111111111111111111111111111111/0 HTTP/1.1\r\nHost: localhost\r\n\r\n')
    abandoned.close()
    assert host.wait(timeout=10)==0, (root/'host.log').read_text()
    pending.settimeout(1);assert pending.recv(4096)==b'';pending.close()
    print('PASS: real native torrent HTTP full/range/HEAD/fallback/error/abort-sibling/graceful-shutdown cases; evidence '+str(root))
finally:
    if tracker:tracker.shutdown();tracker.server_close()
    for process in [host,peer]:
        if process is not None and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    host_log.close();peer_log.close()
