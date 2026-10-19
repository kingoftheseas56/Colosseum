// Real TheatreApi.js execution against bounded local fixtures. No provider/network.
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';

const root = new URL('../', import.meta.url);
const strip = s => s.replace(/^\.(pragma|import).*$/gm, '');
const read = name => strip(fs.readFileSync(new URL('qml/' + name, root), 'utf8'));
const pause = () => new Promise(r => setTimeout(r, 5));
const now = Date.parse('2026-10-16T12:00:00Z');
function signal() {
  const s = new Set();
  return {connect:f=>s.add(f), disconnect:f=>s.delete(f),
    emit:k=>[...s].forEach(f=>f(k)), get size(){return s.size;}};
}
function moduleUnderTest() {
  const parts = {};
  for (const [name,as] of [['CatalogDelivery.js','CatalogDelivery'],
    ['TheatreCatalogRules.js','Rules'],['ExplicitContentPolicy.js','Policy'],
    ['AddonClient.js','AddonClient']]) {
    const c = vm.createContext({console,setTimeout,Date,Math});
    vm.runInContext(read(name),c);parts[as]=c;
  }
  const c=vm.createContext({console,setTimeout,Date,Math,...parts});
  const api = process.env.THEATRE_BASELINE
    ? strip(fs.readFileSync(new URL('baseline/TheatreApi.js',root),'utf8'))
    : read('TheatreApi.js');
  vm.runInContext(api,c);
  c.setExtensions([]);
  return c;
}
class FakeFacts {
  constructor() {
    this.remoteEnabled=true;this.remoteQueryChanged=signal();this.revisionChanged=signal();
    this.readyChanged=signal();this.requestFailed=signal();
    this.pending=new Map();this.cache=new Map();this.failures=new Map();
    this.reads=[];this.queued=[];this.retries=[];this.source='remote';this.sourceRevision='pub-a';
  }
  available(){return true;}
  ready(){return false;}
  key(ids){return JSON.stringify(['titleFacts',ids]);}
  titleCatalog(){return [];}
  titleFacts(ids){
    this.reads.push(Array.from(ids));
    const k=this.key(ids),v=this.cache.get(k);
    if ((!v || v.stale) && !this.failures.has(k) && !this.pending.has(k)) {
      this.pending.set(k,Array.from(ids));this.queued.push(Array.from(ids));
      setImmediate(()=>{if(this.pending.has(k))this.remoteQueryChanged.emit(k);});
    }
    return v?v.value:{};
  }
  titleFactsState(ids){
    if(this.source==='local') return {source:'local',status:'success'};
    const k=this.key(ids),c=this.cache.has(k);
    return {source:'remote',key:k,enabled:true,cached:c,sourceRevision:this.sourceRevision,
      status:this.pending.has(k)?'pending':this.failures.has(k)?'failure':c?'success':'missing',
      retryable:this.failures.has(k)&&!this.pending.has(k),
      error:this.failures.get(k)||''};
  }
  retryTitleFacts(ids){
    this.retries.push(Array.from(ids));const k=this.key(ids);
    if (!this.failures.has(k)) return false;
    this.failures.delete(k);this.pending.set(k,Array.from(ids));return true;
  }
  seedStale(ids,value){this.cache.set(this.key(ids),{value,stale:true});}
  succeed(ids,value){
    const k=this.key(ids);this.pending.delete(k);this.failures.delete(k);
    this.cache.set(k,{value,stale:false});this.remoteQueryChanged.emit(k);
  }
  fail(ids,reason){
    const k=this.key(ids);this.pending.delete(k);this.failures.set(k,reason);
    this.remoteQueryChanged.emit(k);
  }
  sourceChange(){
    this.source='local';this.revisionChanged.emit('changed');
  }
}
function metas(count,type='movie') {
  return Array.from({length:count},(_,i)=>({
    id:'tt'+String(10000+i),type,name:'Title '+i,
    releaseInfo:String(2026-(i%9)),
    status:i%3===0?'Continuing':'Ended',genres:['Drama']
  }));
}
function bindProvider(mod,items) {
  const requests=[];
  mod.setRequestAdapter((url,done)=>{
    requests.push(url);
    if(/\/catalog\/(movie|series)\/top(?:\/skip=\d+)?\.json/.test(url))
      done({metas:items.slice(url.match(/skip=(\d+)/)?.[1]||0)});
    else done(null);
    return ()=>{};
  });
  return requests;
}
function startDeep(mod,cat,items,pageKey='movies') {
  const requests=bindProvider(mod,items),published=[];
  let controller=null;
  const cancel=mod.loadCatalogPage(pageKey,{imdbCatalog:cat,generation:13,
      nowMs:now,onFactsController:c=>controller=c},
    payload=>published.push(payload));
  return {controller,cancel,requests,published};
}
function toFacts(ids,predicate) {
  const res={};
  for(const id of ids){const n=Number(id.slice(2))-10000;
    res[id]={isAnime:predicate(n),votes:n%7===0?300:10000};}
  return res;
}
async function resolveThrough(cat,controller,stop,predicate=()=>false,max=35) {
  for(let i=0;i<max;i++) {
    if(stop()) return;
    await pause();
    const next=cat.pending.values().next().value;
    if(next) cat.succeed(next,toFacts(next,predicate));
  }
  assert.ok(stop(),'bounded facts stream reaches requested cap');
}
function ids(items){return Array.from(items,x=>String(x.id));}

