"""Loopback-only development host; no deployment or filesystem bridge."""
import argparse, json, re, threading, time
import urllib.error, urllib.request
from urllib.parse import urlsplit, parse_qs
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
HOST = 'https://colosseum-content-api.colosseum-watchparty-relay.workers.dev'
TABS = {'discover': {}, 'movies': {'type':'movie'}, 'shows': {'type':'series','excludeAnime':True}}
API = '/api/v1'
VERSION = 'arc54-1'
lock = threading.Lock()
guest = ('', 0.0)

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None

opener = urllib.request.build_opener(NoRedirect)
def worker(path, payload, token=None):
    headers = {'Content-Type':'application/json','Accept':'application/json',
               'User-Agent':'ColosseumWebArc54/1.0 (https://getcolosseum.world)'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request(HOST+path, json.dumps(payload).encode(), headers=headers, method='POST')
    with opener.open(req,timeout=13) as res:
        raw = res.read(500001)
        if len(raw)>500000:
            raise ValueError('oversize')
        return json.loads(raw)

def lease():
    global guest
    with lock:
        if guest[1] < time.monotonic():
            token = worker('/v1/guest/session',{'appId':'colosseum','apiVersion':1}).get('token')
            if not isinstance(token,str) or not token or len(token)>2048:
                raise ValueError('missing lease')
            guest = (token,time.monotonic()+240)
        return guest[0]

class Handler(SimpleHTTPRequestHandler):
    def send_json(self, code, obj):
        raw=json.dumps(obj,ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header('Content-Type','application/json; charset=utf-8')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Length',str(len(raw)))
        self.send_header('X-Content-Type-Options','nosniff')
        self.end_headers()
        try: self.wfile.write(raw)
        except (BrokenPipeError,ConnectionResetError): pass

    def do_GET(self):
        url=urlsplit(self.path)
        path=url.path
        if not path.startswith('/api/'):
            # Only frontend files and the named shared assets are exposed.
            if path.startswith('/web/arc54/') and re.fullmatch(r'/web/arc54/(?:[a-zA-Z0-9_.-]+\.(?:html|css|mjs))?', path):
                return super().do_GET()
            if path.startswith('/assets/') and re.fullmatch(r'/assets/(?:fonts/(?:selawik/)?[A-Za-z0-9-]+\.ttf|icons/(?:lucide/)?[A-Za-z0-9-]+\.svg|wallpaper/cold-ripple\.jpg)',path):
                return super().do_GET()
            return self.send_error(404)
        if path==API+'/capabilities':
            return self.send_json(200,{'schema':1,'version':VERSION,'theatre':{'tabs':list(TABS),'title':True},'source':'Colosseum Content API'})
        if path==API+'/theatre/browse':
            tab=parse_qs(url.query).get('tab',['discover'])[0]
            if tab not in TABS:
                return self.send_json(422,{'error':'TAB_UNAVAILABLE'})
            method,args='titleCatalog',[{**TABS[tab],'order':'votes'},0,24]
        elif match:=re.fullmatch(API+r'/theatre/title/(tt[0-9]{5,12})',path):
            method,args='titlePresentation',[match[1]]
        else:
            return self.send_json(404,{'error':'ROUTE_UNAVAILABLE'})
        try:
            data=worker('/v1/catalogue/imdb/'+method,{'args':args},lease())
            if data.get('apiVersion')!=1 or data.get('method')!=method or 'result' not in data:
                raise ValueError('incompatible response')
            result=data['result']
            if method=='titlePresentation' and not result.get('title'):
                return self.send_json(404,{'error':'TITLE_NOT_FOUND','source':'Colosseum Content API'})
            return self.send_json(200,{'schema':1,'source':'Colosseum Content API','revision':data.get('revision'),'result':result})
        except urllib.error.HTTPError as e:
            return self.send_json(502,{'error':'UPSTREAM_HTTP','upstreamStatus':e.code,'source':'Colosseum Content API'})
        except (OSError,ValueError,TypeError,AttributeError):
            return self.send_json(502,{'error':'UPSTREAM_UNAVAILABLE','source':'Colosseum Content API'})

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--port',type=int,default=8932)
    a=p.parse_args()
    s=ThreadingHTTPServer(('127.0.0.1',a.port),lambda *x,**kw:Handler(*x,directory=str(a.root),**kw))
    print(f'Arc 54 dev on http://127.0.0.1:{a.port}',flush=True)
    s.serve_forever()
if __name__=='__main__': main()
