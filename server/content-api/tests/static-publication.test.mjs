import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,mkdirSync,writeFileSync,readFileSync,readdirSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {createHash} from 'node:crypto';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {buildPublicCatalogue,retainArtwork} from '../projection/public_static.mjs';
import {packOffsets,packLookupFiles,SEARCH_SHARD_BYTES} from '../projection/shard_packing.mjs';
import {prebuiltCatalogue,selectionKey} from '../src/catalogue-prebuilt.ts';
const hash=b=>createHash('sha256').update(b).digest('hex');
const sourceRevision='1'.repeat(64),prefix='/internal/prebuilt/'+sourceRevision+'/';
function fixture(t,{withImdb=false,imdbRows,withTankoban=false}={}){
 const root=mkdtempSync(join(tmpdir(),'colosseum-i05-'));t.after(()=>rmSync(root,{recursive:true,force:true}));
 const source=join(root,'source'),content=join(root,'content'),output=join(root,'output');
 const entries=[],write=(base,path,raw)=>{const target=join(base,path);mkdirSync(dirname(target),{recursive:true});writeFileSync(target,raw);};
 const put=value=>{const raw=JSON.stringify(value),path=prefix+'shared/'+hash(raw)+'.json';write(source,path,raw);entries.push({path,bytes:Buffer.byteLength(raw)});return path;};
 const records=put([{identity:{id:13,title:'One Piece'}}]);
 const names=put([['one piece',13,records,0,'one piece',1997,9,0]]);
 const keys=put([[13,records,0]]),rows=put([{id:1},{id:2}]);
 const imdbSourceRows=imdbRows??[
  ['tt0000001',null,0,{identity:{id:'tt0000001',title:'Alpha',rating:8.4,votes:100,isAnime:false},ordinal:2}],
  ['tt0000002',null,0,{identity:{id:'tt0000002',title:'Anime',rating:9.1,votes:220,isAnime:true},ordinal:0}],
  ['tt0000003',null,0,{identity:{id:'tt0000003',title:'Unknown rating',rating:null,votes:null,isAnime:false},ordinal:1}]
 ];
 const imdbKeys=withImdb?put(imdbSourceRows):null;
 const list=put({[selectionKey('imdb','titleCatalog',[{}])]:{parts:[rows],length:2,chunkSize:256,shape:'array',period:'stable'}});
 const tankKey=selectionKey('tankoban','series',[13]);
 const tankEnvelope={apiVersion:1,sourceHash:'5'.repeat(64),ready:true,seriesInfo:{volumeCount:113,countBasis:'mal'},volumes:[{number:'1',cover:'',name:''}]};
 const lookups={};let tankRangePath=null;
 if(withTankoban){
  lookups[hash(tankKey).slice(0,2)]=put({[tankKey]:tankEnvelope,'["comics","series",[9]]':{placeholder:true}});
  tankRangePath=put({[tankKey]:tankEnvelope});
 }
 const manifest={version:1,revision:sourceRevision,lists:{},fixed:{},listBuckets:{[hash(selectionKey('imdb','titleCatalog',[{}])).slice(0,2)]:list},lookups,search:{manga:{names:[{path:names,first:'one piece',last:'one piece'}],keys:[{path:keys,first:13,last:13}]},...(withImdb?{imdb:{names:[],keys:[{path:imdbKeys,first:'tt0000001',last:'tt0000003'}]}}:{})},...(withTankoban?{lookupRanges:{version:1,ranges:[{first:tankKey,last:tankKey,path:tankRangePath}]}}:{})};
 const raw=JSON.stringify(manifest);write(source,prefix+'manifest.json',raw);entries.push({path:prefix+'manifest.json',bytes:Buffer.byteLength(raw)});
 const reportObject={revision:sourceRevision,date:'2026-10-15',year:'2026',files:entries.length,listSelections:1,source:{mal:{sha256:'2'.repeat(64)}},entries};
 const writeManifest=()=>{const next=JSON.stringify(manifest);write(source,prefix+'manifest.json',next);entries.find(e=>e.path===prefix+'manifest.json').bytes=Buffer.byteLength(next);write(source,'static-catalogue-report.json',JSON.stringify(reportObject));};
 write(source,'static-catalogue-report.json',JSON.stringify(reportObject));
 const document='\n'+JSON.stringify({schemaVersion:1,revision:'doc-1',data:{title:'Universe',image:'https://provider.example/cover.jpg'}},null,2)+'\n';
 const artwork=Buffer.from('artwork-fixture'),artPath='/assets/content/'+hash(artwork)+'.webp';write(content,artPath,artwork);
 const cm={schemaVersion:1,revision:'3'.repeat(64),documents:{'universe-test':{revision:'doc-1',sha256:hash(document)}},assets:{'assets/test.png':{path:artPath,sha256:hash(artwork),bytes:artwork.length}}};
 write(content,'internal/content/manifest.json',JSON.stringify(cm));write(content,'internal/content/documents/universe-test.json',document);
 write(content,'internal/content/rpc-bundle.json',JSON.stringify({apiVersion:1,revision:cm.revision,method:'bundle',result:{manifest:cm,documents:{'universe-test':document}}}));
 return{root,source,content,output,manifest,entries,records,artPath,write,writeManifest,imdbKeys,imdbSourceRows,tankKey,tankEnvelope,tankRangePath};
}
test('public graph is hash-addressed, preserves old Worker results and shares one payload tree',async t=>{
 const f=fixture(t),report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const entry=JSON.parse(readFileSync(join(f.output,'catalogue/current.json'))),raw=readFileSync(join(f.output,entry.index.path));
 assert.equal(hash(raw),entry.index.sha256);assert.equal(raw.length,entry.index.bytes);assert.equal(entry.schemaVersion,1);
 const index=JSON.parse(raw);assert.equal(index.revision,entry.revision);assert.equal(index.sourceRevision,sourceRevision);
 const paths=[];function check(v){if(typeof v==='string'&&v.startsWith('/catalogue/')){paths.push(v);const bytes=readFileSync(join(f.output,v));assert.equal(hash(bytes),v.match(/([a-f0-9]{64})\.json$/)[1]);check(JSON.parse(bytes));}else if(Array.isArray(v))v.forEach(check);else if(v&&typeof v==='object')Object.values(v).forEach(check);}check(index);
 assert.ok(paths.length>5);assert.ok(!readFileSync(join(f.output,entry.index.path),'utf8').includes('/internal/'));
 assert.deepEqual(readdirSync(join(f.output,'internal/prebuilt',sourceRevision)),['manifest.json']);
 const assets={async fetch(req){try{return new Response(readFileSync(join(f.output,new URL(req.url).pathname)));}catch{return new Response('',{status:404});}}};
 assert.deepEqual((await prebuiltCatalogue(assets,sourceRevision,'imdb','titleCatalog',[{},1,24])).result,[{id:2}]);
 assert.deepEqual((await prebuiltCatalogue(assets,sourceRevision,'mal','search',['one',20,'manga'])).result,[{id:13,title:'One Piece'}]);
 assert.ok(report.files<20000);assert.equal(report.artworkFiles,1);assert.equal(report.contentDocuments,1);
 assert.equal(readFileSync(join(f.output,f.artPath)).toString(),'artwork-fixture');
 const docRaw=readFileSync(join(f.output,index.content.documents['universe-test'].path));assert.equal(hash(docRaw),index.content.documents['universe-test'].sha256);
 const doc=JSON.parse(docRaw);assert.equal(doc.data.image,'https://provider.example/cover.jpg');
 const headers=readFileSync(join(f.output,'_headers'),'utf8');assert.match(headers,/max-age=31536000, immutable/);assert.match(headers,/max-age=60, must-revalidate/);
});
test('deterministic generation; changed content yields a new immutable publication revision',t=>{
 const f=fixture(t),one=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const two=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:join(f.root,'second')});assert.equal(one.revision,two.revision);assert.equal(one.index.sha256,two.index.sha256);
 const cmPath=join(f.content,'internal/content/manifest.json'),cm=JSON.parse(readFileSync(cmPath));cm.revision='4'.repeat(64);writeFileSync(cmPath,JSON.stringify(cm));
 // An inconsistent bundle must fail, rather than publishing a mixed generation.
 assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:join(f.root,'mixed')}),/CONTENT_BUNDLE_MISMATCH/);
 const bp=join(f.content,'internal/content/rpc-bundle.json'),bundle=JSON.parse(readFileSync(bp));bundle.revision=cm.revision;bundle.result.manifest=cm;writeFileSync(bp,JSON.stringify(bundle));
 const three=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:join(f.root,'changed')});assert.notEqual(one.revision,three.revision);
});
test('corrupt, missing, unlisted and unsafe input files fail before any usable entry is published',t=>{
 for(const kind of ['hash','missing','unlisted','path']){
  const f=fixture(t);
  if(kind==='hash')f.write(f.source,f.records,'[]');
  if(kind==='missing')rmSync(join(f.source,f.records));
  const reportPath=join(f.source,'static-catalogue-report.json'),report=JSON.parse(readFileSync(reportPath));
  if(kind==='unlisted')report.entries=report.entries.filter(e=>e.path!==f.records);
  if(kind==='path')report.entries[0].path='/internal/prebuilt/'+sourceRevision+'/../../bad.json';
  writeFileSync(reportPath,JSON.stringify(report));
  assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output}),/SOURCE_|ENOENT/);
  assert.throws(()=>readFileSync(join(f.output,'catalogue/current.json')));
 }
});
test('file and byte budgets include content, artwork, compatibility manifest and controls',t=>{
 const f=fixture(t);
 assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output,maxFiles:3}),/FREE_FILE_LIMIT/);
 const g=fixture(t);assert.throws(()=>buildPublicCatalogue({source:g.source,content:g.content,previous:g.content,output:g.output,maxFileBytes:12}),/FREE_FILE_SIZE/);
});
test('legacy compatibility refuses a shard from any other public generation',async t=>{
 const f=fixture(t);buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const path=join(f.output,prefix+'manifest.json'),manifest=JSON.parse(readFileSync(path));
 manifest.fixed[selectionKey('comics','curatedReady',[])]=`/catalogue/${'0'.repeat(64)}/shared/${'f'.repeat(64)}.json`;writeFileSync(path,JSON.stringify(manifest));
 const assets={async fetch(req){return new Response(readFileSync(join(f.output,new URL(req.url).pathname)));}};
 await assert.rejects(prebuiltCatalogue(assets,sourceRevision,'comics','curatedReady',[]),/INVALID_PREBUILT_PATH/);
});
test('optional IMDb facts shards are versioned, hashed and leave generic title identity intact',t=>{
 const f=fixture(t,{withImdb:true});
 const report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const index=JSON.parse(readFileSync(join(f.output,report.index.path)));
 assert.equal(index.facts.imdb.version,1);
 assert.equal(index.facts.imdb.blocks.length,1);
 assert.equal(report.factsImdbBlocks,1);
 assert.equal(index.facts.imdb.hashBuckets.version,1);
 assert.equal(index.facts.imdb.hashBuckets.keyHash,'sha256-utf8');
 assert.equal(index.facts.imdb.hashBuckets.prefixChars,2);
 assert.equal(Object.keys(index.facts.imdb.hashBuckets.paths).length,256);
 assert.equal(report.factsImdbHashBuckets,256);
 const ref=index.facts.imdb.blocks[0],raw=readFileSync(join(f.output,ref.path));
 assert.equal(hash(raw),ref.path.match(/([a-f0-9]{64})\.json$/)[1]);
 assert.equal(ref.first,'tt0000001');assert.equal(ref.last,'tt0000003');
 assert.deepEqual(JSON.parse(raw),[
  ['tt0000001',8.4,100,false,2],['tt0000002',9.1,220,true,0],
  ['tt0000003',null,null,false,1]
 ]);
 const generic=JSON.parse(readFileSync(join(f.output,index.search.imdb.keys[0].path)));
 assert.deepEqual(generic,f.imdbSourceRows,'titlePresentation and legacy lookups keep full identity records');
 assert.ok(raw.length<Buffer.byteLength(JSON.stringify(generic)));
 assert.ok(report.entries.some(e=>e.path===ref.path && e.bytes===raw.length),
  'shared facts shard participates in file and byte accounting');
 const entry=JSON.parse(readFileSync(join(f.output,'catalogue/current.json')));
 assert.equal(hash(readFileSync(join(f.output,entry.index.path))),entry.index.sha256);
});
test('legacy publisher without IMDb key directory leaves optional facts absent',t=>{
 const f=fixture(t);
 const report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const index=JSON.parse(readFileSync(join(f.output,report.index.path)));
 assert.ok(!Object.hasOwn(index,'facts'));assert.equal(report.factsImdbBlocks,0);
 assert.equal(report.factsImdbHashBuckets,0);assert.equal(report.factsImdbHashBucketFiles,0);
});
test('invalid compact input fails before publishing any usable current pointer',t=>{
 const valid=[
  ['tt0000001',null,0,{identity:{rating:8.4,votes:100,isAnime:false},ordinal:2}],
  ['tt0000002',null,0,{identity:{rating:9.1,votes:220,isAnime:true},ordinal:0}],
  ['tt0000003',null,0,{identity:{rating:null,votes:null,isAnime:false},ordinal:1}]
 ];
 const variants=[
  {name:'duplicate-id',rows:[valid[0],[...valid[1].slice(0,0),valid[0][0],...valid[1].slice(1)],valid[2]]},
  {name:'wrong-range',rows:[valid[1],valid[2]]},
  {name:'negative-votes',rows:[valid[0],['tt0000002',null,0,{identity:{rating:9.1,votes:-1,isAnime:true},ordinal:0}],valid[2]]},
  {name:'missing-votes',rows:[valid[0],['tt0000002',null,0,{identity:{rating:9.1,isAnime:true},ordinal:0}],valid[2]]},
  {name:'bad-ordinal',rows:[valid[0],['tt0000002',null,0,{identity:{rating:9.1,votes:220,isAnime:true},ordinal:-1}],valid[2]]}
 ];
 for(const v of variants){
  const f=fixture(t,{withImdb:true,imdbRows:v.rows});
  assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output}),
   /SOURCE_INVALID_FACTS_/,v.name);
  assert.throws(()=>readFileSync(join(f.output,'catalogue/current.json')),undefined,v.name);
 }
});

