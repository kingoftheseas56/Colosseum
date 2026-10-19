#!/usr/bin/env python3
"""Compare C++ M172 projection with the exact hash-locked executable source."""
import argparse, hashlib, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--oracle',required=True);p.add_argument('--candidate',required=True)
a=p.parse_args();raw=pathlib.Path(a.oracle).read_bytes()
expected='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
assert hashlib.sha256(raw).hexdigest()==expected,'Wrong server.js: refusing unqualified oracle'
s=raw.decode();start=s.index('    function getStatistics(e, idx) {');end=s.index('    function Emit(args)',start)
program='const util=require("util");\n'+s[start:end]+'''
const e={infoHash:'a'.repeat(40),torrent:{name:'bundle',pieceLength:4,files:[
{path:'bundle/a',name:'a',length:3,offset:0},{path:'bundle/b',name:'b',length:3,offset:3},
{path:'bundle/empty',name:'empty',length:0,offset:6}]},
bitfield:{get:i=>i===0||i===1},options:{},selection:[{from:0,to:1,offset:0,priority:1}],
swarm:{wires:[
{peerChoking:false,requests:[1,2,3],peerAddress:'127.0.0.1:1',amInterested:true,isSeeder:false,downloadSpeed:()=>7,uploadSpeed:()=>9},
{peerChoking:false,requests:[],peerAddress:null,amInterested:false,isSeeder:false,downloadSpeed:()=>0,uploadSpeed:()=>0},
{peerChoking:true}],queued:7,_peers:Object.fromEntries(Array.from({length:11},(_,i)=>[i,{}])),tries:13,
paused:true,connections:[1,2,3,4],size:55,downloaded:12345,uploaded:6789,downloadSpeed:()=>23.5}};
// Match the controlled snapshot's wire list while maintaining explicit counts.
// The second unchoked wire remains in source projection, so candidate fixture
// has its matching record too.
for(const idx of [undefined,0,1,2,3,-1,'0','1','01','bad',null,false,''])
console.log(JSON.stringify(getStatistics(e,idx)));
'''
reference=[json.loads(x) for x in subprocess.check_output(['node','-e',program],text=True).splitlines()]
candidate=[json.loads(x) for x in subprocess.check_output([a.candidate,'--goldens'],text=True).splitlines()]
assert len(reference)==len(candidate)
for i,(ref,got) in enumerate(zip(reference,candidate)):
    assert ref==got,f'case {i}:\nreference={json.dumps(ref,sort_keys=True)}\ncandidate={json.dumps(got,sort_keys=True)}'
print(json.dumps({'oracleSha256':expected,'projectionCases':len(reference),'result':'PASS','scope':'Controlled M172 projection only; not live swarm lifecycle parity'},indent=2))
