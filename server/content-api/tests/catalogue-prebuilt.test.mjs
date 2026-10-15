import test from 'node:test';
import assert from 'node:assert/strict';
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
