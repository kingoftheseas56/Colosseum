#!/usr/bin/env python3
"""QA-only public-static exact lookup. Does not qualify the native UI or alter its gate."""
import argparse, datetime, hashlib, json, pathlib, re, sys, urllib.request, urllib.parse
BASE='https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev'
H=re.compile(r'^[0-9a-f]{64}$')
def sha(b): return hashlib.sha256(b).hexdigest()
def key(method,args): return json.dumps(['comics',method,args],ensure_ascii=False,separators=(',',':'),sort_keys=True)
def sanitize(value):
    if isinstance(value,dict):return {k:('[redacted]' if re.search(r'token|password|secret|authorization|cookie|api.?key',k,re.I) else sanitize(v)) for k,v in value.items()}
    if isinstance(value,list):return [sanitize(x) for x in value]
    if isinstance(value,str):
        def clean(match):
            u=urllib.parse.urlsplit(match.group(0));host=u.hostname or ''
            if u.port:host+=':'+str(u.port)
            return urllib.parse.urlunsplit((u.scheme,host,u.path,'',''))
        return re.sub(r'https?://[^\s\"<>]+',clean,value)
    return value
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs): return None
class Probe:
    def __init__(self):
        self.cache={}; self.calls=[]; self.index={}; self.pub=''
        self.opener=urllib.request.build_opener(NoRedirect())
    def fetch(self,path,expected=None,size=None):
        if path in self.cache:return self.cache[path]
        if len(self.calls)>=10:raise ValueError('REQUEST_BUDGET_EXHAUSTED')
        if not path.startswith('/catalogue/') or '..' in path or '?' in path:raise ValueError('INVALID_PATH')
        receipt={'path':path};self.calls.append(receipt)
        try:
            with self.opener.open(urllib.request.Request(BASE+path,headers={'User-Agent':'Colosseum-Targeted-QA/1'}),timeout=25) as r:
                raw=r.read(25*1024*1024+1);receipt['status']=r.status
            if len(raw)>25*1024*1024:raise ValueError('ASSET_TOO_LARGE')
            receipt.update(bytes=len(raw),sha256=sha(raw))
            if expected and sha(raw)!=expected:raise ValueError('HASH_MISMATCH')
            if size is not None and len(raw)!=size:raise ValueError('SIZE_MISMATCH')
            value=json.loads(raw);self.cache[path]=value;return value
        except Exception as e:
            receipt['error']=type(e).__name__+': '+str(e);raise
    def asset(self,path):
        match=re.fullmatch('/catalogue/'+self.pub+r'/shared/([0-9a-f]{64})\.json',path or '')
        if not match:raise ValueError('INVALID_SHARED_PATH')
        return self.fetch(path,match.group(1))
    def start(self):
        entry=self.fetch('/catalogue/current.json');self.pub=entry.get('revision','')
        ref=entry.get('index',{});digest=ref.get('sha256','')
        if entry.get('schemaVersion')!=1 or not H.fullmatch(self.pub) or not H.fullmatch(entry.get('sourceRevision','')) or not H.fullmatch(digest):raise ValueError('INVALID_ENTRY')
        if ref.get('path')!='/catalogue/'+self.pub+'/index-'+digest+'.json':raise ValueError('INVALID_INDEX_PATH')
        self.index=self.fetch(ref['path'],digest,ref['bytes'])
        if any(self.index.get(k)!=entry.get(k) for k in ['schemaVersion','revision','sourceRevision']) or self.index.get('version')!=1:raise ValueError('INVALID_INDEX')
        return entry
    def lookup(self,method,args):
        k=key(method,args);path=self.index.get('lookups',{}).get(sha(k.encode())[:2])
        result=self.asset(path).get(k) if path else None
        return result,{'method':method,'args':args,'selectionKey':k,'nativeQueryKey':sha(json.dumps({'args':args,'method':method},ensure_ascii=False,separators=(',',':'),sort_keys=True).encode()),'bucketPath':path,'publication':self.pub}
    def first_popular(self):
        k=key('discoverPageFiltered',['popular',[],False]);desc=self.index.get('lists',{}).get(k)
        if desc is None:
            path=self.index.get('listBuckets',{}).get(sha(k.encode())[:2])
            if path:desc=self.asset(path).get(k)
        if not desc:return []
        year=str(datetime.datetime.now(datetime.timezone.utc).year)
        if desc.get('period') not in ('stable',year):raise ValueError('STALE_POPULAR_PERIOD')
        if desc.get('notBefore','')>datetime.datetime.now(datetime.timezone.utc).date().isoformat():raise ValueError('FUTURE_POPULAR_LIST')
        parts=desc.get('parts',[])
        if not parts:return []
        # One first-page part only; never page through the list to hunt examples.
        rows=self.asset(parts[0])
        if not isinstance(rows,list):raise ValueError('INVALID_LIST')
        return rows[:min(24,int(desc.get('chunkSize',24)))]