test('landing: first visible 20 only; unrelated pending cannot settle; scroll changes demand without provider reload',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),data=metas(120);
  const h=startDeep(mod,cat,data);
  assert.ok(h.controller,'page receives the actual Theatre demand controller');
  assert.equal(cat.queued.length,0,'provider receipt alone never fetches facts');
  h.controller.setVisible(['top-10']);
  assert.equal(cat.queued.length,1);assert.equal(cat.queued[0].length,20);
  cat.remoteQueryChanged.emit('unrelated-title-key');cat.revisionChanged.emit('unrelated');
  assert.equal(h.controller.snapshot('top-10').items.length,0,'pending unknown is not shown');
  assert.equal(cat.queued.length,1,'unrelated notification cannot fan out');
  assert.equal(h.controller.snapshot('recently-released').items.length,0);
  cat.succeed(cat.queued[0],toFacts(cat.queued[0],()=>false));await pause();
  assert.equal(h.controller.snapshot('top-10').items.length,10);
  assert.equal(cat.queued.length,1,'offscreen recent row requests nothing');
  const providerCount=h.requests.length;
  h.controller.setVisible(['recently-released']);
  await resolveThrough(cat,h.controller,()=>h.controller.snapshot('recently-released').complete);
  assert.ok(cat.queued.every(b=>b.length<=20),'all titleFacts reads bounded');
  assert.equal(h.requests.length,providerCount,'viewport demand never reacquires Cinemeta');
  h.cancel();assert.equal(cat.remoteQueryChanged.size,0);
});

test('rejection refill has exact full-algorithm Top 10 and recent ranking',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),data=metas(105);
  const h=startDeep(mod,cat,data);
  const anime=n=>n<24||n%11===0;
  h.controller.setVisible(['top-10']);
  await resolveThrough(cat,h.controller,()=>h.controller.snapshot('top-10').complete,anime);
  const snapshot=h.controller.snapshot('top-10');
  assert.ok(cat.queued.length>=2,'rejected first 20 triggers next candidate batch');
  assert.deepEqual(ids(snapshot.items),data.filter((_,i)=>!anime(i)).slice(0,10).map(m=>m.id));
  h.controller.setVisible(['recently-released']);
  await resolveThrough(cat,h.controller,()=>h.controller.snapshot('recently-released').complete,anime);
  const mapped=data.map(mod.mapCinemeta);
  const filtered=mapped.filter((m,i)=>!anime(i)&&i%7!==0);
  const expected=mod.Rules.rankItems({kind:'recent'},filtered,now).slice(0,20);
  assert.deepEqual(ids(h.controller.snapshot('recently-released').items),ids(expected));
  assert.ok(cat.queued.every(a=>a.length<=20),'never calls unbounded titleFacts');
  h.cancel();
});

