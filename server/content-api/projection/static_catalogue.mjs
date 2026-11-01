// Static catalogue publication only: no remote writes or deployment.
import {DatabaseSync} from 'node:sqlite';
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import {catalogueMethod,imdbRow,malRow} from '../src/catalogue-semantic.ts';
import {comicsPreparedList,legacyRow} from '../src/catalogue-comics.ts';
import {selectionKey} from '../src/catalogue-prebuilt.ts';
import schema from '../src/source-schema.json' with {type:'json'};
import {publicationSql} from './publication_sql.mjs';
import {publishLandingCatalogues} from './landing_catalogue.mjs';
import {packOffsets,packLookupFiles} from './shard_packing.mjs';
const [artifact,output,qmlRoot]=process.argv.slice(2);
if(!artifact||!output||!qmlRoot)throw Error('Usage: node projection/static_catalogue.mjs static-source.sqlite fresh-output qml-directory');
const sourceReport=JSON.parse(readFileSync(join(dirname(artifact),'static-source-report.json'),'utf8'));
const sha=b=>createHash('sha256').update(b).digest('hex');
if(sourceReport.format!=='static-source-v1'||sha(readFileSync(artifact))!==sourceReport.sqliteSha256)throw Error('Unverified static source');
if(sourceReport.sources.tankoban?.sha256!==schema.tankoban__series.sourceHash)throw Error('Tankoban source/schema identity mismatch');
if(existsSync(output))throw Error('Refusing to overwrite publication');mkdirSync(output,{recursive:true});
const sqlite=new DatabaseSync(artifact,{readOnly:true});
// Only publication list queries remove pagination, after the canonical SQL owner
// has validated/sanitized the arguments. No change to runtime D1 query semantics.
function adapter(unpaged=false){return{prepare(sql){return{bind(...args){return{async all(){
 if(unpaged&&/ LIMIT \? OFFSET \?$/.test(sql)){sql=sql.replace(/ LIMIT \? OFFSET \?$/,'');args=args.slice(0,-2);}
 else if(unpaged&&/ LIMIT \?$/.test(sql)){sql=sql.replace(/ LIMIT \?$/,'');args=args.slice(0,-1);}
 // Packed genre counts and MAL anime score ties follow source-scan order.
 // Local helper indexes must not silently change these ranking ties.
 sql=publicationSql(sql);
 return{success:true,results:sqlite.prepare(sql).all(...args)};
}}}}}};}
const db=adapter(),all=adapter(true),revision=sourceReport.revision,prefix='/internal/prebuilt/'+revision+'/';
const manifest={version:1,revision,lists:{},fixed:{},search:{},lookups:{}};
const lookupBuckets=new Map();
function lookup(dataset,method,args,result){const key=selectionKey(dataset,method,args),bucket=sha(key).slice(0,2);if(!lookupBuckets.has(bucket))lookupBuckets.set(bucket,{});lookupBuckets.get(bucket)[key]=result;return key;}
const files=new Map(),today=new Date().toISOString().slice(0,10),year=String(new Date().getFullYear());
const landingFixed=new Map();
if(sqlite.prepare("SELECT COUNT(*) AS n FROM lib_mal_manga WHERE date(start_date)>date(?)").get(today).n)throw Error('Future-dated MAL titles require an admission-aware publication; do not silently fall back daily');
function putRaw(raw){const bytes=Buffer.byteLength(raw);if(bytes>4*1024*1024)throw Error('Shared file exceeds4MiB');const path=prefix+'shared/'+sha(raw)+'.json';if(!files.has(path)){files.set(path,bytes);const target=join(output,path);mkdirSync(dirname(target),{recursive:true});writeFileSync(target,raw);}if(files.size+200>18000)throw Error('Free file budget/headroom exceeded');return path;}
function put(value){return putRaw(JSON.stringify(value));}
function fixed(dataset,method,args,result){const key=selectionKey(dataset,method,args);manifest.fixed[key]=put(result);landingFixed.set(key,result);}
function list(dataset,method,args,items,shape='array',period='stable'){
 if(method==='discoverPageFiltered')args=[args[0],args[1].map(f=>({...f,key:f.key.replace(/[A-Z]/g,c=>c.toLowerCase())})),args[2]];
 let chunkSize=256;
 while(items.some((_,i)=>i%chunkSize===0&&Buffer.byteLength(JSON.stringify(items.slice(i,i+chunkSize)))>4*1024*1024)){chunkSize=Math.floor(chunkSize/2);if(!chunkSize)throw Error('Single item exceeds cap');}
 const parts=[];for(let i=0;i<items.length;i+=chunkSize)parts.push(put(items.slice(i,i+chunkSize)));
 const stableRelease=dataset==='mal'&&method==='discoverPageFiltered'&&args[0]==='new-releases';
 manifest.lists[selectionKey(dataset,method,args)]={parts,length:items.length,chunkSize,shape,period:stableRelease?'stable':period,...(stableRelease?{notBefore:today}:{})};
}
const requests=new Map();
const add=(dataset,method,args)=>requests.set(selectionKey(dataset,method,args),{dataset,method,args});
for(const explicit of [false,true])for(const dataset of ['mal','comics']){
 const axes=dataset==='mal'?['genre','demographic']:['genre','publisher','format','availability'],filters=[[]];
 for(const axis of axes){
  const values=await catalogueMethod(db,dataset,'discoverFilters',[axis,explicit]);fixed(dataset,'discoverFilters',[axis,explicit],values);
  if(dataset==='mal')fixed(dataset,'cachedDiscoverFilters',[axis,explicit],values);
  for(const r of values)filters.push([{axis,key:r.key??r.value}]);
 }
 if(dataset==='mal')for(const key of ['manga','manhwa','manhua'])filters.push([{axis:'format',key}]);
 for(const catalog of dataset==='mal'?['popular','trending','top-rated','new-releases','completed','publishing','short-completed','one-shots','started-1990s','started-2000s','started-2010s']:['popular','new-releases','most-stocked','all','recently-available','complete-runs','near-complete','community-collections'])for(const filter of filters){
  const args=[catalog,filter,explicit],items=dataset==='comics'?await comicsPreparedList(db,[...args,0,100]):(await catalogueMethod(all,dataset,'discoverPageFiltered',[...args,0,100])).items;
  list(dataset,'discoverPageFiltered',args,items,dataset,dataset==='comics'?year:catalog==='new-releases'?today:'stable');
 }
 console.error('Prepared '+dataset+' explicit='+explicit+'; files='+files.size);
}
const clean=s=>s.replace(/^\s*\.(?:pragma|import).*$/gm,'');
const rules={};vm.createContext(rules);vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreCatalogRules.js'),'utf8')),rules);
const api={};vm.createContext(api);vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreApi.js'),'utf8')),api);
const recipes=[...rules.MOVIE_ROWS(),...rules.SHOW_ROWS(),...rules.ANIME_ROWS(),...rules.MOVIE_DAILY_POOL];
for(const row of recipes){
 for(const q of rules.indexQueriesFor(row.recipe))add('imdb','titleCatalog',[q]);
 if(row.recipe.kind==='animeTagAny'){for(const tag of row.recipe.tags??[])add('mal','animeCatalog',[{order:'members',tag}]);}
 else{const q=api.animeQueryFor(row.recipe);if(q)add('mal','animeCatalog',[q]);}
}
for(const order of ['votes','rating','year','episodes'])for(const type of ['', 'movie','series','mini']){
 const base={...(type?{type}:{}),...(order==='votes'?{}:{order})};add('imdb','titleCatalog',[base]);
 for(const {genre}of sqlite.prepare('SELECT DISTINCT genre FROM lib_imdb_genre ORDER BY genre').all())add('imdb','titleCatalog',[{...base,genre}]);
}
add('imdb','titleCatalog',[{genre:'Drama',ratingMin:6}]);
for(const r of requests.values()){const result=await catalogueMethod(all,r.dataset,r.method,[...r.args,0,100]);list(r.dataset,r.method,r.args,result);}
for(const {medium,tag}of sqlite.prepare('SELECT DISTINCT medium,tag FROM lib_mal_tag ORDER BY medium,tag').all())for(const order of ['members','score'])list('mal','genreEntries',[medium,tag,order],await catalogueMethod(all,'mal','genreEntries',[medium,tag,order,24]));
for(const medium of ['anime','manga'])fixed('mal','genreCounts',[medium],await catalogueMethod(db,'mal','genreCounts',[medium]));
for(const method of ['curatedReady','curatedRanked'])fixed('comics',method,[],await catalogueMethod(db,'comics',method,[]));
for(const n of [3,8])fixed('comics','curatedGenreShelves',[n],await catalogueMethod(db,'comics','curatedGenreShelves',[n]));
for(const[kind,arg]of [['stocked',''],['publisher','Marvel'],['publisher','DC'],['publisher','Image'],['decade','2020'],['decade','2010'],['deep',''],['fanmade','']])fixed('comics','shelf',[kind,arg,24],await catalogueMethod(db,'comics','shelf',[kind,arg,24]));
for(const {medium,tag,total}of sqlite.prepare('SELECT medium,tag,total FROM lib_mal_tag_count').all())lookup('mal','genreCount',[medium,tag],Number(total)||0);
for(const {gcd_id:id}of sqlite.prepare('SELECT gcd_id FROM den_comics_series').all()){
 lookup('comics','series',[id],await catalogueMethod(db,'comics','series',[id]));
 lookup('comics','downloadsFor',[id],await catalogueMethod(db,'comics','downloadsFor',[id]));
}
for(const {locg_id:id,norm_title:norm}of sqlite.prepare('SELECT locg_id,norm_title FROM den_comics_curated_series').all()){
 for(const method of ['curatedSeries','curatedCoverage','curatedHasDownloadable'])lookup('comics',method,[id],await catalogueMethod(db,'comics',method,[id]));
 lookup('comics','curatedByNorm',[norm],await catalogueMethod(db,'comics','curatedByNorm',[norm]));
}
fixed('comics','curatedGenreShelves',[-1],await catalogueMethod(db,'comics','curatedGenreShelves',[-1]));
// Reuse already computed fixed results; bounded paginated calls retain the SQL owner.
await publishLandingCatalogues({date:today,qmlRoot,fixed,queryFn:async(dataset,method,args)=>{
 const key=selectionKey(dataset,method,args);
 return landingFixed.has(key)?landingFixed.get(key):catalogueMethod(db,dataset,method,args);
}});
// Tankoban shares these files; do not put ten thousand individual files in ASSETS.
manifest.tankobanSourceHash=schema.tankoban__series.sourceHash;
const volumeMap=new Map();
for(const r of sqlite.prepare('SELECT * FROM lib_tankoban_volumes ORDER BY number').all()){if(!volumeMap.has(r.mal_id))volumeMap.set(r.mal_id,new Map());volumeMap.get(r.mal_id).set(r.number,{number:r.number,cover:r.cover_url??'',name:r.name??''});}
const natural=s=>s.toLowerCase().replace(/\p{Nd}+/gu,run=>run.padStart(12,'0'));
// The fine lookup directory is additive; the coarse buckets above stay
// byte-identical for old clients and remain the fallback on any miss.
const tankobanLookups=new Map();
for(const r of sqlite.prepare('SELECT * FROM lib_tankoban_series').all()){
 const overlay=new Map(volumeMap.get(r.mal_id)??[]),volumes=[];
 for(let n=1;n<=r.volume_count;n++){const number=String(n);volumes.push(overlay.get(number)??{number,cover:'',name:''});overlay.delete(number);}
 volumes.push(...overlay.values());volumes.sort((a,b)=>natural(a.number)<natural(b.number)?-1:natural(a.number)>natural(b.number)?1:0);
 const envelope={apiVersion:1,sourceHash:manifest.tankobanSourceHash,ready:true,seriesInfo:{volumeCount:r.volume_count,countBasis:r.count_basis},volumes};
 tankobanLookups.set(lookup('tankoban','series',[r.mal_id],envelope),envelope);
}
for(const[bucket,values]of lookupBuckets)manifest.lookups[bucket]=put(values);
if(tankobanLookups.size)manifest.lookupRanges={version:1,ranges:packLookupFiles([...tankobanLookups].sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0)).map(({first,last,body})=>({first,last,path:putRaw(body)}))};