def validate(result,ident,title):
    if not isinstance(result,dict) or str(result.get('locgId',''))!=ident or result.get('title')!=title or not isinstance(result.get('editions'),list):
        return {'status':'INCOMPLETE','reason':'Missing record, identity/title mismatch, or absent explicit editions array'}
    editions=result['editions']
    if any(not isinstance(e,dict) or not str(e.get('chid','')) or not str(e.get('displayTitle') or e.get('title') or '') for e in editions):
        return {'status':'INCOMPLETE','reason':'Edition rows lack stable identity/title'}
    return {'status':'LIVE_DATA_ESTABLISHED','count':len(editions),'countClass':'zero' if not editions else 'single' if len(editions)==1 else 'multi','result':result}
def run(output):
    p=Probe();report={'scope':'Targeted live published data only; native UI qualification remains separate','nativeGate':'INCOMPLETE','candidates':[]}
    try:
        report['entry']=p.start()
        boys,receipt=p.lookup('curatedByNorm',['boys'])
        candidates=[]
        if isinstance(boys,dict) and boys.get('title')=='The Boys' and isinstance(boys.get('locgId'),str) and boys['locgId']:
            candidates.append((boys['locgId'],'The Boys',receipt))
        else:report['candidates'].append({'title':'The Boys','status':'INCOMPLETE','reason':'No exact current normalized-title identity','lookup':receipt})
        candidates.extend([('gcd-117217','Batman: White Knight',{'source':'run 39129013913 runtime/comics.json visibleCards'}),('gcd-135464','The Batman Who Laughs',{'source':'run 39129013913 runtime/comics.json visibleCards'})])
        for ident,title,origin in candidates:
            result,receipt=p.lookup('curatedSeries',[ident]);report['candidates'].append({'locgId':ident,'title':title,'origin':origin,'lookup':receipt,**validate(result,ident,title)})
        visible=p.first_popular();byid={str(x.get('locgId')):x for x in visible if isinstance(x,dict)}
        for c in report['candidates']:
            match=byid.get(c.get('locgId'))
            c['normalUiRoute']={'catalogue':'comics','catalog':'popular','firstPageCard':'discoverCard_'+c['locgId']} if match and match.get('title')==c['title'] else {'status':'NOT_ESTABLISHED','reason':'Target absent from bounded first-page discovery; do not inject QML navigation or substitute GCD search'}
        observed={c.get('countClass') for c in report['candidates'] if c['status']=='LIVE_DATA_ESTABLISHED'}
        report['liveDataCoverage']='ESTABLISHED' if {'zero','single'}<=observed else 'INCOMPLETE'
        report['missingLiveClasses']=sorted({'zero','single'}-observed)
    except Exception as e:
        report['error']=type(e).__name__+': '+str(e);report['liveDataCoverage']='INCOMPLETE'
    report['requests']=p.calls;report['requestCount']=len(p.calls)
    report['observedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    pathlib.Path(output).write_text(json.dumps(sanitize(report),indent=2)+'\n')
    print(json.dumps({k:report.get(k) for k in ['liveDataCoverage','missingLiveClasses','nativeGate','requestCount','error']}))
    return 0 if report['liveDataCoverage']=='ESTABLISHED' else 2
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args();sys.exit(run(args.output))