test('valid empty facts retains unknown; hard error blocks unknown; retry is exact; late callback cancelled',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(65));
  const c=h.controller;c.setVisible(['top-10']);
  const first=cat.queued[0];cat.fail(first,'HTTP_503');await pause();
  assert.deepEqual(ids(c.snapshot('top-10').items),[]);
  assert.equal(c.snapshot('top-10').error,'HTTP_503');
  assert.equal(c.snapshot('top-10').retryable,true);
  assert.equal(c.retry(),true);assert.deepEqual(cat.retries[0],first);
  cat.succeed(first,{});await pause();
  assert.deepEqual(ids(c.snapshot('top-10').items),metas(10).map(x=>x.id),
    'valid empty map means valid unknown-to-index results');
  h.cancel();const painted=h.published.length;
  cat.remoteQueryChanged.emit(cat.key(first));
  assert.equal(h.published.length,painted,'cancelled owner never paints');
});

test('validated stale cache can be used, unrelated revision cannot validate absent facts, source switch fences',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(50));
  const c=h.controller;
  const first=metas(20).map(x=>x.id);
  cat.seedStale(first,toFacts(first,()=>false));
  c.setVisible(['top-10']);assert.equal(c.snapshot('top-10').items.length,0);
  cat.remoteQueryChanged.emit('peer');assert.equal(c.snapshot('top-10').items.length,0);
  cat.fail(first,'OFFLINE');await pause();
  assert.equal(c.snapshot('top-10').stale,true);
  assert.equal(c.snapshot('top-10').items.length,10);
  assert.equal(c.retry(),true,'stale failed native key permits explicit retry');
  assert.deepEqual(cat.retries[0],first);
  cat.succeed(first,toFacts(first,()=>false));await pause();
  assert.equal(c.snapshot('top-10').stale,false,'fresh exact-key retry clears stale notice');
  h.cancel();
  const second=moduleUnderTest(),native=new FakeFacts(),page=startDeep(second,native,metas(70));
  page.controller.setVisible(['top-10']);
  native.sourceChange();await pause();
  assert.equal(page.controller.snapshot('top-10').items.length,0);
  assert.match(page.controller.snapshot('top-10').error,/source.changed/i);
  page.cancel();assert.equal(native.remoteQueryChanged.size,0);
});

