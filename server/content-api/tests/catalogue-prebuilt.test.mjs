import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {prebuiltCatalogue,selectionKey} from '../src/catalogue-prebuilt.ts';
const revision='1'.repeat(64),prefix='/internal/prebuilt/'+revision+'/';
function fixture(){
 const rows=Array.from({length:2300},(_,id)=>({id})),files=new Map();
 const manifest={version:1,revision,lists:{},fixed:{},search:{}};
 const parts=[];for(let i=0;i<rows.length;i+=1024){const path=prefix+i+'.json';parts.push(path);files.set(path,rows.slice(i,i+1024));}
 manifest.lists[selectionKey('mal','discoverPageFiltered',['popular',[],false])]={parts,length:rows.length,chunkSize:1024,shape:'mal',period:'stable'};
 manifest.lists[selectionKey('imdb','titleCatalog',[{}])]={parts,length:rows.length,chunkSize:1024,shape:'array',period:'stable'};
 manifest.lists[selectionKey('mal','animeCatalog',[{order:'members'}])]={parts,length:rows.length,chunkSize:1024,shape:'array',period:'stable'};
 const path=prefix+'records.json';files.set(path,[{identity:{id:13,title:'One Piece'},normal:{mal_id:13}},{identity:{id:14,title:'One Punch Man'}}]);
 const names=prefix+'names.json',keys=prefix+'keys.json';files.set(names,[['one piece',13,path,0,'one piece',1997,9,0],['one punch man',14,path,1,'one punch man',2009,8,1],['ワンピース',13,path,0,'one piece',1997,9,0]]);files.set(keys,[[13,path,0],[14,path,1]]);
 manifest.search.manga={names:[{path:names,first:'one piece',last:'ワンピース'}],keys:[{path:keys,first:13,last:14}]};
 files.set(prefix+'manifest.json',manifest);let reads=0;
 const assets={async fetch(req){reads++;const value=files.get(new URL(req.url).pathname);return value===undefined?new Response('',{status:404}):Response.json(value)}};
 return{assets,manifest,files,rows,get reads(){return reads}};
}
test('all deep offsets and arbitrary page sizes slice shared files exactly, including boundary and empty',async()=>{
 const f=fixture();
 for(const [offset,limit]of [[0,24],[1020,24],[2048,100],[2300,24],[100000,24],[-1,0]]){
  const off=Math.max(0,offset),lim=Math.min(100,Math.max(1,limit));
  const result=await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',['popular',[],false,offset,limit]);
  const items=f.rows.slice(off,off+lim);assert.deepEqual(result,{result:{items,nextOffset:off+items.length,exhausted:items.length<lim,freshness:'bundled',fallbackCatalog:''}});
 }
 const array=await prebuiltCatalogue(f.assets,revision,'imdb','titleCatalog',[{},1020,25]);assert.deepEqual(array.result,f.rows.slice(1020,1045));
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'imdb','titleCatalog',[{order:'votes'},0,24])).result,f.rows.slice(0,24));
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','animeCatalog',[{},0,24])).result,f.rows.slice(0,24));
});
test('prefix/exact/English-alias/wildcard searches share records and preserve native per-medium order',async()=>{
 const f=fixture();
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','search',['one',1,'manga'])).result.map(r=>r.id),[14]);
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','search',['one piece',20,'manga'])).result.map(r=>r.id),[13]);
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','search',['one p%',20,'manga'])).result,[]);
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','matchByTitle',['ワンピース',1997,'manga'])).result.map(r=>r.id),[13]);
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','matchByTitle',['one piece',2000,'manga'])).result,[]);
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','mangaById',[13])).result,{mal_id:13});
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'mal','mangaById',[999])).result,{});
});
test('rare combined filters and stale date-scoped lists fall through, corrupt chunks fail closed',async()=>{
 const f=fixture();
 assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',['popular',[{axis:'genre',key:'action'},{axis:'demographic',key:'seinen'}],false,0,24]),null);
 const stale=fixture();stale.manifest.lists[selectionKey('mal','discoverPageFiltered',['popular',[],false])].period='1900-01-01';
 assert.equal(await prebuiltCatalogue(stale.assets,revision,'mal','discoverPageFiltered',['popular',[],false,0,24]),null);
 const bad=fixture();bad.files.delete(prefix+'0.json');await assert.rejects(prebuiltCatalogue(bad.assets,revision,'mal','discoverPageFiltered',['popular',[],false,0,24]),/PREBUILT_ASSET_MISSING/);
});

