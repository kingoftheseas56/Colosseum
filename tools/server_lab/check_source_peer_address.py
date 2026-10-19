#!/usr/bin/env python3
"""Exact pinned M818 add gate, deliberately not a claim about net.connect errors."""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--oracle',required=True);p.add_argument('--candidate',required=True);a=p.parse_args()
b=pathlib.Path(a.oracle).read_bytes();assert hashlib.sha256(b).hexdigest()=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
s=b.decode();begin=s.index('Swarm.prototype.add = function(addr) {');end=s.index('    }, Swarm.prototype.remove',begin)
body=s[begin+len('Swarm.prototype.add = function(addr) {'):end]
cases=['127.0.0.1:1','127.0.0.1:01','localhost:80',':80','999.1.1.1:5','127.0.0.1:65535','127.0.0.1:65534','127.0.0.1:0','host:1.5','host:0x50','host:1e2','host: 01 ','[::1]:80','::1:80','fe80:123:tail','host:80:extra','host:5x','host:','bad','host:-1','host:Infinity','host:0b10','host:0o10']
js='const add=function(addr){'+body+'};for(const raw of '+json.dumps(cases)+'){const s={_peers:{},_queues:[{push(){return {}}}],_drain(){}};add.call(s,raw);const accepted=!!s._peers[raw];console.log(JSON.stringify(accepted?{accepted,raw,host:raw.split(":")[0],portText:raw.split(":")[1],portNumber:Number(raw.split(":")[1])}:{accepted}));}'
ref=[json.loads(x)for x in subprocess.check_output(['node','-e',js],text=True).splitlines()]
got=[json.loads(x)for x in subprocess.check_output([a.candidate],text=True).splitlines()]
assert ref==got,(ref,got)
print(json.dumps({'result':'PASS','cases':len(ref),'scope':'M818 raw address add gate only; native DNS, aliases and throwing ports require integration tests'},indent=2))