test('live See-all: 40 visible entries fill in <=20-ID batches; raw/ranked cursor avoids skipping',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),data=metas(65);
  const requests=bindProvider(mod,data);
  const pin={pageKey:'movies',sourceKind:'house',rowKey:'recently-released'};
  const page=[],opts={imdbCatalog:cat,generation:9,nowMs:now};
  let cancel=mod.loadRowPage(pin,0,40,opts,p=>page.push(p));
  await resolveThrough(cat,null,()=>page.length>0,()=>false);
  assert.equal(page[0].items.length,40);
  assert.ok(page[0].nextCursor && typeof page[0].nextCursor==='object');
  assert.ok(page[0].nextCursor.index>0 && page[0].nextCursor.index<65);
  assert.equal(requests.length,1,'one Cinemeta query for multiple facts batches');
  assert.ok(cat.queued.every(a=>a.length<=20));
  cancel();
  const prior=cat.queued.length,second=[];
  cancel=mod.loadRowPage(pin,page[0].nextCursor,40,opts,p=>second.push(p));
  await resolveThrough(cat,null,()=>second.length>0,()=>false);
  assert.equal(second[0].items.length,15,'recent vote floor excludes ten low-vote candidates');
  assert.equal(new Set([...ids(page[0].items),...ids(second[0].items)]).size,55);
  assert.ok(cat.queued.length>prior,'next visible page demands remaining candidate facts');
  cancel();
  const cachedMisses=cat.queued.length,again=[];
  cancel=mod.loadRowPage(pin,0,40,opts,p=>again.push(p));
  await pause();await pause();
  assert.equal(cat.queued.length,cachedMisses,'peer consumer reused existing native facts cache');
  assert.deepEqual(ids(again[0].items),ids(page[0].items));cancel();

  // A provider returns only 40 source entries per window. Rejections in
  // window one must consume window two before reporting a visible 40-card page.
  const other=moduleUnderTest(),native=new FakeFacts(),long=metas(110),urls=[];
  other.setRequestAdapter((url,done)=>{
    urls.push(url);
    const match=url.match(/skip=(\d+)/);
    const from=match?Number(match[1]):0;
    done({metas:long.slice(from,from+40)});
    return ()=>{};
  });
  const visible=[],pTop={pageKey:'movies',sourceKind:'house',rowKey:'top-10'};
  const stop=other.loadRowPage(pTop,0,40,{imdbCatalog:native},x=>visible.push(x));
  await resolveThrough(native,null,()=>visible.length>0,n=>n<40);
  assert.deepEqual(ids(visible[0].items),long.slice(40,80).map(x=>x.id),
    'rejected source window causes bounded next provider acquisition to fill visible cap');
  assert.equal(urls.length,2,'provider fetched per candidate window, not per facts batch');
  assert.ok(native.queued.every(b=>b.length<=20));
  stop();
});

test('live See-all exact retry after failed facts key, no false exhausted page',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),data=metas(50);
  bindProvider(mod,data);
  const pin={pageKey:'movies',sourceKind:'house',rowKey:'top-10'},out=[];
  const opts={imdbCatalog:cat,generation:7};
  let cancel=mod.loadRowPage(pin,0,40,opts,res=>out.push(res));
  const batch=cat.queued[0];cat.fail(batch,'HTTP_502');await pause();
  assert.equal(out[0].failed,true);assert.equal(out[0].exhausted,false);
  assert.deepEqual(Array.from(out[0].failedFactsIds),batch);
  const ok=mod.retryRowPage(pin,0,40,{...opts,failedFactsIds:out[0].failedFactsIds});
  assert.equal(ok,true);assert.deepEqual(cat.retries[0],batch);
  cancel();
  const good=[];
  cancel=mod.loadRowPage(pin,0,40,opts,res=>good.push(res));
  await resolveThrough(cat,null,()=>good.length>0,()=>false);
  assert.equal(good[0].error,'');assert.equal(good[0].items.length,40);cancel();
});

test('root receiving: failed offscreen facts key cannot block a disjoint newly-visible shelf',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts();
  const data=metas(80).map((m,i)=>({...m,releaseInfo:i<20?'2010':'2026'}));
  const h=startDeep(mod,cat,data),c=h.controller;
  c.setVisible(['top-10']);const failed=cat.queued[0];cat.fail(failed,'HTTP_503');await pause();
  const before=cat.queued.length;
  c.setVisible(['recently-released']);await pause();
  assert.ok(cat.queued.length>before,'new disjoint exact query must fetch despite old offscreen error');
  assert.ok(cat.queued.at(-1).every(id=>!failed.includes(id)),'fixture uses disjoint ID vectors');
  h.cancel();
});
test('root receiving: remote publication invalidation cannot retain completed ephemeral old facts',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(80)),c=h.controller;
  c.setVisible(['top-10']);cat.succeed(cat.queued[0],toFacts(cat.queued[0],()=>false));await pause();
  assert.equal(c.snapshot('top-10').items.length,10);
  const before=cat.queued.length;
  // CatalogRemote reconfiguration clears the cache while remote stays enabled,
  // and notifies legacy revisionChanged without a queryChanged for every old key.
  cat.cache.clear();cat.failures.clear();cat.pending.clear();cat.revisionChanged.emit('source-invalidated');
  await pause();c.setVisible(['top-10']);await pause();
  assert.ok(cat.queued.length>before,'completed visible facts must revalidate after source cache invalidation');
  assert.equal(c.snapshot('top-10').items.length,0,'old unvalidated-source facts cannot stay eligible');
  h.cancel();
});

