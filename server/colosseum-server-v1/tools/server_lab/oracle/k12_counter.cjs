'use strict';
// Extract only the hash-locked M662 bytes. No substitute server bundle is accepted.
const fs = require('node:fs');
const crypto = require('node:crypto');
const vm = require('node:vm');
const {EventEmitter} = require('node:events');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
function factory(bundle, clock) {
  const bytes = fs.readFileSync(bundle);
  assert.equal(hash(bytes), '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
  const code = bytes.subarray(3424943,3425719);
  assert.equal(hash(code), 'fb3f3872c39d5587bf694679581f4a7d0a497a86b0fc21ad3d02c884de94ad26');
  const module = {exports:{}};
  vm.runInNewContext(`(${code.toString('utf8')})`, {
    setTimeout:clock.set, clearTimeout:clock.clear
  })(module,module.exports);
  return module.exports;
}
function run(bundle, c) {
  let now=0, serial=0, delay=10, reentered=false, thrown=false;
  const trace=[], timers=new Map(), events=new EventEmitter();
  const clock={
    set(fn,ms){const id=++serial;timers.set(id,{fn,due:now+ms});trace.push(['set',id,now+ms]);return id;},
    clear(id){timers.delete(id);trace.push(['clear',id]);}
  };
  function act(op) {
    if(op[0]==='wait') {
      const target=now+op[1];let guard=0;
      while(true) {
        const next=[...timers].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due||a[0]-b[0])[0];
        if(!next)break;
        assert.ok(++guard<100,'timer runaway');
        now=next[1].due;timers.delete(next[0]);trace.push(['fire',next[0]]);next[1].fn();
      }
      now=target;
    } else if(op[0]==='delay')delay=op[1];
    else events.emit(op[0],op[1]||'a',op[2]);
  }
  function notify(kind,h,i) {
    trace.push([kind,now,h,i===undefined?null:i]);
    if(!thrown && c.throw===kind){thrown=true;throw new Error('callback throw');}
    if(!reentered && c.reenter?.when===kind) {
      reentered=true;for(const op of c.reenter.ops)act(op);
    }
  }
  factory(bundle,clock)(events,'open','close',(h,i)=>c.hashOnly?h:`${h}:${i}`,
    (h,i)=>notify('positive',h,i),(h,i)=>notify('zero',h,i),()=>delay);
  for(const op of c.ops){try{act(op);}catch(e){trace.push(['error',e.message]);}}
  return trace;
}
if(require.main===module) {
  const [bundle,casesPath,candidate]=process.argv.slice(2);
  const cases=JSON.parse(fs.readFileSync(casesPath));
  const expected=cases.map(c=>({name:c.name,trace:run(bundle,c)}));
  if(!candidate){console.log(JSON.stringify({node:process.version,cases:expected},null,2));process.exit(0);}
  const r=spawnSync(candidate,[casesPath],{encoding:'utf8'});
  assert.equal(r.error,undefined,`native K12 candidate unavailable: ${r.error?.message}`);
  assert.equal(r.status,0,r.stderr);
  assert.deepEqual(JSON.parse(r.stdout),expected);
  console.log(`K12 counter differential: ${cases.length} passed, 0 failed; Node ${process.version}`);
}
module.exports={run};
