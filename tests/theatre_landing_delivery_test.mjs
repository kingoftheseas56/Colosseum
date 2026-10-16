import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';

const strip=s=>s.replace(/^\.(pragma|import).*$/gm,'');
const source=name=>strip(fs.readFileSync(new URL('../qml/'+name,import.meta.url),'utf8'));
const now=Date.parse('2026-10-16T12:00:00Z');
function moduleUnderTest(){
 const delivery=vm.createContext({console});vm.runInContext(source('CatalogDelivery.js'),delivery);
 const rules=vm.createContext({console,Date,Math});vm.runInContext(source('TheatreCatalogRules.js'),rules);
 const policy=vm.createContext({console});vm.runInContext(source('ExplicitContentPolicy.js'),policy);
 const addon=vm.createContext({console,setTimeout});vm.runInContext(source('AddonClient.js'),addon);
 const mod=vm.createContext({console,setTimeout,Date,Math,CatalogDelivery:delivery,Rules:rules,Policy:policy,AddonClient:addon});
 vm.runInContext(source('TheatreApi.js'),mod);
 const live=[];mod.setRequestAdapter((url)=>{live.push(url);return()=>{};});
 return {mod,live};
}
function signal(){const slots=new Set();return{connect:f=>slots.add(f),disconnect:f=>slots.delete(f),emit:(...args)=>[...slots].forEach(f=>f(...args)),get size(){return slots.size;}};}
function fixture(mod,tab,{legacy=false}={}){
 const method=tab==='anime'?'animeCatalog':'titleCatalog';
 const key=args=>JSON.stringify(args),cache=new Map(),reads=[],misses=[];
 let status='pending',error='',cached=false,landingReads=0;
 const cat={remoteEnabled:true,available:()=>true,remoteQueryChanged:signal(),revisionChanged:signal(),readyChanged:signal(),
  [method](...args){reads.push(args);if(!cache.has(key(args)))misses.push(args);return cache.get(key(args))||[];},
  [method+'State'](...args){return{source:'remote',key:key(args),status:cache.has(key(args))?'success':'pending',cached:cache.has(key(args)),enabled:true};},
  landing(requested){assert.equal(requested,tab);landingReads++;return cached?{version:1,dataset:tab==='anime'?'mal':'imdb',tab,queries:[]}:{};},
  landingState(requested){assert.equal(requested,tab);return{source:'remote',key:'landing:'+tab,status,error,cached,enabled:true,retryable:status==='failure'};}
 };
 function prime(){
  let defs=mod.Rules.defaultRows(tab);if(tab==='movies')defs=defs.concat(mod.Rules.dailyRows(now,6));
  for(const def of defs)for(const q of tab==='anime'?mod.animeQueriesFor(def.recipe):mod.Rules.indexQueriesFor(def.recipe)){
   const row=tab==='anime'?{mal_id:1,title:'Anime fixture',images:{jpg:{large_image_url:'poster'}},genres:[],score:8,members:100000}:{tt:'tt1',type:tab==='movies'?'movie':'series',title:'IMDb fixture',genres:[],year:2026,rating:8,votes:300000};
   cache.set(key([q,0,20]),[row]);
  }
 }
 if(legacy){delete cat.landing;delete cat.landingState;prime();}
 return{cat,reads,misses,prime,get landingReads(){return landingReads;},succeed(){prime();cached=true;status='success';cat.remoteQueryChanged.emit('landing:'+tab);},fail(reason,withCache=false){if(withCache)prime();cached=withCache;status='failure';error=reason;cat.remoteQueryChanged.emit('landing:'+tab);}};
}
const tick=()=>new Promise(resolve=>setTimeout(resolve,10));

for(const tab of ['movies','shows','anime'])test(tab+': landing gates exact owner and primed queries avoid misses',async()=>{
 const {mod,live}=moduleUnderTest(),f=fixture(mod,tab),wrong=fixture(mod,tab==='anime'?'movies':'anime'),pushes=[];
 const options={imdbCatalog:tab==='anime'?wrong.cat:f.cat,malCatalog:tab==='anime'?f.cat:wrong.cat,generation:71,nowMs:now};
 const cancel=mod.loadCatalogPage(tab,options,p=>pushes.push(p));
 assert.equal(f.landingReads,1);assert.equal(wrong.landingReads,0);assert.equal(f.reads.length,0);assert.equal(live.length,0);
 assert.equal(pushes.at(-1).loading,true);
 f.cat.remoteQueryChanged.emit('unrelated');assert.equal(f.reads.length,0);
 f.succeed();await tick();
 assert.ok(f.reads.length>0);assert.equal(f.misses.length,0);assert.ok(pushes.some(p=>p.rows.length));
 assert.ok(pushes.every(p=>p.generation===71));assert.ok(live.length>0,'existing live enrichment remains enabled');
 cancel();assert.equal(f.cat.remoteQueryChanged.size,0);
});

