'use strict';
const assert=require('node:assert/strict');
const {EventEmitter}=require('node:events');
const {loadBundle}=require('./module_loader.cjs');
const oracle=loadBundle(process.argv[2]);
assert.equal(oracle.sha256,'405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
const e=oracle.require(172), pending=[],trace=[];
let constructions=0;
e.getDefaults=()=>({peerSearch:false});
e.engine=()=>{
  ++constructions;
  const engine=new EventEmitter();
  engine.swarm=new EventEmitter();engine.swarm.resume=()=>{};
  engine.ready=cb=>cb();
  engine.destroy=cb=>pending.push(cb);
  engine.selection=[];
  return engine;
};
const a='f'.repeat(40),b='a'.repeat(40),c='9'.repeat(40);
const snapshot=label=>trace.push({label,list:e.list().slice(),constructions});
e.on('engine-destroyed',()=>snapshot('during-destroyed-event'));
e.create(a,{});e.create(b,{});e.create(c,{});snapshot('inserted');
assert.deepEqual(Array.from(e.list()),[a,b,c]);
e.create(a,{});snapshot('reused');assert.equal(constructions,3);
e.remove(b,()=>snapshot('remove-callback'));snapshot('pending-remove');
assert.deepEqual(Array.from(e.list()),[a,b,c]);
pending.shift()();snapshot('removed');assert.deepEqual(Array.from(e.list()),[a,c]);
e.create(b,{});snapshot('recreated');assert.deepEqual(Array.from(e.list()),[a,c,b]);
// The oracle's same-hash create-during-destroy race is recorded, not silently
// imposed on the native registry's stronger generation-retirement contract.
e.remove(b,()=>snapshot('race-remove-callback'));
e.create(b,{});snapshot('created-during-destroy');assert.equal(constructions,4);
pending.shift()();snapshot('race-completed');assert.deepEqual(Array.from(e.list()),[a,c]);
console.log(JSON.stringify({node:process.version,oracle:oracle.sha256,trace},null,2));
