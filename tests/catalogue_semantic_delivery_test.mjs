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