test('old publication STATIC_NOT_PREPARED falls back to existing exact queries',()=>{
 const {mod}=moduleUnderTest(),f=fixture(mod,'shows'),pushes=[];
 const cancel=mod.loadCatalogPage('shows',{imdbCatalog:f.cat,nowMs:now},p=>pushes.push(p));
 assert.equal(f.reads.length,0);f.fail('STATIC_NOT_PREPARED');
 assert.ok(f.reads.length>0);assert.ok(f.misses.length>0);assert.equal(pushes.some(p=>p.failed),false);cancel();
});

test('owners without landing retain legacy loading behavior',()=>{
 const {mod}=moduleUnderTest(),f=fixture(mod,'shows',{legacy:true}),pushes=[];
 const cancel=mod.loadCatalogPage('shows',{imdbCatalog:f.cat,nowMs:now},p=>pushes.push(p));
 assert.ok(f.reads.length);assert.equal(f.misses.length,0);assert.ok(pushes.some(p=>p.rows.length));cancel();
});

test('uncached landing failure is honest and does not fan out child or provider reads',()=>{
 const {mod,live}=moduleUnderTest(),f=fixture(mod,'movies'),pushes=[];
 const cancel=mod.loadCatalogPage('movies',{imdbCatalog:f.cat,generation:91},p=>pushes.push(p));
 f.fail('STATIC_HASH_MISMATCH');
 const last=pushes.at(-1);assert.equal(last.failed,true);assert.equal(last.loading,false);assert.equal(last.error,'STATIC_HASH_MISMATCH');assert.equal(last.generation,91);
 assert.equal(f.reads.length,0);assert.equal(live.length,0);assert.equal(f.cat.remoteQueryChanged.size,0);cancel();
});

test('validated cached landing failure paints cached rows with an offline warning',async()=>{
 const {mod}=moduleUnderTest(),f=fixture(mod,'shows'),pushes=[];
 const cancel=mod.loadCatalogPage('shows',{imdbCatalog:f.cat,nowMs:now},p=>pushes.push(p));
 f.fail('STATIC_NETWORK_ERROR',true);await tick();
 assert.equal(f.misses.length,0);assert.ok(pushes.some(p=>p.rows.length&&p.warning==='Showing offline catalogue'));cancel();
});

test('cancellation before landing completion disconnects and suppresses old generation',()=>{
 const {mod,live}=moduleUnderTest(),f=fixture(mod,'movies'),pushes=[];
 const cancel=mod.loadCatalogPage('movies',{imdbCatalog:f.cat,generation:3},p=>pushes.push(p));
 cancel();const before=pushes.length;assert.equal(f.cat.remoteQueryChanged.size,0);f.succeed();
 assert.equal(pushes.length,before);assert.equal(f.reads.length,0);assert.equal(live.length,0);
});

test('rotation is UTC and remote reads use the same supplied day as initial rows',()=>{
 const {mod}=moduleUnderTest();
 const a=Date.parse('2026-10-17T00:05:00+05:30'),b=Date.parse('2026-10-16T18:35:00Z');
 assert.equal(mod.Rules.dayIndex(a),mod.Rules.dayIndex(b));
 const f=fixture(mod,'movies');
 const cancel=mod.loadCatalogPage('movies',{imdbCatalog:f.cat,nowMs:now},()=>{});f.succeed();
 assert.equal(f.misses.length,0,'initial and remotePage findDef share the pinned day');cancel();
});

for (const tab of ['movies','shows','anime'])test(tab+': configured API never gates a ready local SQLite owner',()=>{
 const {mod}=moduleUnderTest(),f=fixture(mod,tab),pushes=[];f.prime();
 f.cat.landingState=()=>({source:'local',status:'missing',enabled:true});
 const cancel=mod.loadCatalogPage(tab,{imdbCatalog:f.cat,malCatalog:f.cat,nowMs:now},p=>pushes.push(p));
 assert.equal(f.landingReads,0);assert.ok(f.reads.length);assert.equal(f.misses.length,0);
 assert.ok(pushes.some(p=>p.rows.length));cancel();
});
