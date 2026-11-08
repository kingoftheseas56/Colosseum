#!/usr/bin/env python3
"""Opt-in metadata-only fixture collection. Never invoked by the offline tests.
Public, unconfigured endpoints only; no accounts, playback, or media downloads.
"""
import concurrent.futures, datetime, hashlib, json, pathlib, re, subprocess, tempfile, urllib.parse
ROOT=pathlib.Path(__file__).resolve().parent
CASES=[
 ('inception','movie','tt1375666','Inception'),
 ('matrix','movie','tt0133093','The Matrix'),
 ('shawshank','movie','tt0111161','The Shawshank Redemption'),
 ('godfather','movie','tt0068646','The Godfather'),
 ('dark-knight','movie','tt0468569','The Dark Knight'),
 ('interstellar','movie','tt0816692','Interstellar'),
 ('parasite','movie','tt6751668','Parasite'),
 ('spirited-away','movie','tt0245429','Spirited Away'),
 ('dune','movie','tt1160419','Dune'),
 ('clayface','movie','tt34890576','Clayface'),
 ('one-piece','series','tt0388629','One Piece'),
 ('naruto','series','tt0409591','Naruto'),
 ('attack-on-titan','series','tt2560140','Attack on Titan'),
 ('death-note','series','tt0877057','Death Note'),
 ('fullmetal-alchemist','series','tt1355642','Fullmetal Alchemist: Brotherhood'),
 ('demon-slayer','series','tt9335498','Demon Slayer: Kimetsu no Yaiba'),
 ('jujutsu-kaisen','series','tt12343534','Jujutsu Kaisen'),
 ('cowboy-bebop','series','tt0213338','Cowboy Bebop'),
 ('frieren','series','tt22248376',"Frieren: Beyond Journey's End"),
 ('steins-gate','series','tt1910272','Steins;Gate'),
]
SECRET_KEYS=re.compile(r'(?:token|api[_-]?key|password|secret|authorization|signature|access[_-]?key)', re.I)

def scrub(value, key=''):
    if SECRET_KEYS.search(key):
        return '<redacted>'
    if isinstance(value,dict): return {k:scrub(v,k) for k,v in value.items()}
    if isinstance(value,list): return [scrub(v,key) for v in value]
    if isinstance(value,str) and value.startswith(('http://','https://')):
        p=urllib.parse.urlsplit(value)
        # Authentication-bearing URL query/userinfo is not needed by parser tests.
        q=urllib.parse.parse_qsl(p.query,keep_blank_values=True)
        q=[(k,'REDACTED' if SECRET_KEYS.search(k) else v) for k,v in q]
        host=p.hostname or ''
        if p.port: host+=':'+str(p.port)
        return urllib.parse.urlunsplit((p.scheme,host,p.path,urllib.parse.urlencode(q),''))
    return value

def fetch(url):
    now=datetime.datetime.now(datetime.timezone.utc).isoformat()
    # One bounded request. A timeout/error remains evidence, never a fabricated empty response.
    p=subprocess.run(['curl','-sS','-L','--connect-timeout','10','--max-time','25',url],capture_output=True)
    rec={'endpoint':url,'retrieved_at':now,'curl_exit':p.returncode,'raw_sha256':hashlib.sha256(p.stdout).hexdigest(),'raw_bytes':len(p.stdout)}
    if p.returncode:
        rec['error']=p.stderr.decode(errors='replace').strip(); return None,rec
    try: return json.loads(p.stdout),rec
    except Exception:
        rec['error']='Non-JSON response'; rec['response_prefix']=p.stdout[:160].decode(errors='replace'); return None,rec

def save(name,obj):
    (ROOT/name).write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n')

def collect(case):
    slug,kind,iid,title=case; req_id=iid+(':1:1' if kind=='series' else '')
    response,source=fetch(f'https://torrentio.strem.fun/stream/{kind}/{req_id}.json')
    meta,metadata_source=fetch(f'https://v3-cinemeta.strem.io/meta/{kind}/{iid}.json')
    context={'kind':kind,'imdb_id':iid,'expected_title':title,'is_anime':kind=='series','today':'2026-11-08','preferred_languages':['en'] if kind=='movie' else ['ja','en']}
    if kind=='series': context.update(expected_season=1,expected_episode=1)
    if isinstance(meta,dict) and isinstance(meta.get('meta'),dict):
        m=meta['meta']; context['expected_title']=m.get('name',title); context['release_date']=m.get('released'); context['release_info']=m.get('releaseInfo'); context['runtime_text']=m.get('runtime')
        rm=re.search(r'\d+',str(m.get('runtime','')))
        if rm: context['runtime_minutes']=int(rm.group())
        year=re.match(r'\d{4}',str(m.get('releaseInfo','')))
        if year: context['expected_year']=int(year.group())
        if kind=='series':
            ep=next((v for v in m.get('videos',[]) if v.get('season')==1 and v.get('episode')==1),None)
            if ep: context['episode_metadata']={k:ep.get(k) for k in ('id','title','season','episode','released')}
    if slug=='clayface':
        # Official distributor overrides stale scheduled dates in third-party metadata.
        context['release_date']='2026-10-23'
        context['release_date_source']='https://www.warnerbros.com/movies/clayface'
        context['release_date_source_excerpt']='Theatrical Release October 23, 2026; released only in theaters in North America on October 23, 2026, and internationally beginning 21 October 2026.'
        context['release_date_region']='North America'
    rec={'id':slug,'title':context['expected_title'],'kind':kind,'imdbId':iid,'season':1 if kind=='series' else None,'episode':1 if kind=='series' else None,'isAnime':kind=='series','releaseDate':(context.get('release_date') or '')[:10] or None,'runtimeMinutes':context.get('runtime_minutes'),'expectedTitles':list(dict.fromkeys([context['expected_title'],title])),'requestId':req_id,'provider':'Torrentio','source':source,'metadataSource':metadata_source,'context':context,'humanPick':None,'agentPick':None,'humanReviewStatus':'pending: no human-reviewed label was supplied; agent output is not a human label'}
    if slug=='fullmetal-alchemist':
        rec['runtimeMinutes']=None
        rec['runtimeContextNote']='Cinemeta reports 54 min for the series. The requested episode runtime was not verified, so scoring runtime is deliberately unknown rather than assuming this series-level value is correct.'
    if isinstance(response,dict) and isinstance(response.get('streams'),list):
        filename=f'{slug}.torrentio.json'; clean=scrub(response);save(filename,clean)
        rec['responseFile']=filename;rec['stream_count']=len(clean['streams']);rec['scrubbed_sha256']=hashlib.sha256((ROOT/filename).read_bytes()).hexdigest()
        rec['scrubbing_changed_values']=clean!=response
    else: rec['stream_count']=None
    print(slug,rec.get('stream_count'),source.get('error','OK'),flush=True)
    return rec

if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        records=list(pool.map(collect,CASES))
    save('manifest.json',{'schema':1,'as_of':'2026-11-08','scope':'Metadata-only actual public add-on responses; offline test fixtures. No playback verified.','fixtures':records})
