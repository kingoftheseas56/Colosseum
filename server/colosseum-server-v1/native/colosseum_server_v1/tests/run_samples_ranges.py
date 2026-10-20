#!/usr/bin/env python3
import argparse,json,pathlib,subprocess,threading,time,http.server,struct
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out).resolve();fixtures=out/'fixtures'
# Build virtual 4GiB mdat, without allocating or scanning its payload.
raw=(fixtures/'tail.mp4').read_bytes();pos=0;moov=None
while pos<len(raw):
 size=struct.unpack_from('>I',raw,pos)[0]
 if raw[pos+4:pos+8]==b'moov':moov=raw[pos:pos+size];break
 pos+=size
large_offset=(1<<32)+128
sources={'large64.mp4':(large_offset+len(moov),[(0,struct.pack('>I4sQ',1,b'mdat',large_offset)),(large_offset,moov)])}
for name in ['fast.mp4','tail.mp4','fragmented.mp4','real_mkv.mkv','mkv_cues_after.mkv','mkv_cues_before.mkv','mkv_unknown_duration.mkv','mkv_truncated_128.mkv','mkv_truncated_512.mkv','mkv_truncated_50000.mkv']:
 b=(fixtures/name).read_bytes();sources[name]=(len(b),[(0,b)])
records=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  label,name=self.path.strip('/').split('/',1);name=name.split('?')[0]
  r=self.headers.get('Range');records.append({'label':label,'name':name,'range':r})
  start=int(r.split('=')[1].split('-')[0]) if r else 0;size,parts=sources[name]
  if label=='delay':time.sleep(.3)
  self.send_response(206);self.send_header('Content-Length',str(max(0,size-start)));self.send_header('Content-Range',f'bytes {start}-{size-1}/{size}');self.end_headers()
  try:
   # Limit a misbehaving client rather than actually streaming 4GiB zeros.
   stop=min(size,start+1024*1024)
   for pos in range(start,stop,4096):
    end=min(stop,pos+4096);b=bytearray(end-pos)
    for off,data in parts:
     left=max(pos,off);right=min(end,off+len(data))
     if right>left:b[left-pos:right-pos]=data[left-off:right-off]
    self.wfile.write(b);self.wfile.flush();time.sleep(.02)
  except (BrokenPipeError,ConnectionResetError):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();base=f'http://127.0.0.1:{server.server_port}'
cases=[{'name':name,'mode':'mkv' if name.endswith('mkv') else 'mp4','file':base+'/oracle/'+name} for name in sources]
casefile=out/'ranges-cases.json';casefile.write_text(json.dumps(cases));script=pathlib.Path(__file__).with_name('run_samples_oracle.cjs')
proc=subprocess.run(['node',str(script),a.oracle,str(casefile)],capture_output=True,timeout=40);(out/'ranges-oracle.jsonl').write_bytes(proc.stdout);(out/'ranges-oracle.stderr').write_bytes(proc.stderr)
expected={x['name']:x['result'] for x in map(json.loads,proc.stdout.splitlines())}
rows=[]
for c in cases:
 cp=subprocess.run([a.candidate,'async-'+c['mode'],base+'/candidate/'+c['name']],capture_output=True,timeout=15)
 actual=json.loads(cp.stdout);candidate_result={k:actual[k] for k in ['tracks','error','available']}
 source_ranges=[r['range'] for r in records if r['label']=='oracle' and r['name']==c['name']];candidate_ranges=[r['range'] for r in records if r['label']=='candidate' and r['name']==c['name']]
 row={'name':c['name'],'passed':candidate_result==expected[c['name']] and source_ranges==candidate_ranges,'source_ranges':source_ranges,'candidate_ranges':candidate_ranges,'candidate':actual,'oracle':expected[c['name']]};rows.append(row)
 print(c['name'],row['passed'],source_ranges,candidate_ranges,'bytes',actual['bytesFetched'])
for mode,name in [('mp4','tail.mp4'),('mkv','real_mkv.mkv')]:
 cp=subprocess.run([a.candidate,'cancel-'+mode,base+'/delay/'+name],capture_output=True,timeout=3)
 passed=cp.returncode==0 and json.loads(cp.stdout)=={'cancelled':True};rows.append({'name':'cancel_'+mode,'passed':passed});print('cancel_'+mode,passed)
server.shutdown();(out/'ranges-comparison.json').write_text(json.dumps(rows,indent=2));(out/'ranges-requests.json').write_text(json.dumps(records,indent=2))
raise SystemExit(not all(r['passed'] for r in rows))
