'use strict';
// Unmodified M172 with controlled engine/bitmap/selection inputs. The fake
// ready() explicitly models M814's nextTick boundary; it is not a real swarm.
const assert=require('node:assert/strict');
const {EventEmitter}=require('node:events');
const {loadBundle}=require('./module_loader.cjs');
const oracle=loadBundle(process.argv[2]);
assert.equal(oracle.sha256,'405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
const fs=oracle.require(172);
fs.getDefaults=()=>({peerSearch:false});
fs.STREAM_TIMEOUT=1;fs.ENGINE_TIMEOUT=1;
let engine;
fs.engine=()=>engine;
const nextTurn=()=>new Promise(resolve=>setImmediate(resolve));
const cases=[
  {name:'cross-boundary',offset:7,length:2,committed:[],verify:[0,1],progress:[-1,0,1]},
  {name:'already-cached',offset:0,length:8,committed:[0],verify:[],progress:[1]},
  {name:'duplicate-irrelevant',offset:0,length:16,committed:[],verify:[9,0,0,1],progress:[0,0.5,1]},
  {name:'partial',offset:0,length:16,committed:[1],verify:[0],progress:[0.5,1]},
  {name:'circular',offset:7,length:2,committed:[],verify:[0,1],progress:[-1,0,1],buffer:true,circular:true},
  {name:'buffered-persistent',offset:0,length:8,committed:[],verify:[0],progress:[0,1],buffer:true},
  {name:'zero-length',offset:0,length:0,committed:[],verify:[0],progress:[-Infinity,NaN]}
];
(async()=>{
  const output=[];
  for(let index=0;index<cases.length;++index){
    const c=cases[index], hash=(index+1).toString(16).padStart(40,'0');
    engine=new EventEmitter();const e=engine;
    e.swarm=new EventEmitter();e.swarm.resume=()=>{};
    e.ready=callback=>process.nextTick(callback);
    e.destroy=callback=>process.nextTick(callback);
    e.torrent={pieceLength:8,files:[{name:'fixture.bin',path:'fixture.bin',offset:c.offset,length:c.length}]};
    e.bitfield={get:piece=>c.committed.includes(piece)};
    e.buffer=c.buffer || false;
    e.store=c.circular ? {} : {getDest:()=>'/synthetic/0'};
    const selections=[];e.select=(...args)=>selections.push(args);e.selection=[];
    fs.create(hash,{});await nextTurn();
    const trace=[],progress=[];
    const listeners=[
      ['stream-active',(h,i)=>{if(h===hash)trace.push('active');}],
      ['stream-created',(h,i)=>{if(h===hash)trace.push('created');}],
      ['stream-progress:'+hash+':0',(p,path)=>{assert.equal(path,undefined);progress.push(p);trace.push('progress');}],
      ['stream-cached:'+hash+':0',(path)=>{assert.equal(path,c.circular?undefined:'/synthetic/0');trace.push('scoped-cached');}],
      ['stream-cached',(h)=>{if(h===hash)trace.push('cached');}]
    ];
    for(const [name,listener] of listeners)fs.on(name,listener);
    fs.emit('stream-open',hash,0);fs.emit('stream-open',hash,0);
    assert.deepEqual(trace,['active']);
    await nextTurn();
    for(const piece of c.verify)e.emit('verify',piece);
    assert.deepEqual(progress,c.progress);
    assert.equal(trace.filter(x=>x==='created').length,1);
    assert.equal(trace.filter(x=>x==='cached').length,1);
    const before=trace.length;e.emit('verify',0);await nextTurn();assert.equal(trace.length,before);
    assert.equal(selections.length,c.buffer?0:1);
    output.push({name:c.name,trace,progress:progress.map(x=>Number.isFinite(x)?x:String(x)),selections});
    fs.emit('stream-close',hash,0);fs.emit('stream-close',hash,0);
    for(const [name,listener] of listeners)fs.removeListener(name,listener);
    fs.remove(hash);
  }
  console.log(JSON.stringify({oracle:oracle.sha256,node:process.version,cases:output},null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