test('IMDb facts hash buckets: exact ordered 256 keys, hash membership, full coverage and nullable tuples',t=>{
 const f=fixture(t,{withImdb:true});
 const report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const index=JSON.parse(readFileSync(join(f.output,report.index.path))),hb=index.facts.imdb.hashBuckets;
 assert.deepEqual(Object.keys(hb.paths).sort(),Array.from({length:256},(_,i)=>i.toString(16).padStart(2,'0')));
 const prefix='/catalogue/'+index.revision+'/shared/',tuples=new Map(),distinct=new Set(),emptyPaths=new Set();
 for(const key of Object.keys(hb.paths)){
  const path=hb.paths[key];distinct.add(path);
  assert.match(path,/^\/catalogue\/[a-f0-9]{64}\/shared\/[a-f0-9]{64}\.json$/,key);
  assert.ok(path.startsWith(prefix),key);
  const raw=readFileSync(join(f.output,path));
  assert.equal(hash(raw),path.slice(-69,-5),key);
  assert.equal(report.entries.filter(e=>e.path===path&&e.bytes===raw.length).length,1,'accounted '+key);
  const body=JSON.parse(raw);assert.ok(Array.isArray(body),key);
  if(raw.toString('utf8')==='[]')emptyPaths.add(path);
  let previous='';
  for(const tuple of body){
   assert.equal(tuple.length,5,key);
   assert.equal(hash(Buffer.from(tuple[0],'utf8')).slice(0,2),key,'sha256 membership '+tuple[0]);
   assert.ok(tuple[0]>previous,'strict tt order within '+key);previous=tuple[0];
   assert.ok(tuple[1]===null||(typeof tuple[1]==='number'&&Number.isFinite(tuple[1])),tuple[0]);
   assert.ok(tuple[2]===null||(Number.isSafeInteger(tuple[2])&&tuple[2]>=0),tuple[0]);
   assert.equal(typeof tuple[3],'boolean',tuple[0]);
   assert.ok(Number.isSafeInteger(tuple[4])&&tuple[4]>=0,tuple[0]);
   assert.ok(!tuples.has(tuple[0]),'duplicate '+tuple[0]);tuples.set(tuple[0],tuple);
  }
 }
 assert.equal(emptyPaths.size,1,'empty bucket bodies deduplicate to one shared file');
 assert.equal(distinct.size,report.factsImdbHashBucketFiles);
 assert.deepEqual([...tuples.keys()].sort(),['tt0000001','tt0000002','tt0000003']);
 assert.deepEqual(tuples.get('tt0000001'),['tt0000001',8.4,100,false,2]);
 assert.deepEqual(tuples.get('tt0000002'),['tt0000002',9.1,220,true,0]);
 assert.deepEqual(tuples.get('tt0000003'),['tt0000003',null,null,false,1]);
 assert.equal(tuples.size,f.imdbSourceRows.length,'full source tuple coverage exactly once');
 assert.equal(index.facts.imdb.blocks.length,1,'aligned V1 block retained beside buckets');
 assert.deepEqual(JSON.parse(readFileSync(join(f.output,index.facts.imdb.blocks[0].path))),
  ['tt0000001','tt0000002','tt0000003'].map(tt=>tuples.get(tt)),'blocks and buckets carry identical tuples');
});
test('hash-bucket facility changes the immutable public revision; sourceRevision and legacy derivation stay',t=>{
 const identity=report=>({contract:'i05-static-v1',sourceManifestSha256:report.sourceManifestSha256,contentManifestSha256:report.contentManifestSha256,retainedArtworkSha256:report.retention.inventorySha256,date:report.date,year:report.year});
 const legacy=fixture(t),legacyReport=buildPublicCatalogue({source:legacy.source,content:legacy.content,previous:legacy.content,output:legacy.output});
 const legacyIndex=JSON.parse(readFileSync(join(legacy.output,legacyReport.index.path)));
 assert.ok(!Object.hasOwn(legacyIndex,'facts'));assert.equal(legacyReport.factsImdbHashBuckets,0);
 assert.equal(legacyReport.revision,hash(Buffer.from(JSON.stringify(identity(legacyReport)))),'legacy identity derivation unchanged');
 const f=fixture(t,{withImdb:true}),report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const index=JSON.parse(readFileSync(join(f.output,report.index.path)));
 const expected=hash(Buffer.from(JSON.stringify({...identity(report),factsContract:'imdb-v1',factsHashContract:'imdb-sha256-utf8-prefix2'})));
 assert.equal(report.revision,expected);assert.equal(index.revision,expected);
 assert.notEqual(report.revision,hash(Buffer.from(JSON.stringify(identity(report)))),'bucket facility is an identity discriminator');
 assert.equal(report.sourceRevision,sourceRevision);assert.equal(index.sourceRevision,sourceRevision);
 const two=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:join(f.root,'second')});
 assert.equal(two.revision,report.revision);assert.equal(two.index.sha256,report.index.sha256);
});
test('hash buckets share content addressing and free file accounting without retention regression',t=>{
 const f=fixture(t,{withImdb:true}),previous=join(f.root,'previous'),old=Buffer.from('historical bucket-era PNG');
 const path='/assets/content/'+hash(old)+'.png';f.write(previous,path,old);
 const report=buildPublicCatalogue({source:f.source,content:f.content,output:f.output,previous});
 assert.deepEqual(readFileSync(join(f.output,path)),old);
 assert.equal(report.artworkFiles,2);assert.equal(report.retention.requiredFiles,1);assert.equal(report.retention.copiedFiles,1);
 assert.equal(report.factsImdbHashBuckets,256);
 const index=JSON.parse(readFileSync(join(f.output,report.index.path)));
 const distinct=new Set(Object.values(index.facts.imdb.hashBuckets.paths));
 assert.equal(report.factsImdbHashBucketFiles,distinct.size);
 assert.equal(report.files,report.entries.length);
 assert.equal(new Set(report.entries.map(e=>e.path)).size,report.entries.length,'each output file is accounted once');
 for(const bucketPath of distinct)assert.ok(report.entries.some(e=>e.path===bucketPath),'bucket accounted '+bucketPath);
});
test('actual static router bypasses Worker for public files, preserves cache/ETag and protects internal routes',async t=>{
 const cleanup=[],f=fixture({after:cb=>cleanup.push(cb)}),report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const config=JSON.parse(readFileSync(new URL('../wrangler.jsonc',import.meta.url),'utf8').replace(/^\s*\/\/.*$/gm,''));
 const mf=new Miniflare(convertV4MiniflareOptions({modules:true,compatibilityDate:config.compatibility_date,
  script:'export default {fetch(){return new Response("WORKER_INVOKED",{status:418,headers:{"X-Worker-Invoked":"yes"}})}}',
  assets:{directory:f.output,binding:'ASSETS',run_worker_first:config.assets.run_worker_first,routerConfig:{has_user_worker:true}}}));
 try{
 for(const path of ['/catalogue/current.json',report.index.path,f.artPath]){
  const r=await mf.dispatchFetch('https://static.invalid'+path);await r.arrayBuffer();assert.equal(r.status,200);assert.equal(r.headers.get('X-Worker-Invoked'),null,path);
  assert.match(r.headers.get('Cache-Control'),path.endsWith('current.json')?/max-age=60, must-revalidate/:/immutable/);
  const etag=r.headers.get('ETag');assert.ok(etag);
  const conditional=await mf.dispatchFetch('https://static.invalid'+path,{headers:{'If-None-Match':etag}});assert.equal(conditional.status,304);assert.equal(await conditional.text(),'');assert.equal(conditional.headers.get('X-Worker-Invoked'),null);
 }
 const index=JSON.parse(readFileSync(join(f.output,report.index.path))),path=Object.values(index.listBuckets)[0];
 const shard=await mf.dispatchFetch('https://static.invalid'+path);assert.equal(shard.status,200);assert.equal(shard.headers.get('X-Worker-Invoked'),null);await shard.arrayBuffer();
 for(const path of ['/v1/guest/session','/v1/catalogue/mal/search','/internal/content/manifest.json','/admin/status','/healthz']){
  const r=await mf.dispatchFetch('https://static.invalid'+path);assert.equal(r.status,418,path);assert.equal(r.headers.get('X-Worker-Invoked'),'yes');await r.text();
 }
 }catch(error){console.error(error);throw error;}finally{await mf.dispose();for(const cb of cleanup)cb();}
});

