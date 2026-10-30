import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';
const source = n => fs.readFileSync(new URL('../qml/'+n,import.meta.url),'utf8').replace(/^\.(pragma|import).*$/gm,'');
const module = (n, deps={}) => { const c=vm.createContext(deps); vm.runInContext(source(n),c); return c; };
const Rules=module('MangaBrowseRules.js'), Policy=module('ExplicitContentPolicy.js');
const Delivery=module('CatalogDelivery.js');
const Browse=module('MangaBrowseDelivery.js',{Rules,Policy,Delivery});
const plain=x=>JSON.parse(JSON.stringify(x));
const titles=(start=1,n=24)=>Array.from({length:n},(_,i)=>({mal_id:start+i,title:'Title '+(start+i),cover:'',explicit:false}));
const signal=()=>{const slots=new Set(); return {connect:f=>slots.add(f),disconnect:f=>slots.delete(f),emit:(...a)=>[...slots].forEach(f=>f(...a)),get count(){return slots.size}}};
function catalog(local=true) {
 const c={remoteEnabled:!local,ready:()=>local,calls:[],retries:[],landingReads:0,remoteQueryChanged:signal(),revisionChanged:signal(),readyChanged:signal(),state:{source:'remote',key:'landing:manga',status:'success',cached:true},
 landing(){++this.landingReads;return {entries:[]}},landingState(){return this.state},retryLanding(tab){this.retries.push(['landing',tab])},retryDiscoverPageFiltered(...args){this.retries.push(args)},
 discoverPageFiltered(...args){this.calls.push(args);return {items:titles(),nextOffset:24,exhausted:false}},
 discoverPageFilteredState(){return local?{source:'local',status:'success'}:{source:'remote',key:'page',status:'success',cached:true}}};return c;
}
test('eight exact shelves and pins share Discover catalogue IDs',()=>{
 const api=module('TankobanDiscoverApi.js',{CatalogDelivery:Delivery,Policy});
 assert.equal(Rules.defaultRows().length,8);
 for(const row of Rules.defaultRows()) {assert.ok(api.MANGA_CATALOGS.some(c=>c.key===row.catalogId));assert.equal(Rules.pin(row).catalogId,row.catalogId)}
});
test('presentation enforces canonical numeric IDs, deduplication, explicit policy and minimum three',()=>{
 const rows=Rules.defaultRows();const items=[...titles(1,4),{mal_id:'5',title:'bad'},{mal_id:1,title:'duplicate'},{mal_id:6,explicit:true},{mal_id:0},{mal_id:2.5}];
 const out=Rules.present(rows,{popular:{items},completed:{items:titles(1,2)}},false,Policy.visible);
 assert.deepEqual(plain(out[0].items.map(x=>x.id)),['1','2','3','4']);assert.equal(out.length,1);
 assert.equal(out[0].items[0].availability,false);assert.equal(out[0].items[0].raw,items[0]);
 assert.equal(Rules.present(rows,{popular:{items}},true,Policy.visible)[0].items.length,5);
});
test('stable diversity ordering, bounded cards and source immutability',()=>{
 const items=titles(), before=JSON.stringify(items), pages={};for(const row of Rules.defaultRows())pages[row.key]={items};
 const out=Rules.present(Rules.defaultRows(),pages,false,Policy.visible);
 assert.equal(out.length,8);assert.equal(out[0].items.length,10);assert.equal(out[1].items.length,20);
 assert.equal(out[1].items[0].id,'11');assert.equal(JSON.stringify(items),before);
 assert.deepEqual(plain(out),plain(Rules.present(Rules.defaultRows(),pages,false,Policy.visible)));
});
test('local catalogue reads only eight bounded exact pages and never landing',()=>{
 const c=catalog();let result;Browse.load(c,false,r=>result=r);
 assert.equal(c.calls.length,8);assert.equal(c.landingReads,0);assert.equal(result.rows.length,8);assert.equal(result.error,'');
 assert.deepEqual(c.calls.map(x=>[x[0],x[2],x[3],x[4]]),plain(Rules.defaultRows()).map(d=>[d.catalogId,false,0,24]));
});
test('remote landing success reads primed exact pages; explicit partitions stay separate',()=>{
 const c=catalog(false);let result;Browse.load(c,true,r=>result=r);
 assert.equal(c.landingReads,1);assert.equal(c.calls.length,8);assert.ok(c.calls.every(x=>x[2]===true));assert.equal(result.rows.length,8);
});
test('old publication/first-run failure preserves fallback without eight unsupported requests',()=>{
 const c=catalog(false);c.state={...c.state,status:'failure',cached:false,error:'not published'};let result;
 Browse.load(c,false,r=>result=r);assert.equal(result.error,'not published');assert.equal(result.rows.length,0);assert.equal(c.calls.length,0);
});
test('stale cached landing supplies rows with offline notice',()=>{
 const c=catalog(false);c.state={...c.state,status:'failure',error:'offline'};let result;Browse.load(c,false,r=>result=r);
 assert.equal(result.rows.length,8);assert.equal(result.error,'offline');
});
test('cancellation detaches exact-key subscription and ignores late landing completion',()=>{
 const c=catalog(false);c.state={...c.state,status:'pending',cached:false};let count=0;
 const cancel=Browse.load(c,false,()=>++count);assert.equal(c.remoteQueryChanged.count,1);cancel();assert.equal(c.remoteQueryChanged.count,0);
 c.state={...c.state,status:'success',cached:true};c.remoteQueryChanged.emit(c.state.key);assert.equal(count,0);assert.equal(c.calls.length,0);
});
test('explicit retry delegates exact landing and page keys',()=>{
 const c=catalog(false);Browse.load(c,false,()=>{},true);assert.equal(c.retries.length,9);assert.deepEqual(c.retries[0],['landing','manga']);
 assert.deepEqual(c.retries.slice(1),c.calls);
});
test('new See-all shelves never replace predicate results with a generic live Jikan page',()=>{
 const api=module('TankobanDiscoverApi.js',{CatalogDelivery:Delivery,Policy});const c=catalog();let xhrs=0;
 const adapter=api.create(c,null,[],false,()=>{++xhrs;throw new Error('unexpected live fetch')});
 for(const row of Rules.defaultRows().slice(1)) {
  let page;adapter.fetchPage({type:'manga',catalogKey:row.catalogId,filters:{},filterGroup:'',filterKey:''},null,1,(gen,p)=>page=p);
  assert.ok(page);assert.equal(page.items.length,24);
 }
 assert.equal(xhrs,0);
});
test('validated static last-good landing works without an enabled network manager',()=>{
 const c=catalog(false);c.remoteEnabled=false;c.available=()=>true;let result;
 Browse.load(c,false,r=>result=r);assert.equal(result.rows.length,8);assert.equal(result.error,'');
});