// Receiving 015: independently blocked exact vectors must not share a retry switch.
test('two failed disjoint facts vectors stay blocked until their own explicit retries',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts();
  const data=metas(80).map((m,i)=>({...m,releaseInfo:i<20?'2010':'2026'}));
  const h=startDeep(mod,cat,data),c=h.controller;
  c.setVisible(['top-10','recently-released']);
  const top=cat.queued[0];cat.fail(top,'TOP_503');await pause();
  const recent=cat.queued[1];
  assert.ok(recent && recent.every(x=>!top.includes(x)));
  cat.fail(recent,'RECENT_504');await pause();
  assert.equal(c.snapshot('top-10').error,'TOP_503');
  assert.equal(c.snapshot('recently-released').error,'RECENT_504');
  const before=cat.queued.length;
  c.setVisible(['recently-released','top-10']);await pause();
  assert.equal(cat.queued.length,before,'scroll cannot retry either failed key');
  assert.equal(c.retry('top-10'),true);
  assert.deepEqual(cat.retries[0],top);
  cat.succeed(top,toFacts(top,()=>false));await pause();
  assert.equal(c.snapshot('top-10').error,'');
  assert.equal(c.snapshot('recently-released').error,'RECENT_504');
  assert.equal(c.retry('recently-released'),true);
  assert.deepEqual(cat.retries[1],recent);
  cat.succeed(recent,toFacts(recent,()=>false));await pause();
  assert.equal(c.snapshot('recently-released').error,'');
  h.cancel();
  assert.equal(cat.revisionChanged.size,0,'completed revision listener disconnects');
});

test('validated stale vector is scoped to its fact consumers, not a disjoint recent shelf',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts();
  const data=metas(80).map((m,i)=>({...m,releaseInfo:i<20?'2010':'2026'}));
  const h=startDeep(mod,cat,data),c=h.controller;
  const top=data.slice(0,20).map(m=>m.id);
  cat.seedStale(top,toFacts(top,()=>false));
  c.setVisible(['top-10']);cat.fail(top,'OFFLINE_STALE');await pause();
  assert.equal(c.snapshot('top-10').stale,true);
  c.setVisible(['recently-released']);await pause();
  assert.ok(cat.queued.length>1,'stale offscreen vector cannot suppress new work');
  const recent=cat.queued[1];cat.succeed(recent,toFacts(recent,()=>false));await pause();
  assert.equal(c.snapshot('recently-released').stale,false);
  assert.equal(c.retry('recently-released'),false,'no unrelated stale retry');
  c.setVisible(['top-10']);
  assert.equal(c.snapshot('top-10').stale,true);
  assert.equal(c.retry('top-10'),true,'same exact stale vector is retryable');
  assert.deepEqual(cat.retries.at(-1),top);
  cat.succeed(top,toFacts(top,()=>false));await pause();
  assert.equal(c.snapshot('top-10').stale,false);
  h.cancel();
});