test('publication retains historical immutable artwork omitted from the current manifest',t=>{
 const f=fixture(t),previous=join(f.root,'previous'),old=Buffer.from('historical original PNG');
 const path='/assets/content/'+hash(old)+'.png';f.write(previous,path,old);
 const report=buildPublicCatalogue({source:f.source,content:f.content,output:f.output,previous});
 assert.deepEqual(readFileSync(join(f.output,path)),old);
 assert.equal(report.artworkFiles,2);
 assert.equal(report.retention.requiredFiles,1);
});

test('publication refuses an absent retention baseline instead of silently deleting old artwork',t=>{
 const f=fixture(t);
 assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,output:f.output}),/RETENTION_BASELINE_REQUIRED/);
});

test('corrupt historical artwork fails publication before the current pointer exists',t=>{
 const f=fixture(t),previous=join(f.root,'previous');
 f.write(previous,'/assets/content/'+hash('original')+'.png','corrupted');
 assert.throws(()=>buildPublicCatalogue({source:f.source,content:f.content,output:f.output,previous}),/checksum|HASH/);
 assert.throws(()=>readFileSync(join(f.output,'catalogue/current.json')));
});

test('independent retention gate rejects a historical blob removed after generation',t=>{
 const f=fixture(t),previous=join(f.root,'previous'),old=Buffer.from('old cached manifest image');
 const path='/assets/content/'+hash(old)+'.jpg';f.write(previous,path,old);
 buildPublicCatalogue({source:f.source,content:f.content,output:f.output,previous});
 rmSync(join(f.output,path));
 assert.throws(()=>retainArtwork({previous,output:f.output,verifyOnly:true}),/IMMUTABLE_ARTWORK_DROPPED/);
});

