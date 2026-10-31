// Read-only parity and local-Worker cost qualification. Optional --live performs
// exactly20 bounded guest catalogue reads, never an admin/write operation.
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {readFileSync,writeFileSync,existsSync} from 'node:fs';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {catalogueMethod} from '../src/catalogue-semantic.ts';
import {prebuiltCatalogue,selectionKey} from '../src/catalogue-prebuilt.ts';
import schema from '../src/source-schema.json' with {type:'json'};
import {publicationSql} from './publication_sql.mjs';
const [artifact,publication,evidence,liveFlag]=process.argv.slice(2);
if(!artifact||!publication||!evidence)throw Error('Usage: node projection/verify_static.mjs static-source.sqlite publication report.json [--live]');
const entry=join(publication,'catalogue/current.json');
const report=JSON.parse(readFileSync(existsSync(entry)?entry:join(publication,'static-catalogue-report.json'),'utf8')),revision=report.sourceRevision??report.revision;
const sqlite=new DatabaseSync(artifact,{readOnly:true});
const adapter={prepare(sql){sql=publicationSql(sql);return{bind(...args){return{async all(){return{success:true,results:sqlite.prepare(sql).all(...args)}}}}}}};
let assetReads=0,assetBytes=0;const assetFiles=[];
const assets={async fetch(request){assetReads++;const path=new URL(request.url).pathname;try{const raw=readFileSync(join(publication,path));assetBytes+=raw.byteLength;assetFiles.push({path,bytes:raw.byteLength});return new Response(raw,{headers:{'Content-Type':'application/json'}});}catch(e){if(e.code==='ENOENT')return new Response('',{status:404});throw e;}}};
const cases=[
 ['mal','discoverPageFiltered',['popular',[],false,0,24]],['mal','discoverPageFiltered',['popular',[],false,24,24]],
 ['mal','discoverPageFiltered',['top-rated',[{axis:'genre',key:'romance'}],false,0,24]],['mal','discoverFilters',['genre',false]],
 ['mal','mangaById',[13]],['mal','search',['one piece',20]],['imdb','titleCatalog',[{},0,25]],
 ['imdb','titleCatalog',[{genre:'Drama',ratingMin:6},0,25]],['imdb','titleFacts',['tt0103359']],['imdb','titleFacts',[['tt0103359']]],
 ['comics','discoverPageFiltered',['popular',[],false,0,24]],['comics','discoverFilters',['genre',false]],['comics','search',['batman',50]],
 ['mal','discoverPageFiltered',['popular',[],false,1020,100]],['mal','discoverPageFiltered',['top-rated',[],false,2500,100]],
 ['comics','discoverPageFiltered',['all',[],false,1000,24]],['imdb','titleCatalog',[{},1020,100]],
 ['mal','search',['one',100,'manga']],['mal','search',['ワン',20]],
 ['mal','matchByTitle',['one piece',1997,'manga']],['imdb','search',['bat',20]],['imdb','matchByTitle',['batman',1989]],
 ['comics','exactMatches',['Batman']],['comics','search',['Spider-Man',50]],['comics','search',['%',50]],
 ['mal','genreEntries',['anime','Action','score',24]],['mal','genreEntries',['manga','Action','members',24]],
 ['mal','genreCount',['manga','Action']],['mal','genreCounts',['manga']],['mal','cachedDiscoverFilters',['demographic',false]],
 ['comics','curatedReady',[]],['comics','curatedRanked',[]],['comics','curatedGenreShelves',[3]],['comics','shelf',['stocked','',24]],
 ['mal','animeCatalog',[{},0,24]],['mal','animeCatalog',[{order:'members'},72,24]],['imdb','titleCatalog',[{order:'votes'},72,24]],
];
for(const format of ['manga','manhwa','manhua'])for(const explicit of [false,true])
 for(const catalog of ['popular','trending','top-rated','new-releases'])for(const offset of [0,24])
  cases.push(['mal','discoverPageFiltered',[catalog,[{axis:'format',key:format}],explicit,offset,24]]);
