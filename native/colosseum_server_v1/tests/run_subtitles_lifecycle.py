import http.server,threading,subprocess,json,sys,gzip,zipfile,io,urllib.parse,pathlib,struct,tempfile
exe=sys.argv[1]; media=pathlib.Path(sys.argv[2]);out=pathlib.Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
srt=b'1\n00:00:01,000 --> 00:00:02,300\nHello & world\n\n'; zipped=io.BytesIO()
with zipfile.ZipFile(zipped,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('notes.txt','skip');z.writestr('first.srt',srt);z.writestr('second.srt',b'wrong')
empty=io.BytesIO()
with zipfile.ZipFile(empty,'w') as z:z.writestr('upper.SRT',srt)
mp4=(media/'tiny.mp4').read_bytes();mkv=(media/'tiny.mkv').read_bytes();pos=0;boxes={}
while pos<len(mp4):
 size=struct.unpack('>I',mp4[pos:pos+4])[0];boxes[mp4[pos+4:pos+8]]=mp4[pos:pos+size];pos+=size
late=boxes[b'ftyp']+struct.pack('>I',100000)+b'mdat'+bytes(99992)+boxes[b'moov'];movie=bytes((i*13+7)%256 for i in range(150000));requests=[]
data={'/plain.srt':srt,'/compressed.gz':gzip.compress(srt),'/concat.gz':gzip.compress(srt[:15])+gzip.compress(srt[15:]),'/archive.zip':zipped.getvalue(),'/no.zip':empty.getvalue(),'/bad.gz':b'bad','/movie':movie,'/tiny.mp4':mp4,'/tiny.mkv':mkv,'/late.mp4':late,'/empty.mp4':boxes[b'ftyp']+struct.pack('>I',8)+b'moov','/truncated.mp4':boxes[b'ftyp']+struct.pack('>I',100)+b'moov','/bad.bin':b'garbage'}
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_HEAD(self):self.serve(True)
 def do_GET(self):self.serve(False)
 def serve(self,head):
  requests.append({'method':self.command,'path':self.path,'range':self.headers.get('range'),'prio':self.headers.get('enginefs-prio')})
  if self.path=='/redirect':self.send_response(302);self.send_header('Location','/plain.srt');self.end_headers();return
  if self.path=='/loop':self.send_response(302);self.send_header('Location','/loop');self.end_headers();return
  b=data.get(self.path,b'not found');status=200 if self.path in data else 404;rg=self.headers.get('range');contentrange=None
  if rg:
   a,z=map(int,rg[6:].split('-'));z=min(z,len(b)-1);contentrange=f'bytes {a}-{z}/{len(b)}';b=b[a:z+1];status=206
  self.send_response(status);self.send_header('Content-Length',str(len(b)))
  if contentrange:self.send_header('Content-Range',contentrange)
  self.end_headers()
  if not head:
   try:self.wfile.write(b)
   except BrokenPipeError:pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();base='http://127.0.0.1:'+str(server.server_port);rows=[]
def run(mode,url,*args):
 start=len(requests);p=subprocess.run([exe,mode,url,*args],text=True,capture_output=True,timeout=15);assert p.returncode==0,(mode,url,p.returncode,p.stdout,p.stderr);r=json.loads(p.stdout);rows.append({'mode':mode,'url':url,'args':args,'result':r,'requests':requests[start:]});return r
for p in ['/plain.srt','/compressed.gz','/concat.gz','/archive.zip','/redirect']:assert run('fetch',base+p)['text']==srt.decode()
assert run('fetch',base+'/no.zip')['error'].startswith('no srt found')
assert run('fetch',base+'/bad.gz')['error'];assert run('fetch',base+'/loop')['error']=='Too many redirects'
assert run('fetch',base+'/plain.srt','bad-charset')['error'].startswith('Encoding not recognized')
assert run('agent',base+'/plain.srt')['text']==srt.decode()
h=run('hash',base+'/movie');assert h['error']=='' and len(rows[-1]['requests'])==3
assert rows[-1]['requests'][1]['range']=='bytes=0-65535';assert rows[-1]['requests'][2]['range']=='bytes=84464-149999';assert rows[-1]['requests'][2]['prio']=='10'
for p in ['/tiny.mp4','/tiny.mkv','/late.mp4']:assert len(run('tracks',base+p)['tracks'])==2
assert len(rows[-1]['requests'])==2
assert run('tracks',base+'/empty.mp4')=={'error':'','incomplete':False,'tracks':[]}
assert run('tracks',base+'/truncated.mp4')['incomplete'];assert run('tracks',base+'/bad.bin')['error'];assert run('tracks',base+'/tiny.mp4','1')['error']=='Reached maxBytesLimit of 1'
assert run('cancel',base+'/tiny.mp4')['cancelled']
q=urllib.parse.quote(base+'/plain.srt',safe='');r=run('route','/subtitlesTracks?subsUrl='+q);assert r['status']==200 and json.loads(r['body'])['result']['tracks'][0]['text']=='Hello & world'
r=run('route','/subtitles.vtt?from='+q+'&offset=1000');assert r['status']==200 and r['body']=='WEBVTT\n\n0\n00:00:02.000 --> 00:00:03.300\nHello &amp; world\n\n'
r=run('route','/opensubHash?videoUrl='+urllib.parse.quote(base+'/movie',safe=''));assert r['status']==200 and json.loads(r['body'])['result']==h['result']
r=run('route','/tracks/'+urllib.parse.quote(base+'/late.mp4',safe=''));assert r['status']==200 and len(json.loads(r['body']))==2
assert run('route-abort','/subtitlesTracks?subsUrl='+q)['cancelled']
if len(sys.argv)>4:
 oracle=pathlib.Path(__file__).with_name('subtitles_oracle_cli.cjs'); bundle=sys.argv[4]
 for mode,path in [('fetch','/plain.srt'),('fetch','/compressed.gz'),('fetch','/concat.gz'),('fetch','/archive.zip'),('fetch','/no.zip'),('fetch','/redirect'),('hash','/movie'),('tracks','/tiny.mp4'),('tracks','/tiny.mkv'),('tracks','/late.mp4'),('tracks','/empty.mp4')]:
  native=run(mode,base+path); start=len(requests);p=subprocess.run(['node',str(oracle),bundle,mode,base+path],text=True,capture_output=True,timeout=20)
  assert p.returncode==0,(mode,path,p.stdout,p.stderr)
  expected=json.loads(p.stdout); assert native==expected,(mode,path,native,expected)
  assert rows[-1]['requests']==requests[start:],(mode,path,rows[-1]['requests'],requests[start:])
  rows[-1]['oracle']=expected;rows[-1]['oracleRequests']=requests[start:]
 if len(sys.argv)>4:
  with tempfile.TemporaryDirectory(prefix='m02-hashes-') as temp:
   for size in [65535,65536,65537,131071,131072,131073]:
    file=pathlib.Path(temp)/str(size);file.write_bytes(bytes((i*13+7)%256 for i in range(size)));url=file.as_uri();native=run('hash',url);p=subprocess.run(['node',str(oracle),bundle,'hash',url],text=True,capture_output=True,timeout=20);assert p.returncode==0,p.stderr;expected=json.loads(p.stdout);assert native==expected,(size,native,expected);rows[-1]['oracle']=expected
(out/'lifecycle-cases.json').write_text(json.dumps(rows,indent=2));server.shutdown();print(json.dumps({'cases':len(rows),'passed':True}))