test('search shard packing enforces exact byte budgets across the whole range list',()=>{
 const rows=Array.from({length:257},(_,i)=>({id:i,pad:'x'.repeat(i%40)}));
 for(const budget of [64,256,SEARCH_SHARD_BYTES]){
  const ranges=packOffsets(rows,budget);
  assert.ok(ranges.length>0);
  let next=0;
  for(const [start,end]of ranges){
   assert.equal(start,next);next=end+1;
   assert.ok(Buffer.byteLength(JSON.stringify(rows.slice(start,end+1)))<=budget);
  }
  assert.equal(next,rows.length);
 }
 assert.deepEqual(packOffsets([]),[]);
 assert.deepEqual(packOffsets(['aa','bb'],11),[[0,1]]);
 assert.deepEqual(packOffsets(['aa','bb'],10),[[0,0],[1,1]]);
 assert.throws(()=>packOffsets(['x'.repeat(100)],10),/Single source row exceeds the search shard byte budget/);
});

test('lookup file packing keeps every key, stays sorted and respects the budget',()=>{
 const entries=[['a',{v:1}],['b',{v:'y'.repeat(10)}],['c',{v:2}],['d',{v:'z'.repeat(60)}]];
 for(const budget of [80,SEARCH_SHARD_BYTES]){
  const files=packLookupFiles(entries,budget),seen=[];
  for(const file of files){
   assert.ok(Buffer.byteLength(file.body)<=budget);
   const parsed=JSON.parse(file.body),keys=Object.keys(parsed);
   assert.equal(keys[0],file.first);assert.equal(keys[keys.length-1],file.last);
   for(const key of keys){assert.ok(key>=file.first&&key<=file.last);seen.push(key);}
   assert.deepEqual(parsed,Object.fromEntries(entries.filter(([key])=>keys.includes(key))));
  }
  assert.deepEqual(seen,entries.map(([key])=>key));
 }
 assert.deepEqual(packLookupFiles([]),[]);
 assert.throws(()=>packLookupFiles([['key',{pad:'x'.repeat(100)}]],10),/Single lookup entry exceeds the search shard byte budget/);
});