test('settled facts ignore unrelated revision, but revalidate exact invalidated native state',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(70)),c=h.controller;
  c.setVisible(['top-10']);const top=cat.queued[0];
  cat.succeed(top,toFacts(top,()=>false));await pause();
  const before=cat.queued.length,providerCount=h.requests.length;
  cat.revisionChanged.emit('unrelated-peer-success');await pause();
  assert.equal(cat.queued.length,before,'unrelated revision has no titleFacts reads');
  assert.equal(h.requests.length,providerCount,'unrelated revision never reloads Cinemeta');
  assert.equal(c.snapshot('top-10').items.length,10);
  cat.cache.clear();cat.failures.clear();cat.pending.clear();
  cat.revisionChanged.emit('published-new-source');await pause();
  assert.equal(c.snapshot('top-10').items.length,0,'old facts fenced while native cache is missing');
  assert.ok(cat.queued.length>before,'exact visible key reacquired for new publication');
  assert.equal(h.requests.length,providerCount,'facts revalidation does not reload providers');
  const refreshed=cat.queued.at(-1);
  cat.succeed(refreshed,toFacts(refreshed,()=>false));await pause();
  assert.equal(c.snapshot('top-10').items.length,10);
  h.cancel();
  assert.equal(cat.revisionChanged.size,0);
  assert.equal(cat.readyChanged.size,0);
});

test('pending source invalidation fences old completion and cancels subscriptions',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(55)),c=h.controller;
  c.setVisible(['top-10']);const first=cat.queued[0];
  cat.pending.clear();cat.cache.clear();
  cat.revisionChanged.emit('publication');await pause();
  assert.match(c.snapshot('top-10').error,/source.changed|invalidat/i);
  const before=cat.queued.length;
  cat.succeed(first,toFacts(first,()=>false));await pause();
  c.setVisible(['top-10']);await pause();
  assert.equal(c.snapshot('top-10').items.length,0,'old-source late results are never eligible');
  assert.equal(cat.queued.length,before,'terminal source fence does not silently retry');
  h.cancel();
  assert.equal(cat.remoteQueryChanged.size,0);
  assert.equal(cat.revisionChanged.size,0);
  assert.equal(cat.readyChanged.size,0);
});

test('exact cached remote-refresh failure marks only its own validated rows stale',async()=>{
  const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(75)),c=h.controller;
  c.setVisible(['top-10']);const ids=cat.queued[0];
  cat.succeed(ids,toFacts(ids,()=>false));await pause();
  assert.equal(c.snapshot('top-10').stale,false);
  cat.fail(ids,'CACHED_REFRESH_FAIL');
  cat.revisionChanged.emit('exact-cached-refresh-failed');await pause();
  assert.equal(c.snapshot('top-10').items.length,10,'validated cards remain painted');
  assert.equal(c.snapshot('top-10').stale,true,'exact native cached failure gets stale marker');
  assert.equal(c.snapshot('top-10').retryable,true);
  assert.equal(c.retry('top-10'),true);
  cat.succeed(ids,toFacts(ids,()=>false));await pause();
  assert.equal(c.snapshot('top-10').stale,false);
  h.cancel();
});

test('root receiving: publication identity invalidates settled same-key success replaced by another consumer',async()=>{
 const mod=moduleUnderTest(),cat=new FakeFacts(),h=startDeep(mod,cat,metas(60)),c=h.controller;
 c.setVisible(['top-10']);const first=cat.queued[0];
 cat.succeed(first,toFacts(first,()=>false));await pause();
 assert.deepEqual(ids(c.snapshot('top-10').items),metas(10).map(x=>x.id));
 const providerReads=h.requests.length;
 // Real CatalogRemote can replace this exact key while clearing other old entries.
 // status/key/cached are unchanged; only the canonical data-source revision changes.
 cat.sourceRevision='pub-b';
 cat.cache.set(cat.key(first),{value:toFacts(first,n=>n<6),stale:false});
 cat.remoteQueryChanged.emit(cat.key(first));cat.revisionChanged.emit('new-publication');await pause();await pause();
 assert.deepEqual(ids(c.snapshot('top-10').items),metas(16).slice(6).map(x=>x.id),
   'same success shape must not retain facts from old publication');
 assert.equal(h.requests.length,providerReads,'source revalidation does not reload provider');
 h.cancel();
});
