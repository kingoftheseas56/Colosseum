import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=new URL('../',import.meta.url);
const delivery=fs.readFileSync(new URL('qml/CatalogDelivery.js',root),'utf8').replace(/^\.pragma.*$/gm,'');
const main=fs.readFileSync(new URL('qml/Main.qml',root),'utf8');
function extract(name) {
 const start=main.indexOf('    function '+name+'('); assert(start>=0,name);
 let braces=0,end=main.indexOf('{',start);
 for(;end<main.length;end++) { if(main[end]==='{')braces++; else if(main[end]==='}' && --braces===0) return main.slice(start,end+1); }
 throw Error('unclosed '+name);
}
function signal() { const slots=new Set; return {connect:f=>slots.add(f),disconnect:f=>slots.delete(f),emit(){for(const f of [...slots])f();},get count(){return slots.size;}}; }
function fixture() {
 const pending=new Set,cache=new Map;
 const catalog={available:()=>true,ready:()=>false,remoteEnabled:true,revisionChanged:signal(),requestFailed:signal(),remotePending:()=>pending.size>0,
  read(method,arg,empty) { const key=method+':'+String(arg); if(!cache.has(key))pending.add(key); return cache.get(key)??empty; },
  exactMatches(t){return this.read('exact',t,[]);},series(id){return this.read('series',id,{});},downloadsFor(id){return this.read('downloads',id,[]);},
  curatedSeries(id){return this.read('curated',id,{});},curatedByNorm(t){return this.read('norm',t,null);},
  complete(method,arg,result){const key=method+':'+String(arg);cache.set(key,result);pending.delete(key);this.revisionChanged.emit();}
 };
 const ctx={console,ComicsCatalog:catalog,currentSurface:'Tankoban',_catalogueRouteGeneration:0,_catalogueRouteCancel:null,
 westernLayer:{active:false,item:null},comicSeriesLayer:{active:false,item:null},comicsDbLoader:{active:false},
 ComicsDb:{ready:()=>true,seriesByTitle:t=>catalog.curatedByNorm(t)},
 shellEscapeState:()=>({world:'Tankoban'}),opened:[]};
 ctx.win=ctx;vm.createContext(ctx);vm.runInContext(delivery+'\nCatalogDelivery={available,wait};',ctx);
 for(const name of ['cancelCatalogueRoute','catalogueRouteSnapshot','comicsDbHit','openWestern','_openWesternReady','openGcdSeries','_openGcdSeriesReady','_openPinnedGcdSeries','openComicSeries','_openComicSeriesReady'])vm.runInContext(extract(name),ctx);
 return {ctx,catalog,pending};
}
{
 const {ctx,catalog}=fixture();ctx.openGcdSeries({gcdId:12,resumeChapterId:'post12'});
 assert.equal(ctx.westernLayer.active,false,'cold series cannot mount with transient identity');
 catalog.complete('series',12,{gcdId:12,title:'Exact Run',year:2020,cover:'cover'});
 assert.equal(ctx.westernLayer.active,false,'downloads must arrive as well');
 catalog.complete('downloads',12,[{postId:1,link:'https://example.test/1',title:'Issue One',kind:'single'}]);
 assert.equal(ctx.westernLayer.active,true);assert.equal(ctx.westernLayer.baked.gcdId,12);assert.equal(ctx.westernLayer.resumeChapterId,'post12');
 assert.equal(catalog.revisionChanged.count,0,'settled callback unsubscribes');
}
{
 const {ctx,catalog}=fixture();ctx.openGcdSeries({gcdId:1});ctx.openGcdSeries({gcdId:2});
 catalog.complete('series',1,{gcdId:1,title:'Old'});catalog.complete('downloads',1,[]);
 assert.equal(ctx.westernLayer.active,false,'superseded route remains closed');
 catalog.complete('series',2,{gcdId:2,title:'New'});catalog.complete('downloads',2,[]);
 assert.equal(ctx.westernLayer.baked.gcdId,2,'only latest route opens');
}
{
 const {ctx,catalog}=fixture();ctx.openGcdSeries({gcdId:3});ctx.cancelCatalogueRoute();
 catalog.complete('series',3,{gcdId:3,title:'Cancelled'});catalog.complete('downloads',3,[]);
 assert.equal(ctx.westernLayer.active,false,'Back cancellation never reopens');
 assert.equal(catalog.revisionChanged.count,0);
}
{
 const {ctx,catalog}=fixture();ctx.openGcdSeries({gcdId:4});ctx.currentSurface='Home';
 catalog.complete('series',4,{gcdId:4,title:'Late'});catalog.complete('downloads',4,[]);
 assert.equal(ctx.westernLayer.active,false,'navigation snapshot rejects late completion');
}
{
 const {ctx,catalog}=fixture();ctx.openWestern({title:'Curated'});
 catalog.complete('norm','Curated',{locgId:'locg:9',title:'Curated'});catalog.complete('exact','Curated',[]);
 assert.equal(ctx.comicSeriesLayer.active,false,'LOCG detail warms before mount');
 catalog.complete('curated','9',{locgId:'9',title:'Curated',editions:[]});
 assert.equal(ctx.comicSeriesLayer.active,true);assert.equal(ctx.comicSeriesLayer.locgSid,'locg:9');
}
{
 const {ctx,catalog}=fixture();ctx.openGcdSeries({gcdId:99,title:'Pinned',posts:[11,12],resumeChapterId:'12'});
 catalog.complete('series',99,{});catalog.complete('downloads',99,[]);
 assert.equal(ctx.westernLayer.baked.gcdId,99);assert.equal(ctx.westernLayer.baked.releases.length,2);assert.equal(ctx.westernLayer.resumeChapterId,'12');
}
{
 const {ctx,catalog}=fixture();const events=[];const item={packSeriesId:'old',gcdId:0};
 Object.defineProperty(item,'bakedReleases',{set(v){events.push(['baked',v===null?null:this.gcdId]);}});
 Object.defineProperty(item,'openChapterId',{set(v){events.push(['reader',this.gcdId,v]);}});
 ctx.westernLayer.active=true;ctx.westernLayer.item=item;ctx.openGcdSeries({gcdId:6,resumeChapterId:'resume'});
 catalog.complete('series',6,{gcdId:6,title:'Run'});catalog.complete('downloads',6,[]);
 assert.deepEqual(events.at(-1),['reader',6,'resume']);assert.deepEqual(events.at(-2),['baked',6],'reader opens after canonical baked identity');
}
assert(/function handleEscape\(\) \{\s+win\.cancelCatalogueRoute\(\);/.test(main),'Escape owns cancellation');
console.log('PASS actual Main catalogue functions: async dual reads, supersession, cancellation, navigation fence, curated warmup, pinned fallback, identity-before-reader');

// F2 delivery: execute the actual helper in an isolated QML-like VM. Native-style
// async notification is deferred, while state inspection is strictly read-only.
{
 const ctx=vm.createContext({console});
 vm.runInContext(delivery+'\nthis.waitQuery=waitQuery; this.legacyWait=wait;',ctx);
 const waitQuery=ctx.waitQuery;
 const tick=()=>new Promise(resolve=>setImmediate(resolve));
 const page=items=>({items,nextOffset:items.length,exhausted:items.length===0,freshness:'remote'});
 function asyncSignal() {
  const callbacks=new Set();let connects=0,disconnects=0;
  return {
   connect(fn){assert(!callbacks.has(fn),'duplicate connect');connects++;callbacks.add(fn);},
   disconnect(fn){assert(callbacks.has(fn),'duplicate disconnect');disconnects++;callbacks.delete(fn);},
   emit(...args){for(const fn of [...callbacks]) fn(...args);},
   defer(...args){setImmediate(()=>this.emit(...args));},
   get count(){return callbacks.size;},get connects(){return connects;},get disconnects(){return disconnects;}
  };
 }
 function remoteFixture() {
  const remoteQueryChanged=asyncSignal(),revisionChanged=asyncSignal(),readyChanged=asyncSignal(),requestFailed=asyncSignal();
  const catalog={remoteQueryChanged,revisionChanged,readyChanged,requestFailed,
   remotePending(){throw Error('query helper must not read global pending');},
   remoteWaiting(){throw Error('query helper must not read global waiting');}};
  const entries=new Map(),reads=[],inspections=[];
  let local=false,enabled=true,atCap=false,readHook=null,inspectHook=null;
  function install(id,key,initial={}) {
   const entry={id,key,status:initial.status||'missing',cache:initial.cache||null,
                error:initial.error||'',retryable:initial.retryable??false};
   entries.set(id,entry);
   // Emulate captured original method and five argument values, without JS key derivation.
   const args=[id,[{axis:'genre',key:id}],false,initial.offset??0,24];
   const read=()=>{
    reads.push(args);
    if(readHook) readHook(entry);
    if(local) return page([]);
    if(entry.status==='missing' && enabled && !atCap) {
     entry.status='pending';remoteQueryChanged.defer(key); // native QTimer::singleShot(0)
    }
    return entry.cache??page([]); // empty fallback is not necessarily success
   };
   const inspect=()=>{
    inspections.push(args);
    if(inspectHook) inspectHook(entry);
    if(local) return {source:'local',key:'',status:'success',enabled:false,pending:false,waiting:false,cached:false,error:'',retryable:false};
    return {source:'remote',key:entry.key,status:entry.status,enabled,
      pending:entry.status==='pending',waiting:entry.status==='pending'&&!entry.cache,
      cached:entry.cache!==null,error:entry.error,retryable:entry.retryable};
   };
   return {entry,args,read,inspect};
  }
  function success(id,value) {
   const e=entries.get(id);
   // Model one native completion turn: state mutation, exact key, THEN revision.
   setImmediate(()=>{e.cache=value;e.status='success';e.error='';e.retryable=false;
     remoteQueryChanged.emit(e.key);revisionChanged.emit();});
  }
  function failure(id,message) {
   const e=entries.get(id);
   setImmediate(()=>{e.status='failure';e.error=message;e.retryable=enabled&&!atCap;
     remoteQueryChanged.emit(e.key);requestFailed.emit('discoverPageFiltered',message);});
  }
  function reset(id,reloadedCache=null) {
   const e=entries.get(id);
   setImmediate(()=>{e.status=reloadedCache?'success':'missing';e.cache=reloadedCache;
     revisionChanged.emit();}); // configure emits no exact-key notification
  }
  function assertUnsubscribed() {
   for(const name of ['remoteQueryChanged','revisionChanged','readyChanged']) {
    assert.equal(catalog[name].count,0,name+' leaked');
    assert.equal(catalog[name].disconnects,catalog[name].connects,name+' disconnect count');
   }
  }
  return {catalog,install,success,failure,reset,entries,reads,inspections,assertUnsubscribed,
   setLocal(v){local=v;readyChanged.defer();},setEnabled(v){enabled=v;},setCap(v){atCap=v;},
   onRead(fn){readHook=fn;},onInspect(fn){inspectHook=fn;}};
 }
 let cases=0;
 function test(label,fn) {return Promise.resolve().then(fn).then(()=>{cases++;console.log('PASS keyed:',label);});}
 await test('exact-key peer isolation, unrelated failure, native deferred enqueue',async()=>{
  const f=remoteFixture();const first=f.install('popular','native-key-p0'),second=f.install('popular-page-2','native-key-p24',{offset:24});
  const a=[],b=[];const stopA=waitQuery(f.catalog,first.read,first.inspect,x=>a.push(x));
  const stopB=waitQuery(f.catalog,second.read,second.inspect,x=>b.push(x));
  assert.equal(a.length+b.length,0,'fallback cannot settle pending');
  assert.equal(f.reads[0],first.args,'first caller 5 args retained by reference');
  assert.equal(f.inspections[0],first.args,'state uses matching original args');
  await tick();assert.equal(a.length+b.length,0,'deferred enqueue notification is pending');
  const beforeUnrelated=f.inspections.length;
  f.catalog.remoteQueryChanged.emit('different-key');f.catalog.requestFailed.emit('discoverPageFiltered','UNRELATED');
  assert.equal(f.inspections.length,beforeUnrelated,'unrelated key cannot even inspect this page');
  f.failure('popular-page-2','HTTP_503');await tick();await tick();
  assert.equal(a.length,0,'other same-method page failure cannot finish first');
  assert.equal(b.length,1);assert.equal(b[0].ok,false);assert.equal(b[0].state.key,'native-key-p24');
  assert.equal(b[0].hasData,false);assert.equal(b[0].value,null,'uncached failure is not valid empty');
  assert.equal(b[0].error,'HTTP_503');assert.equal(b[0].state.retryable,true);
  f.success('popular',page([{id:12}]));await tick();await tick();
  assert.equal(a.length,1);assert.equal(a[0].ok,true);assert.equal(a[0].value.items[0].id,12);
  assert.equal(a[0].state.key,'native-key-p0');stopA();stopB();f.assertUnsubscribed();
 });
 await test('local empty is immediate success and needs no remote key',async()=>{
  const f=remoteFixture();f.setLocal(true);const p=f.install('local','unused');const events=[];
  const cancel=waitQuery(f.catalog,p.read,p.inspect,x=>events.push(x));
  assert.equal(events.length,1);assert.equal(events[0].ok,true);assert.equal(events[0].hasData,true);
  assert.equal(events[0].state.source,'local');assert.equal(events[0].value.items.length,0);
  await tick();cancel();f.assertUnsubscribed();
 });
 await test('valid remote empty success versus identical uncached fallback',async()=>{
  const f=remoteFixture();const valid=f.install('valid','key-valid');const cap=f.install('cap','key-cap');
  const ok=[],miss=[];const done=waitQuery(f.catalog,valid.read,valid.inspect,x=>ok.push(x));
  await tick();f.setCap(true);
  const cancel=waitQuery(f.catalog,cap.read,cap.inspect,x=>miss.push(x));
  assert.equal(miss.length,1);assert.equal(miss[0].ok,false);assert.equal(miss[0].hasData,false);
  assert.equal(miss[0].value,null);assert.equal(miss[0].reason,'missing');
  assert.equal(miss[0].state.enabled,true,'at-cap state remains enabled');
  f.success('valid',page([]));await tick();await tick();
  assert.equal(ok.length,1);assert.equal(ok[0].ok,true);assert.equal(ok[0].hasData,true);
  assert.deepEqual(Object.keys(ok[0].value.items),[]);assert.equal(ok[0].state.status,'success');
  done();cancel();f.assertUnsubscribed();
 });
 await test('pending cached refresh never completes early, cached failure preserves rows',async()=>{
  const f=remoteFixture();const cached=page([{id:1}]);const p=f.install('stale','key-stale',{cache:cached,status:'pending'});
  const events=[];const cancel=waitQuery(f.catalog,p.read,p.inspect,x=>events.push(x));
  await tick();f.catalog.revisionChanged.defer();await tick();
  assert.equal(events.length,0,'cached pending is not settled success or failure');
  f.failure('stale','HTTP_504');await tick();
  assert.equal(events.length,1);assert.equal(events[0].ok,false);assert.equal(events[0].hasData,true);
  assert.equal(events[0].value,cached,'validated cached cursor and rows survive refresh failure');
  assert.equal(events[0].state.retryable,true);assert.equal(events[0].state.status,'failure');
  cancel();f.assertUnsubscribed();
 });
 await test('pending cached refresh success returns new rows, never stale early',async()=>{
  const f=remoteFixture(),stale=page([{id:'old'}]),fresh=page([{id:'new'}]);
  const p=f.install('revalidate','key-revalidate',{cache:stale,status:'pending'}),out=[];
  waitQuery(f.catalog,p.read,p.inspect,x=>out.push(x));
  await tick();assert.equal(out.length,0,'pending stale cache cannot settle');
  f.success('revalidate',fresh);await tick();await tick();
  assert.equal(out.length,1);assert.equal(out[0].ok,true);assert.equal(out[0].value,fresh);
  assert.equal(out[0].value.items[0].id,'new');f.assertUnsubscribed();
 });
 await test('native key change rejects delivery even if old key signals',async()=>{
  const f=remoteFixture(),p=f.install('key-change','key-before'),out=[];
  waitQuery(f.catalog,p.read,p.inspect,x=>out.push(x));await tick();
  p.entry.key='key-after';f.catalog.remoteQueryChanged.emit('key-before');
  assert.equal(out.length,1);assert.equal(out[0].ok,false);
  assert.equal(out[0].reason,'source-changed');assert.equal(out[0].state.key,'key-after');
  f.assertUnsubscribed();
 });
 await test('disabled and no-signal pending cannot wait silently',async()=>{
  const f=remoteFixture();f.setEnabled(false);const p=f.install('offline','key-offline');const events=[];
  waitQuery(f.catalog,p.read,p.inspect,x=>events.push(x));
  assert.equal(events.length,1);assert.equal(events[0].reason,'disabled');f.assertUnsubscribed();
  const g=remoteFixture(),q=g.install('unwired','key-unwired');g.catalog.remoteQueryChanged={count:0,connects:0,disconnects:0};const outcomes=[];
  waitQuery(g.catalog,q.read,q.inspect,x=>outcomes.push(x));
  assert.equal(outcomes.length,1);assert.equal(outcomes[0].reason,'no-signal');
  await tick();g.assertUnsubscribed();
 });
 await test('cancel only own subscription, ignore late signals',async()=>{
  const f=remoteFixture();const p=f.install('cancel','key-cancel'),q=f.install('peer','key-peer');
  const a=[],b=[];const cancel=waitQuery(f.catalog,p.read,p.inspect,x=>a.push(x));
  const peer=waitQuery(f.catalog,q.read,q.inspect,x=>b.push(x));
  await tick();cancel();cancel();f.success('cancel',page([1]));f.success('peer',page([2]));
  await tick();await tick();
  assert.equal(a.length,0);assert.equal(b.length,1);assert.equal(b[0].value.items[0],2);
  peer();f.assertUnsubscribed();
 });
 await test('revision-only source reset invalidates pending key and unsubscribes',async()=>{
  const f=remoteFixture();const p=f.install('reset','key-reset');const events=[];
  const cancel=waitQuery(f.catalog,p.read,p.inspect,x=>events.push(x));await tick();
  f.reset('reset');await tick();
  assert.equal(events.length,1);assert.equal(events[0].ok,false);assert.equal(events[0].reason,'source-changed');
  assert.equal(events[0].state.status,'missing');
  f.catalog.remoteQueryChanged.defer('key-reset');await tick();assert.equal(events.length,1);
  cancel();f.assertUnsubscribed();
 });
 await test('revision-only source reset with newly loaded cache is not old request success',async()=>{
  const f=remoteFixture(),p=f.install('reload','key-reload'),events=[];
  waitQuery(f.catalog,p.read,p.inspect,x=>events.push(x));await tick();
  f.reset('reload',page([{id:'new-source'}]));await tick();
  assert.equal(events.length,1);assert.equal(events[0].ok,false);
  assert.equal(events[0].reason,'source-changed');assert.equal(events[0].hasData,false);
  assert.equal(events[0].state.status,'success','native snapshot exposed but not old result');
  f.assertUnsubscribed();
 });
 await test('local source takeover invalidates previous remote subscription',async()=>{
  const f=remoteFixture();const p=f.install('takeover','key-takeover');const out=[];
  waitQuery(f.catalog,p.read,p.inspect,x=>out.push(x));await tick();f.setLocal(true);await tick();
  assert.equal(out.length,1);assert.equal(out[0].reason,'source-changed');
  assert.equal(out[0].hasData,false);assert.equal(out[0].state.source,'local');f.assertUnsubscribed();
 });
 await test('reentrant exact signal and callback exception unsubscribe once',async()=>{
  const f=remoteFixture();const p=f.install('reentrant','key-reentrant');let triggered=false,delivered=0;
  f.onInspect(entry=>{
   if(entry.status==='success'&&!triggered){triggered=true;f.catalog.remoteQueryChanged.emit(entry.key);}
  });
  waitQuery(f.catalog,p.read,p.inspect,x=>{delivered++;assert.equal(x.ok,true);});
  await tick();f.success('reentrant',page([3]));await tick();await tick();
  assert.equal(delivered,1);f.assertUnsubscribed();
  const g=remoteFixture(),q=g.install('throwing','key-throw');
  waitQuery(g.catalog,q.read,q.inspect,()=>{throw Error('CONSUMER_ERROR');});
  await tick();q.entry.status='success';q.entry.cache=page([4]);
  assert.throws(()=>g.catalog.remoteQueryChanged.emit('key-throw'),/CONSUMER_ERROR/);
  g.assertUnsubscribed();
 });
 await test('done callback can reenter new page subscription without crosstalk',async()=>{
  const f=remoteFixture(),first=f.install('a','re-a'),second=f.install('b','re-b'),events=[];
  let cancelB=null;
  waitQuery(f.catalog,first.read,first.inspect,x=>{
   events.push(['a',x.ok]);
   assert.equal(f.catalog.remoteQueryChanged.count,0,'first listener removed before callback');
   cancelB=waitQuery(f.catalog,second.read,second.inspect,y=>events.push(['b',y.ok]));
  });
  await tick();f.success('a',page([1]));await tick();await tick();
  assert.deepEqual(events,[['a',true]]);
  f.success('b',page([2]));await tick();await tick();
  assert.deepEqual(events,[['a',true],['b',true]]);
  cancelB();f.assertUnsubscribed();
 });
 await test('read and inspect exceptions are explicit terminal failure',async()=>{
  const f=remoteFixture(),p=f.install('bad-read','key-bad-read');const a=[];
  waitQuery(f.catalog,()=>{throw Error('READ_BROKEN');},p.inspect,x=>a.push(x));
  assert.equal(a.length,1);assert.equal(a[0].ok,false);assert.equal(a[0].reason,'exception');
  assert.match(a[0].error,/READ_BROKEN/);f.assertUnsubscribed();
  const g=remoteFixture(),q=g.install('bad-state','key-bad-state'),b=[];
  waitQuery(g.catalog,q.read,()=>{throw Error('INSPECT_BROKEN');},x=>b.push(x));
  assert.equal(b.length,1);assert.equal(b[0].reason,'exception');g.assertUnsubscribed();
  await tick();
 });
 console.log(`PASS F2 exact-query delivery: ${cases} asynchronous semantic cases`);
}