for(const r of sqlite.prepare('SELECT locg_id,norm_title FROM lib_comics_curated_series LIMIT 3').all())for(const method of ['curatedSeries','curatedCoverage','curatedHasDownloadable','curatedByNorm'])cases.push(['comics',method,[method==='curatedByNorm'?r.norm_title:r.locg_id]]);
for(const {gcd_id}of sqlite.prepare('SELECT gcd_id FROM den_comics_series LIMIT 3').all())for(const method of ['series','downloadsFor'])cases.push(['comics',method,[gcd_id]]);
const rules={};vm.createContext(rules);vm.runInContext(readFileSync(new URL('../../../qml/TheatreCatalogRules.js',import.meta.url),'utf8').replace(/^\s*\.pragma.*$/gm,''),rules);
for(const row of rules.MOVIE_DAILY_POOL)for(const q of rules.indexQueriesFor(row.recipe))for(const offset of [0,72])cases.push(['imdb','titleCatalog',[q,offset,24]]);
const result={revision,parity:[],http:[],live:[],deployed:false,d1Writes:0};
for(const [dataset,method,args]of cases){
 const actual=await prebuiltCatalogue(assets,revision,dataset,method,args);assert.notEqual(actual,null,`${dataset}/${method} not prepared ${JSON.stringify(args)}`);
 const expected=await catalogueMethod(adapter,dataset,method,args);assert.deepEqual(actual.result,expected,`${dataset}/${method} ${JSON.stringify(args)}`);
 result.parity.push({dataset,method,args,pass:true});
}
const mf=new Miniflare(convertV4MiniflareOptions({modules:true,scriptPath:fileURLToPath(new URL('../dist/index.js',import.meta.url)),compatibilityDate:'2026-10-06',bindings:{SESSION_SECRET:'local-static-qualification-secret-at-least32',ENVIRONMENT:'staging',TANKOBAN_GUEST_ENABLED:'true',LIBRARY_REVISION:revision},ratelimits:{GUEST_RATE_LIMITER:{simple:{limit:1000,period:60},namespace_id:'1001'}},d1Databases:{CATALOG:'static-qualification-catalog',LIBRARY:'static-qualification-library',METADATA:'static-qualification-metadata'},serviceBindings:{ASSETS:r=>assets.fetch(r)}}));
try{
 const catalog=await mf.getD1Database('CATALOG'),library=await mf.getD1Database('LIBRARY');
 await catalog.exec('CREATE TABLE _source_tables(name TEXT PRIMARY KEY,ready INTEGER,source_hash TEXT,expected_rows INTEGER,imported_rows INTEGER)');
 for(const name of ['tankoban__series','tankoban__volumes'])await catalog.prepare('INSERT INTO _source_tables VALUES(?,?,?,?,?)').bind(name,1,schema[name].sourceHash,schema[name].rowCount,schema[name].rowCount).run();
 await library.exec('CREATE TABLE _library_state(id INTEGER PRIMARY KEY,revision TEXT,ready INTEGER)');await library.prepare('INSERT INTO _library_state VALUES(1,?,1)').bind(revision).run();
 // No catalogue tables exist: any attempt to run old SQL will fail this proof.
 const guest=await mf.dispatchFetch('https://static.invalid/v1/guest/session',{method:'POST',headers:{'Content-Type':'application/json','CF-Connecting-IP':'192.0.2.10'},body:JSON.stringify({appId:'colosseum',apiVersion:1})});assert.equal(guest.status,200);const token=(await guest.json()).token;
 let firstTag;
 for(let i=0;i<cases.length;i++){
  const start=performance.now(),beforeReads=assetReads,beforeBytes=assetBytes,beforeFiles=assetFiles.length;
  const [dataset,method,args]=cases[i],response=await mf.dispatchFetch(`https://static.invalid/v1/catalogue/${dataset}/${method}`,{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify({args})});
  const localWallMs=performance.now()-start;
  assert.equal(response.status,200,await response.clone().text());assert.equal(response.headers.get('X-Catalogue-Delivery'),'prebuilt');
  const rows=Number(response.headers.get('X-Catalogue-D1-Rows'));assert.equal(rows,i===0?1:0);
  assert.deepEqual((await response.json()).result,await catalogueMethod(adapter,dataset,method,args));
  if(i===0)firstTag=response.headers.get('ETag');
  result.http.push({dataset,method,args,rowsRead:rows,delivery:'prebuilt',localWallMs,assetReads:assetReads-beforeReads,assetBytes:assetBytes-beforeBytes,shards:assetFiles.slice(beforeFiles).filter(f=>f.path.includes('/shared/'))});
 }
 // Every per-fetch payload file stays inside the 56 KiB search shard budget.
 const shardCap=56*1024,mangaHttp=result.http.find(r=>r.dataset==='mal'&&r.method==='mangaById');
 assert.ok(mangaHttp,'mangaById measured');assert.equal(mangaHttp.shards.length,2,'mangaById reads the key chunk and the record chunk');
 for(const f of mangaHttp.shards)assert.ok(f.bytes<=shardCap,`mangaById shard within budget ${f.path}`);
 const conditional=await mf.dispatchFetch('https://static.invalid/v1/catalogue/mal/discoverPageFiltered',{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json','If-None-Match':firstTag},body:JSON.stringify({args:cases[0][2]})});assert.equal(conditional.status,304);assert.equal(await conditional.text(),'');assert.equal(conditional.headers.get('X-Catalogue-D1-Rows'),'0');result.conditional304=true;
 const publicIndex=report.index?JSON.parse(readFileSync(join(publication,report.index.path))):null;
 const tankKey=selectionKey('tankoban','series',[13]);
 const fineRef=(publicIndex?.lookupRanges?.ranges??[]).find(r=>tankKey>=r.first&&tankKey<=r.last);
 const coarseRef=publicIndex?.lookups?.[createHash('sha256').update(tankKey).digest('hex').slice(0,2)];
 const tankFiles=assetFiles.length;
 const tank=await mf.dispatchFetch('https://static.invalid/v1/tankoban/series/13',{headers:{Authorization:'Bearer '+token,'CF-Connecting-IP':'192.0.2.10'}});assert.equal(tank.status,200);assert.equal(tank.headers.get('X-Catalogue-D1-Rows'),'0');const tv=await tank.json();assert.equal(tv.volumes.length,113);
 const tankShards=assetFiles.slice(tankFiles).filter(f=>f.path.includes('/shared/'));
 if(fineRef&&coarseRef&&fineRef.path!==coarseRef){assert.ok(tankShards.some(f=>f.path===fineRef.path),'tankoban series reads the fine range file');assert.ok(!tankShards.some(f=>f.path===coarseRef),'fine hit never reads the coarse bucket');}
 for(const f of tankShards)assert.ok(f.bytes<=shardCap,`tankoban shard within budget ${f.path}`);
 assert.equal(tankShards.length,1,'tankoban series reads exactly one payload file');
 result.http.push({dataset:'tankoban',method:'series',id:13,rowsRead:0,volumes:tv.volumes.length,shards:tankShards});
}finally{await mf.dispose();}
writeFileSync(evidence,JSON.stringify(result,null,2));
if(liveFlag==='--live'){
 const origin='https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev';
 const r=await fetch(origin+'/v1/guest/session',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({appId:'colosseum',apiVersion:1})});assert.equal(r.status,200);const {token}=await r.json();
 const samples=[...[13,1,2,1706,44347].map(id=>['mal','mangaById',[id]]),...['tt0103359','tt0111161','tt0068646','tt0468569','tt0137523'].map(id=>['imdb','titlePresentation',[id]]),...['one piece','naruto','bleach','death note','berserk'].map(t=>['mal','search',[t,20]]),['comics','curatedReady',[]],['mal','genreCounts',['anime']],['mal','genreCounts',['manga']],['mal','discoverFilters',['genre',false]],['mal','discoverFilters',['demographic',false]]];
 for(const [dataset,method,args]of samples){const response=await fetch(`${origin}/v1/catalogue/${dataset}/${method}`,{method:'POST',signal:AbortSignal.timeout(15000),headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify({args})});assert.equal(response.status,200);const body=await response.json();assert.equal(body.revision,revision);assert.deepEqual(body.result,(await prebuiltCatalogue(assets,revision,dataset,method,args)).result);result.live.push({dataset,method,args,pass:true});writeFileSync(evidence,JSON.stringify(result,null,2));}
}
sqlite.close();writeFileSync(evidence,JSON.stringify(result,null,2));console.log(JSON.stringify({parity:result.parity.length,http:result.http.length,live:result.live.length,d1Writes:0}));
