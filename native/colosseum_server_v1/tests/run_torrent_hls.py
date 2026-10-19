"""Real tracker-to-torrent-to-HLS HTTP composition and decode qualification."""
import subprocess,tempfile,pathlib,http.client,http.server,threading,urllib.parse,hashlib,socket,time,sys,importlib.util,argparse,struct
parser=argparse.ArgumentParser();parser.add_argument('--host',required=True);parser.add_argument('--peer',required=True);parser.add_argument('--media',required=True);parser.add_argument('--work',required=True);args=parser.parse_args()
work=pathlib.Path(tempfile.mkdtemp(prefix='torrent-hls-',dir=args.work));peerpath=pathlib.Path(args.peer).resolve();original=pathlib.Path(args.media).read_bytes();padding=bytes((i*17+3)%256 for i in range(50000));payload=original+struct.pack('>I',len(padding)+8)+b'free'+padding;media=work/'payload.mp4';media.write_bytes(payload);spec=importlib.util.spec_from_file_location('peer',peerpath);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
info=module.bencode({b'length':len(payload),b'name':b'movie.mp4',b'piece length':16384,b'pieces':b''.join(hashlib.sha1(payload[i:i+16384]).digest() for i in range(0,len(payload),16384))});ih=hashlib.sha1(info).hexdigest();(work/'fixture.torrent').write_bytes(b'd4:info'+info+b'e')
wrapper=work/'peer.py';wrapper.write_text('import importlib.util,pathlib\ns=importlib.util.spec_from_file_location("peer",'+repr(str(peerpath))+');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)\ndata=pathlib.Path('+repr(str(media))+').read_bytes()\ndef block(self,piece,start,length):\n offset=piece*self.piece_length+start\n return data[offset:offset+length]\nm.TinyPeer.block_for=block\nm.main()\n')
with socket.socket() as s:s.bind(('127.0.0.1',0));peerport=s.getsockname()[1]
peer=subprocess.Popen([sys.executable,str(wrapper),'--port',str(peerport),'--label','hls','--info-hash',ih,'--log',str(work/'peer.log'),'--file-length',str(len(payload))],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
class Tracker(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  (work/'tracker.log').write_text(self.path);raw=b'd8:intervali1800e5:peers6:'+socket.inet_aton('127.0.0.1')+peerport.to_bytes(2,'big')+b'e';self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
 def log_message(self,*args):pass
tracker=http.server.ThreadingHTTPServer(('127.0.0.1',0),Tracker);threading.Thread(target=tracker.serve_forever,daemon=True).start();log=open(work/'host.log','w');host=None
try:
 end=time.monotonic()+5
 while not (work/'peer.log').exists():assert time.monotonic()<end;time.sleep(.01)
 host=subprocess.Popen([args.host,'--serve','--port','0','--fixed-interface','--cache',str(work/'cache'),'--torrent',str(work/'fixture.torrent'),'--tracker',f'http://127.0.0.1:{tracker.server_port}/announce','--quit-after-ms','30000'],stdout=log,stderr=subprocess.STDOUT)
 while 'LISTEN ' not in (work/'host.log').read_text():assert host.poll() is None and time.monotonic()<end;time.sleep(.01)
 port=int(next(l.split()[1] for l in (work/'host.log').read_text().splitlines() if l.startswith('LISTEN ')))
 def req(path):
  c=http.client.HTTPConnection('127.0.0.1',port,timeout=15);c.request('GET',path);r=c.getresponse();body=r.read();status=r.status;c.close();assert status==200 and body,(path,status,body);return body
 for suffix,name in [('hls.m3u8','master.m3u8'),('video0.m3u8','video0.m3u8'),('video0/init.mp4','init.mp4'),('video0/segment1.m4s','segment1.m4s')]:
  body=req('/'+ih+'/0/'+suffix);(work/name).write_bytes(body)
 assert req('/'+ih+'/0')==payload
 assert (work/'tracker.log').exists() and 'REQUEST ' in (work/'peer.log').read_text()
 (work/'joined.mp4').write_bytes((work/'init.mp4').read_bytes()+(work/'segment1.m4s').read_bytes());subprocess.run(['ffmpeg','-v','error','-i',str(work/'joined.mp4'),'-f','null','-'],check=True,timeout=10)
 print('PASS tracker-acquired peer -> native verified torrent -> HTTP -> ffprobe/ffmpeg -> HLS init+segment -> decode; lazy alias actual port',work)
finally:
 if host:host.terminate();host.wait(timeout=5)
 peer.terminate();peer.wait(timeout=5);tracker.shutdown();log.close()
