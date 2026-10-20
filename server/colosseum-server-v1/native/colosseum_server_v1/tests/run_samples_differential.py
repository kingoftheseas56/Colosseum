#!/usr/bin/env python3
"""Generate controlled real/synthetic corpus and compare locked factories 877/868/918."""
import argparse, hashlib, json, pathlib, struct, subprocess
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle',required=True);p.add_argument('--out',required=True);a=p.parse_args()
out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True);fixtures=out/'fixtures';fixtures.mkdir(exist_ok=True)
cases=[]
def save(name,mode,data,indexes=None):
 f=fixtures/(name+('.json' if mode=='group' else '.'+mode));f.write_bytes(data)
 c=dict(name=name,mode=mode,file=str(f));
 if indexes is not None:c['indexes']=indexes
 cases.append(c);return f

def command(args):return subprocess.run(args,check=True,capture_output=True).stdout
real=fixtures/'tail.mp4'
command(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=64x64:rate=25','-f','lavfi','-i','sine=frequency=440','-t','3','-c:v','libx264','-g','25','-bf','2','-c:a','aac','-y',str(real)])
cases.append(dict(name='tail',mode='mp4',file=str(real)))
for name,flags in [('fast',['-movflags','+faststart']),('fragmented',['-movflags','+frag_keyframe+empty_moov']),('real_mkv',[])]:
 mode='mkv' if name=='real_mkv' else 'mp4';f=fixtures/(name+'.'+mode)
 command(['ffmpeg','-hide_banner','-loglevel','error','-i',str(real),'-c','copy',*flags,'-y',str(f)])
 cases.append(dict(name=name,mode=mode,file=str(f)))
for n in [128,512,50000]:save('mkv_truncated_'+str(n),'mkv',(fixtures/'real_mkv.mkv').read_bytes()[:n])
def boxes(data):
 p=0
 while p<len(data):
  size,kind=struct.unpack_from('>I4s',data,p);yield kind,data[p+8:p+size];p+=size

def box(kind,data):return struct.pack('>I4s',len(data)+8,kind)+data
moov=next(data for kind,data in boxes(real.read_bytes()) if kind==b'moov')
parents={b'trak',b'mdia',b'minf',b'stbl',b'edts'}
def change(data,target,fn):
 return b''.join(box(k,change(v,target,fn) if k in parents else fn(v) if k==target else v) for k,v in boxes(data) if not(k==target and fn is None))
save('moov_only','mp4',box(b'moov',moov))
save('filter_audio','mp4',box(b'moov',moov),[1])
save('filter_absent','mp4',box(b'moov',moov),[99])
for name,target,fn in [('no_mvhd',b'mvhd',None),('no_mdhd',b'mdhd',None),('no_hdlr',b'hdlr',None),('no_stts',b'stts',None),('all_sync',b'stss',None),('no_sync',b'stss',lambda _:bytes(8)),('truncated_stts',b'stts',lambda b:b[:10]),('negative_ctts',b'ctts',lambda b:b'\1'+b[1:12]+struct.pack('>i',-512)+b[16:])]:
 save(name,'mp4',box(b'moov',change(moov,target,fn)))
# Empty/absent-moov full source streams never settle after EOF in readChunk.
# These are exercised separately as bounded fallback assertions, not exact success.
save('large_moov','mp4',struct.pack('>I4sQ',1,b'moov',len(moov)+16)+moov)
# EBML includes exact source traversal behavior: seek order, first CueTrackPositions only.
def vint(n):
 for k in range(1,9):
  if n<(1<<(7*k))-1:return ((1<<(7*k))|n).to_bytes(k,'big')
 raise ValueError(n)
def tag(i,d):return bytes.fromhex(i)+vint(len(d))+d
def uint(i,n):return tag(i,n.to_bytes(max(1,(n.bit_length()+7)//8),'big'))
ebml_header=(fixtures/'real_mkv.mkv').read_bytes()[:40]
def mkv(name,cues=True,scale=True,unknown=False,first_text=False,reverse=False,invalid_seek=False):
 info=tag('1549a966',uint('2ad7b1',1000000) if scale else b'')
 entries=[]
 for number,typ,duration in ([(7,17,0)] if first_text else [])+[(1,1,40000000),(2,2,0)]:
  entries.append(tag('ae',uint('d7',number)+uint('83',typ)+(uint('23e383',duration) if duration else b'')))
 tracks=tag('1654ae6b',b''.join(entries))
 cuepoints=[]
 for time in [2000,0,1000]:
  cuepoints.append(tag('bb',uint('b3',time)+tag('b7',uint('f7',1)+uint('f1',123))+tag('b7',uint('f7',2))))
 cue=tag('1c53bb6b',b''.join(cuepoints) if cues else b'')
 parts=[('1549a966',info),('1654ae6b',tracks),('1f43b675',tag('1f43b675',bytes(100000))),('1c53bb6b',cue)]
 if reverse:parts=[parts[3],*parts[:3]]
 def head(offsets):return tag('114d9b74',b''.join(tag('4dbb',tag('53ab',bytes.fromhex(k))+tag('53ac',offsets[k].to_bytes(8,'big'))) for k in ['1c53bb6b','1654ae6b','1549a966']))
 offsets={k:0 for k,_ in parts};pos=len(head(offsets))
 for k,v in parts:offsets[k]=pos;pos+=len(v)
 if invalid_seek:offsets['1c53bb6b']=1<<40
 content=head(offsets)+b''.join(v for _,v in parts)
 data=ebml_header+bytes.fromhex('18538067')+(bytes.fromhex('01ffffffffffffff') if unknown else vint(len(content)))+content
 return save(name,'mkv',data)
mkv('mkv_invalid_seek',invalid_seek=True);mkv('mkv_cues_after');mkv('mkv_cues_before',reverse=True);mkv('mkv_unknown_duration',unknown=True);mkv('mkv_empty_cues',cues=False);mkv('mkv_missing_scale',scale=False);mkv('mkv_text_first',first_text=True)
save('mkv_no_seekhead','mkv',ebml_header+tag('18538067',tag('1549a966',uint('2ad7b1',1000000))+tag('1654ae6b',b'')))
save('mkv_missing_segment','mkv',tag('1a45dfa3',b''))
for typ in ['video','audio']:
 for times in [[0,1000,2000,3000,4000,5000],[0,999,1000,2000,5000],[0,5,10,15,2000],[2000,1000,1000,5000]]:
  options={'stream':{'track':typ,'timescale':1000},'format':{'duration':10},'timescale':90000,'segmentDuration':2,'sampleDuration':40,'samples':[{'pts':t,'key':True} for t in times]+[{'pts':9999,'key':False}]}
  save('group_'+typ+'_'+str(len(cases)),'group',json.dumps(options).encode())
for duration in [None,'40','missing']:
 options={'stream':{'track':'video','timescale':1000},'timescale':90000,'segmentDuration':2,'samples':[{'pts':0,'key':True},{'pts':1000,'key':True}]}
 if duration!='missing':options['sampleDuration']=duration
 save('group_nullable_'+str(duration),'group',json.dumps(options).encode())
save('bundled_codec_samples','bundled',b'')
(out/'cases.json').write_text(json.dumps(cases,indent=2))
script=pathlib.Path(__file__).with_name('run_samples_oracle.cjs')
proc=subprocess.run(['node',str(script),a.oracle,str(out/'cases.json')],capture_output=True,timeout=60)
(out/'oracle.jsonl').write_bytes(proc.stdout);(out/'oracle.stderr').write_bytes(proc.stderr)
if proc.returncode:raise RuntimeError(proc.stderr.decode())
oracle={x['name']:x['result'] for x in map(json.loads,proc.stdout.splitlines())}
rows=[];failures=[]
for c in cases:
 args=[a.candidate,c['mode'],c['file']]
 if c.get('indexes'):args.append(','.join(map(str,c['indexes'])))
 result=json.loads(command(args));expected=oracle[c['name']];ok=result==expected
 rows.append(dict(name=c['name'],passed=ok,candidate=result,oracle=expected))
 if not ok:failures.append(c['name']);print(c['name'],'MISMATCH',str(result)[:300],str(expected)[:300])
(out/'comparison.json').write_text(json.dumps(rows,indent=2))
(out/'fixture-hashes.json').write_text(json.dumps({f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in fixtures.iterdir()},indent=2))
print(f'{len(cases)-len(failures)}/{len(cases)} exact comparisons passed')
raise SystemExit(bool(failures))
