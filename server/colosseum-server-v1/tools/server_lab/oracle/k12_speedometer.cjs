'use strict';
const fs=require('node:fs');
const crypto=require('node:crypto');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const [bundlePath,casesPath,executable]=process.argv.slice(2);
const bundle=fs.readFileSync(bundlePath);
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(hash(bundle),'405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
const source=bundle.subarray(1985144,1985973);
assert.equal(hash(source),'63410c90938208354e1c4c0db52f340008d227794ed7c16367db8e09f81b60cf');
const cases=JSON.parse(fs.readFileSync(casesPath,'utf8'));
const expected=cases.map(c=>{
  let tick;
  const module={exports:{}};
  const factory=vm.runInNewContext('('+source.toString('utf8')+')',{
    setInterval(callback,ms){assert.equal(ms,250);tick=callback;return {unref(){}};}
  });
  factory(module,module.exports);
  for(let n=1;n<(c.startTick??1);++n)tick();
  const meters=new Map([['a',module.exports(c.seconds)]]),readings=[];
  for(const op of c.steps){
    if(op[0]==='tick')for(let n=0;n<op[1];++n)tick();
    else if(op[0]==='new')meters.set(op[1],module.exports(op[2]));
    else if(op[0]==='read')readings.push(meters.get(op[1])(typeof op[2]==='string'?Number(op[2]):op[2]));
    else throw new Error('unknown speedometer operation');
  }
  // Diagnostic tokens preserve non-finite distinctions; this is not a claim
  // that the HTTP serializer emits strings for non-finite numbers.
  return {name:c.name,readings:readings.map(value=>Number.isFinite(value)?value:String(value))};
});
const run=spawnSync(executable,[casesPath],{encoding:'utf8'});
if(run.status!==0)throw new Error(`native speedometer exit ${run.status}: ${run.stderr}`);
const actual=JSON.parse(run.stdout);
assert.deepEqual(actual,expected);
console.log(JSON.stringify({state:'PASS',node:process.version,cases:cases.length,
  oracleSha256:hash(bundle),moduleSha256:hash(source),actual},null,2));
