"""Offline controlled fixtures only; no socket or live coverage."""
import importlib.util,json,tempfile,pathlib
spec=importlib.util.spec_from_file_location('probe',str(pathlib.Path(__file__).with_name('probe.py')))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
pub='a'*64;assets={};buckets={}
def put(value):
 raw=json.dumps(value,separators=(',',':')).encode();path='/catalogue/'+pub+'/shared/'+m.sha(raw)+'.json';assets[path]=raw;return path
for ident,title,eds in [('boys-current','The Boys',[]),('gcd-117217','Batman: White Knight',[{'chid':'controlled-1','title':'Controlled fixture edition'}]),('gcd-135464','The Batman Who Laughs',[{'chid':'controlled-2','title':'A'},{'chid':'controlled-3','title':'B'}])]:
 k=m.key('curatedSeries',[ident]);buckets.setdefault(m.sha(k.encode())[:2],{})[k]={'locgId':ident,'title':title,'editions':eds}
k=m.key('curatedByNorm',['boys']);buckets.setdefault(m.sha(k.encode())[:2],{})[k]={'locgId':'boys-current','title':'The Boys'}
page=[{'locgId':'boys-current','title':'The Boys'},{'locgId':'gcd-117217','title':'Batman: White Knight'}]
k=m.key('discoverPageFiltered',['popular',[],False]);lp=put({k:{'parts':[put(page)],'chunkSize':24,'period':'stable'}})
idx={'schemaVersion':1,'version':1,'revision':pub,'sourceRevision':'b'*64,'lists':{},'fixed':{},'search':{},'lookups':{k:put(v) for k,v in buckets.items()},'listBuckets':{m.sha(k.encode())[:2]:lp}}
raw=json.dumps(idx).encode();ip='/catalogue/'+pub+'/index-'+m.sha(raw)+'.json';assets[ip]=raw;assets['/catalogue/current.json']=json.dumps({'schemaVersion':1,'revision':pub,'sourceRevision':'b'*64,'index':{'path':ip,'sha256':m.sha(raw),'bytes':len(raw)}}).encode()
class Response:
 status=200
 def __init__(self,raw):self.raw=raw
 def read(self,n):return self.raw[:n]
 def __enter__(self):return self
 def __exit__(self,*a):pass
class FakeOpener:
 def open(self,req,timeout):return Response(assets[req.full_url.removeprefix(m.BASE)])
m.urllib.request.build_opener=lambda *a:FakeOpener()
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d)/'fixture.json';assert m.run(p)==0;r=json.loads(p.read_text());assert r['nativeGate']=='INCOMPLETE';assert r['requestCount']<=8
 assert r['candidates'][0]['countClass']=='zero';assert r['candidates'][1]['countClass']=='single'
 assert r['candidates'][2]['normalUiRoute']['status']=='NOT_ESTABLISHED'
 assets[ip]=b'{}';assert m.run(p)==2;assert 'HASH_MISMATCH' in json.loads(p.read_text())['error']
for result in [{},{'locgId':'id','title':'Title'},{'locgId':'other','title':'Title','editions':[]},{'locgId':'id','title':'Title','editions':[{}]}]:assert m.validate(result,'id','Title')['status']=='INCOMPLETE'
print('PASS: controlled offline lookup/route/hash/identity tests; no live data or native UI coverage')