function tankFixture(){
 const files=new Map(),fetched=[],hashOf=b=>createHash('sha256').update(b).digest('hex');
 const put=value=>{const raw=JSON.stringify(value),path=prefix+'shared/'+hashOf(raw)+'.json';files.set(path,raw);return path;};
 const envelope=id=>({apiVersion:1,sourceHash:'3'.repeat(64),ready:true,seriesInfo:{volumeCount:id,countBasis:'mal'},volumes:[{number:'1',cover:'',name:''}]});
 const key13=selectionKey('tankoban','series',[13]),key7=selectionKey('tankoban','series',[7]);
 const e13=envelope(113),e7=envelope(7),bucket13=hashOf(key13).slice(0,2),bucket7=hashOf(key7).slice(0,2);
 const coarse={};(coarse[bucket13]??={})[key13]=e13;(coarse[bucket7]??={})[key7]=e7;(coarse[bucket13]??={})['["comics","series",[5]]']={placeholder:true};
 const lookups={};for(const [bucket,map]of Object.entries(coarse))lookups[bucket]=put(map);
 const rangePath=put({[key13]:e13});
 const manifest={version:1,revision,lists:{},fixed:{},lookups,search:{},lookupRanges:{version:1,ranges:[{first:key13,last:key13,path:rangePath}]}};
 files.set(prefix+'manifest.json',JSON.stringify(manifest));let reads=0;
 const assets={async fetch(req){reads++;const path=new URL(req.url).pathname,raw=files.get(path);fetched.push(path);return raw===undefined?new Response('',{status:404}):new Response(raw);}};
 return{assets,files,fetched,manifest,key13,key7,e13,e7,rangePath,coarse13:lookups[bucket13],coarse7:lookups[bucket7],get reads(){return reads}};
}
test('tankoban fine ranges serve covered keys alone, never touching the coarse bucket',async()=>{
 const f=tankFixture();
 assert.deepEqual(await prebuiltCatalogue(f.assets,revision,'tankoban','series',[13]),{result:f.e13});
 assert.deepEqual(f.fetched,[prefix+'manifest.json',f.rangePath]);
});
test('tankoban outside, covered-but-absent and absent-field keys fall back to the coarse bucket',async()=>{
 const f=tankFixture();
 assert.deepEqual((await prebuiltCatalogue(f.assets,revision,'tankoban','series',[7])).result,f.e7);
 assert.ok(f.fetched.includes(f.coarse7),'key outside the ranges reads its coarse bucket');
 const g=tankFixture();g.files.set(g.rangePath,JSON.stringify({[g.key7]:g.e7}));
 assert.deepEqual((await prebuiltCatalogue(g.assets,revision,'tankoban','series',[13])).result,g.e13);
 assert.ok(g.fetched.includes(g.rangePath)&&g.fetched.includes(g.coarse13),'covered-but-absent key falls back to the coarse bucket');
 const h=tankFixture();delete h.manifest.lookupRanges;h.files.set(prefix+'manifest.json',JSON.stringify(h.manifest));
 assert.deepEqual((await prebuiltCatalogue(h.assets,revision,'tankoban','series',[13])).result,h.e13);
 assert.ok(h.fetched.includes(h.coarse13),'absent field keeps the coarse path');
 const i=tankFixture();
 assert.deepEqual((await prebuiltCatalogue(i.assets,revision,'tankoban','series',[99])).result,{apiVersion:1,sourceHash:undefined,ready:true,seriesInfo:{},volumes:[]});
});
test('malformed tankoban range declarations fail closed before any payload read',async()=>{
 const base=tankFixture(),{key13,key7,rangePath}=base;
 const variants={
  'null declaration':null,
  'wrong version':{version:2,ranges:[{first:key13,last:key13,path:rangePath}]},
  'ranges not an array':{version:1,ranges:{}},
  'reference not an object':{version:1,ranges:[42]},
  'missing path':{version:1,ranges:[{first:key13,last:key13}]},
  'reversed bounds':{version:1,ranges:[{first:key7,last:key13,path:rangePath}]},
  'unsorted ranges':{version:1,ranges:[{first:key7,last:key7,path:rangePath},{first:key13,last:key13,path:rangePath}]},
  'foreign path':{version:1,ranges:[{first:key13,last:key13,path:'/assets/content/'+rangePath.slice(rangePath.lastIndexOf('/')+1)}]},
  'over 512 references':{version:1,ranges:Array.from({length:513},()=>({first:key13,last:key13,path:rangePath}))}
 };
 for(const [name,declaration]of Object.entries(variants)){
  const f=tankFixture();f.manifest.lookupRanges=declaration;f.files.set(prefix+'manifest.json',JSON.stringify(f.manifest));
  await assert.rejects(prebuiltCatalogue(f.assets,revision,'tankoban','series',[13]),/PREBUILT_INVALID_LOOKUP_RANGES/,name);
  assert.deepEqual(f.fetched,[prefix+'manifest.json'],'no payload read for '+name);
 }
});

test('new manga shelf selections page prepared data and fall through on old publications',async()=>{
 for(const catalog of ['completed','publishing','short-completed','one-shots','started-1990s','started-2000s','started-2010s']){
  const f=fixture();
  assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',[catalog,[],false,0,24]),null);
  const g=fixture();
  g.manifest.lists[selectionKey('mal','discoverPageFiltered',[catalog,[],false])]=g.manifest.lists[selectionKey('mal','discoverPageFiltered',['popular',[],false])];
  assert.deepEqual((await prebuiltCatalogue(g.assets,revision,'mal','discoverPageFiltered',[catalog,[],false,1020,24])).result.items,g.rows.slice(1020,1044));
  assert.equal(await prebuiltCatalogue(g.assets,revision,'mal','discoverPageFiltered',[catalog,[],true,0,24]),null,'adult variants must not share results');
 }
});

test('format selection misses on old publications fall through, while known empty lists remain authoritative',async()=>{
 const format=[{axis:'format',key:'manhwa'}],f=fixture();
 assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',['popular',format,false,0,24]),null);
 assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPage',['popular','format','Manhwa',false,0,24]),null);
 assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',['popular',[{axis:'format',key:'novel'}],false,0,24]).then(p=>p.result.items.length),0);
 const key=selectionKey('mal','discoverPageFiltered',['popular',format,false]);
 const g=fixture();g.manifest.lists[key]={parts:[],length:0,chunkSize:256,shape:'mal',period:'stable'};
 assert.deepEqual((await prebuiltCatalogue(g.assets,revision,'mal','discoverPageFiltered',['popular',format,false,0,24])).result.items,[]);
 assert.equal(await prebuiltCatalogue(f.assets,revision,'mal','discoverPageFiltered',['popular',format,true,0,24]),null);
});