const fold=s=>s.replace(/[A-Z]/g,c=>c.toLowerCase());const lex=(a,b)=>a<b?-1:a>b?1:0;
for(const [family,table]of [['imdb','lib_imdb_title'],['anime','lib_mal_anime'],['manga','lib_mal_manga'],['comics','den_comics_series']]){
 const source=sqlite.prepare('SELECT * FROM '+table+' ORDER BY rowid').all().map((r,i)=>({...r,__staticOrder:i})).sort((a,b)=>lex(fold(family==='comics'?a.title??'':a.norm_title??''),fold(family==='comics'?b.title??'':b.norm_title??''))||a.__staticOrder-b.__staticOrder),names=[],keys=[];
 const recordRows=raw=>raw.map(r=>family==='imdb'?{identity:imdbRow(r,true)}:family==='comics'?{identity:legacyRow(r)}:{identity:malRow(r,family,true),...(family==='manga'?{normal:malRow(r,family)}:{})});
 // Manga record blocks are byte-packed to the search shard budget; the other
 // families keep the published 128-record grouping.
 const chunks=[];
 if(family==='manga'){const rows=recordRows(source);for(const [start,end]of packOffsets(rows))chunks.push([start,end,rows.slice(start,end+1)]);}
 else for(let start=0;start<source.length;start+=128)chunks.push([start,Math.min(start+128,source.length)-1,recordRows(source.slice(start,start+128))]);
 for(const [start,end,records]of chunks){
  const path=put(records);
  source.slice(start,end+1).forEach((r,i)=>{
   const id=family==='imdb'?r.tt:family==='comics'?r.gcd_id:r.mal_id;
   keys.push([id,path,i,...(family==='imdb'?[{identity:{title:records[i].identity.title,type:records[i].identity.type,rating:records[i].identity.rating,votes:records[i].identity.votes,isAnime:records[i].identity.isAnime},ordinal:r.__staticOrder}]:[])]);
   for(const name of family==='comics'?[r.title??'']:[r.norm_title,r.norm_title_english].filter(Boolean))names.push([name,id,path,i,r.norm_title??'',r.year,family==='imdb'?r.rating:family==='comics'?r.__downloads:r.score,family==='imdb'?r.votes:r.__staticOrder]);
  });
 }
 names.sort((a,b)=>lex(fold(a[0]),fold(b[0]))||a[7]-b[7]);keys.sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
 const directory={names:[],keys:[]};
 for(let n=0;n<names.length;n+=2048){const chunk=names.slice(n,n+2048);directory.names.push({path:put(chunk),first:chunk[0][0],last:chunk.at(-1)[0]});}
 // Manga key chunks are byte-packed for the same reason; other families keep
 // the published 2,048-row grouping so old clients stay byte-identical.
 if(family==='manga'){for(const [start,end]of packOffsets(keys)){const chunk=keys.slice(start,end+1);directory.keys.push({path:put(chunk),first:chunk[0][0],last:chunk.at(-1)[0]});}}
 else for(let n=0;n<keys.length;n+=2048){const chunk=keys.slice(n,n+2048);directory.keys.push({path:put(chunk),first:chunk[0][0],last:chunk.at(-1)[0]});}
 manifest.search[family]=directory;
}
const listSelections=Object.keys(manifest.lists).length,listBuckets=new Map();
for(const[key,value]of Object.entries(manifest.lists)){const bucket=sha(key).slice(0,2);if(!listBuckets.has(bucket))listBuckets.set(bucket,{});listBuckets.get(bucket)[key]=value;}
manifest.listBuckets={};for(const[bucket,values]of listBuckets)manifest.listBuckets[bucket]=put(values);manifest.lists={};
const rawManifest=JSON.stringify(manifest);if(Buffer.byteLength(rawManifest)>4*1024*1024)throw Error('Manifest cap');
mkdirSync(join(output,prefix),{recursive:true});writeFileSync(join(output,prefix,'manifest.json'),rawManifest);
files.set(prefix+'manifest.json',Buffer.byteLength(rawManifest));sqlite.close();
const report={revision,date:today,year,files:files.size,reservedFiles:200,freeFileLimit:20000,largestFileBytes:Math.max(...files.values()),totalBytes:[...files.values()].reduce((a,b)=>a+b,0),listSelections,deepPages:'all offsets; shared bounded chunks',search:'complete title index and shared records',d1Writes:0,source:sourceReport.sources,entries:[...files].map(([path,bytes])=>({path,bytes}))};
writeFileSync(join(output,'static-catalogue-report.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify({...report,source:undefined,entries:undefined}));