test('tankoban fine ranges survive conversion, stay deterministic and leave coarse buckets byte-identical',async t=>{
 const f=fixture(t,{withTankoban:true});
 const report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const index=JSON.parse(readFileSync(join(f.output,report.index.path)));
 assert.ok(!readFileSync(join(f.output,report.index.path),'utf8').includes('/internal/'));
 assert.equal(index.lookupRanges.version,1);assert.equal(index.lookupRanges.ranges.length,1);
 const ref=index.lookupRanges.ranges[0];
 assert.equal(ref.first,f.tankKey);assert.equal(ref.last,f.tankKey);
 assert.ok(ref.path.startsWith('/catalogue/'+report.revision+'/shared/'));
 assert.equal(hash(readFileSync(join(f.output,ref.path))),ref.path.slice(-69,-5));
 assert.deepEqual(JSON.parse(readFileSync(join(f.output,ref.path))),{[f.tankKey]:f.tankEnvelope});
 const coarsePath=Object.values(index.lookups)[0];
 assert.ok(coarsePath.startsWith('/catalogue/'+report.revision+'/shared/'));
 const coarse=JSON.parse(readFileSync(join(f.output,coarsePath)));
 assert.deepEqual(coarse[f.tankKey],f.tankEnvelope);
 const fetched=[];
 const spy={async fetch(req){fetched.push(new URL(req.url).pathname);try{return new Response(readFileSync(join(f.output,new URL(req.url).pathname)));}catch{return new Response('',{status:404});}}};
 assert.deepEqual((await prebuiltCatalogue(spy,sourceRevision,'tankoban','series',[13])).result,f.tankEnvelope);
 assert.ok(fetched.includes(ref.path),'fine range file served');
 assert.ok(!fetched.includes(coarsePath),'fine hit never reads the coarse bucket');
 const second=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:join(f.root,'second')});
 assert.equal(second.revision,report.revision);assert.equal(second.index.sha256,report.index.sha256);
 const g=fixture(t,{withTankoban:true});
 delete g.manifest.lookupRanges;g.writeManifest();
 const legacy=buildPublicCatalogue({source:g.source,content:g.content,previous:g.content,output:g.output});
 const legacyIndex=JSON.parse(readFileSync(join(g.output,legacy.index.path)));
 assert.ok(!Object.hasOwn(legacyIndex,'lookupRanges'));
 const legacyCoarsePath=Object.values(legacyIndex.lookups)[0];
 assert.equal(legacyCoarsePath.slice(-69,-5),coarsePath.slice(-69,-5),'coarse bucket bytes identical without the fine field');
 assert.deepEqual(JSON.parse(readFileSync(join(g.output,legacyCoarsePath))),coarse);
});

test('a malformed fine range declaration still converts and fails closed at resolution',async t=>{
 const f=fixture(t,{withTankoban:true});
 f.manifest.lookupRanges={version:2,ranges:[{first:f.tankKey,last:f.tankKey,path:f.tankRangePath}]};
 f.writeManifest();
 const report=buildPublicCatalogue({source:f.source,content:f.content,previous:f.content,output:f.output});
 const fetched=[];
 const spy={async fetch(req){fetched.push(new URL(req.url).pathname);try{return new Response(readFileSync(join(f.output,new URL(req.url).pathname)));}catch{return new Response('',{status:404});}}};
 await assert.rejects(prebuiltCatalogue(spy,sourceRevision,'tankoban','series',[13]),/PREBUILT_INVALID_LOOKUP_RANGES/);
 assert.deepEqual(fetched,[prefix+'manifest.json'],'malformed declaration fails before any payload fetch');
});
