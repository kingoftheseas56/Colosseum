import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,mkdirSync,writeFileSync,readFileSync,readdirSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {createHash} from 'node:crypto';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {buildPublicCatalogue,retainArtwork} from '../projection/public_static.mjs';
import {prebuiltCatalogue,selectionKey} from '../src/catalogue-prebuilt.ts';
const hash=b=>createHash('sha256').update(b).digest('hex');
const sourceRevision='1'.repeat(64),prefix='/internal/prebuilt/'+sourceRevision+'/';
function fixture(t,{withImdb=false,imdbRows}={}){
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
 const manifest={version:1,revision:sourceRevision,lists:{},fixed:{},listBuckets:{[hash(selectionKey('imdb','titleCatalog',[{}])).slice(0,2)]:list},lookups:{},search:{manga:{names:[{path:names,first:'one piece',last:'one piece'}],keys:[{path:keys,first:13,last:13}]},...(withImdb?{imdb:{names:[],keys:[{path:imdbKeys,first:'tt0000001',last:'tt0000003'}]}}:{})}};
 const raw=JSON.stringify(manifest);write(source,prefix+'manifest.json',raw);entries.push({path:prefix+'manifest.json',bytes:Buffer.byteLength(raw)});
 write(source,'static-catalogue-report.json',JSON.stringify({revision:sourceRevision,date:'2026-10-15',year:'2026',files:entries.length,listSelections:1,source:{mal:{sha256:'2'.repeat(64)}},entries}));
 const document='\n'+JSON.stringify({schemaVersion:1,revision:'doc-1',data:{title:'Universe',image:'https://provider.example/cover.jpg'}},null,2)+'\n';
 const artwork=Buffer.from('artwork-fixture'),artPath='/assets/content/'+hash(artwork)+'.webp';write(content,artPath,artwork);
 const cm={schemaVersion:1,revision:'3'.repeat(64),documents:{'universe-test':{revision:'doc-1',sha256:hash(document)}},assets:{'assets/test.png':{path:artPath,sha256:hash(artwork),bytes:artwork.length}}};
 write(content,'internal/content/manifest.json',JSON.stringify(cm));write(content,'internal/content/documents/universe-test.json',document);
 write(content,'internal/content/rpc-bundle.json',JSON.stringify({apiVersion:1,revision:cm.revision,method:'bundle',result:{manifest:cm,documents:{'universe-test':document}}}));
 return{root,source,content,output,manifest,entries,records,artPath,write,imdbKeys,imdbSourceRows};
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
