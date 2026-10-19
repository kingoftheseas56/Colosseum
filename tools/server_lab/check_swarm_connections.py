#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--oracle',required=True);p.add_argument('--candidate',required=True);a=p.parse_args()
b=pathlib.Path(a.oracle).read_bytes();assert hashlib.sha256(b).hexdigest()=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
s=b.decode();start=s.index('    var utp = __webpack_require__(819), net = __webpack_require__(39), fifo = __webpack_require__(821)');end=s.index('\n}, function(module, exports',start)
js='''const E=require('events');const util=require('util');let now=0,serial=0;const timers=new Map();
function setTimeout(fn,ms){const t={id:++serial,at:now+ms,fn,unref(){}};timers.set(t.id,t);return t;}
function clearTimeout(t){if(t)timers.delete(t.id);}
function advance(t){now=t;for(;;){let next=[...timers.values()].filter(x=>x.at<=now).sort((a,b)=>a.at-b.at||a.id-b.id)[0];if(!next)break;timers.delete(next.id);next.fn();}}
class Conn extends E{pipe(x){x.conn=this;return x;}destroy(){if(this.dead)return;this.dead=true;this.emit('close');}}
class Wire extends E{pipe(x){return x;}handshake(){}destroy(){if(this.dead)return;this.dead=true;this.emit('close');}}
const net={connect(){const c=new Conn();setTimeout(()=>c.emit('connect'),0);return c;}};
const fifo=()=>{let a=[];return{get length(){return a.length},push(x){const n={x};a.push(n);return n;},shift(){return a.shift().x},remove(n){a=a.filter(x=>x!==n)}}};
const req=id=>({819:{},39:net,821:fifo,35:fn=>{let done=false;return(...x)=>{if(!done){done=true;return fn(...x)}}},416:()=>()=>0,822:()=>new Wire(),4:{EventEmitter:E},0:util}[id]);
const module={exports:{}};(function(module,exports,__webpack_require__){
'''+s[start:end]+'''
})(module,module.exports,req);
let s=new module.exports('a'.repeat(40),'b'.repeat(20),{size:2});
const addr=id=>'127.0.0.1:'+(1000+id);
const out=()=>{advance(now);console.log(JSON.stringify({queued:s.queued,unique:Object.keys(s._peers).length,tries:s.tries,connections:s.connections.length,paused:s.paused}));};
const add=id=>{s.add(addr(id));out();};
const drop=id=>{s._peers[addr(id)].wire.conn.destroy();out();};
const hs=id=>{s._peers[addr(id)].wire.emit('handshake',Buffer.from('a'.repeat(40),'hex'),Buffer.from('b'.repeat(20)));out();};
out();add(1);add(2);add(3);add(1);s.pause();out();drop(1);s.resume();out();hs(2);drop(2);add(4);
advance(4000);out();drop(3);drop(2);drop(4);advance(11999);out();advance(12000);out();drop(2);
advance(24000);out();drop(2);add(2);hs(2);drop(2);s.destroy();out();advance(28000);out();
s=new module.exports('a'.repeat(40),'b'.repeat(20),{size:2});s.pause();out();
const incoming=new Conn();incoming.address=()=>({address:'127.0.0.1',port:4321});
const incomingWire=new Wire();s._onincoming(incoming,incomingWire);out();
if(incomingWire.peerAddress!=='127.0.0.1:4321')throw Error('incoming local address changed');
add(1);incoming.destroy();out();s.resume();out();
s=new module.exports('a'.repeat(40),'b'.repeat(20),{size:1});s.pause();out();
s.add('127.0.0.1:01');out();s.add('127.0.0.1:1');out();s.resume();out();
s.add('127.0.0.1:01');out();s._peers['127.0.0.1:01'].wire.conn.destroy();out();
'''
ref=[json.loads(x)for x in subprocess.check_output(['node','-e',js],text=True).splitlines()]
got=[json.loads(x)for x in subprocess.check_output([a.candidate],text=True).splitlines()]
assert len(ref)==len(got),(len(ref),len(got))
for i,(r,g)in enumerate(zip(ref,got)):assert r==g,(i,r,g)
print(json.dumps({'result':'PASS','sourceTransitions':len(ref),'scope':'M818 capacity, one-step drain, pause/resume, retained peer retry 4/8/12s, failed removal, rediscovery, teardown and incoming distinction and raw alias identity'},indent=2))
