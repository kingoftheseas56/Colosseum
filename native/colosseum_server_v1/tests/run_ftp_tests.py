"""Isolated anonymous loopback FTP fixture. No external FTP or credentials."""
import socketserver, socket, threading, subprocess, argparse, time, tempfile, pathlib
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle');p.add_argument('--node',default='node');a=p.parse_args()
body=bytes(i%251 for i in range(200000))
retr_counts={}
class Handler(socketserver.StreamRequestHandler):
 def handle(self):
  listener=None;offset=0;path=''
  def send(s): self.wfile.write((s+'\r\n').encode());self.wfile.flush()
  try:
   send('220 fixture')
   for raw in self.rfile:
    command,_,arg=raw.decode().strip().partition(' ')
    if command=='USER':send('331 password')
    elif command=='PASS':send('230 welcome')
    elif command=='TYPE':send('200 binary')
    elif command=='MDTM':
     path=arg;send('550 not found' if 'missing' in arg else '500 unsupported' if any(x in arg for x in ('mlsd','unix')) else '213 20260101000000')
    elif command=='SIZE':send('550 size unavailable' if 'size-fail' in arg else '213 '+str(len(body)))
    elif command=='FEAT':send('211-Features\r\n MLST type;size;modify;\r\n'+(' REST\r\n' if 'empty-feature' in path else ' REST STREAM\r\n' if 'noseek' not in path else '')+'211 End')
    elif command in ('EPSV','PASV'):
     if command=='EPSV' and 'pasv' in path:
      send('500 no EPSV');continue
     if listener:listener.close()
     listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();port=listener.getsockname()[1];send(f'229 Entering Extended Passive Mode (|||{port}|)' if command=='EPSV' else f'227 Entering Passive Mode (127,0,0,1,{port//256},{port%256})')
    elif command=='REST':offset=int(arg);send('350 restart')
    elif command in ('MLSD','LIST'):
     if command=='MLSD' and 'list-mlsd' in path:
      send('500 no MLSD');continue
     send('150 opening');conn,_=listener.accept();name=path.rsplit('/',1)[-1]
     listing=(f'-rw-r--r-- 1 owner group 200000 Jan 01 2026 {name}\r\n' if 'unix' in path else f'type=file;size=200000;modify=20260101000000; {name}\r\n')
     conn.sendall(listing.encode());conn.close();listener.close();listener=None;send('226 done')
    elif command=='RETR':
     retr_counts[arg]=retr_counts.get(arg,0)+1
     if arg=='/once.mp4' and retr_counts[arg]>1:
      send('550 repeated transfer');continue
     if 'missing' in arg:
      send('550 not found');continue
     send('150 opening');conn,_=listener.accept()
     if 'slow' in arg:time.sleep(.2)
     try:conn.sendall(body[offset:])
     except (BrokenPipeError,ConnectionResetError):pass
     conn.close();listener.close();listener=None;send('226 done')
    else:send('500 unsupported')
  except (BrokenPipeError,ConnectionResetError):pass
  finally:
   if listener:listener.close()
class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with Server(('127.0.0.1',0),Handler) as server:
 threading.Thread(target=server.serve_forever,daemon=True).start()
 subprocess.run([a.candidate,f'ftp://127.0.0.1:{server.server_address[1]}'],check=True,timeout=30)
 if a.oracle:
  with tempfile.TemporaryDirectory(prefix='ftp-transport-') as temporary:
   golden=str(pathlib.Path(temporary)/'golden.json')
   subprocess.run([a.node,str(pathlib.Path(__file__).with_name('run_ftp_transport_oracle.cjs')),a.oracle,f'ftp://127.0.0.1:{server.server_address[1]}',golden],check=True,timeout=45)
   subprocess.run([a.candidate,'--transport-oracle',f'ftp://127.0.0.1:{server.server_address[1]}',golden],check=True,timeout=45)
 server.shutdown()
