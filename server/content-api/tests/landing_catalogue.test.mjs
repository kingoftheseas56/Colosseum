import test from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {DatabaseSync} from 'node:sqlite';
import {catalogueMethod} from '../src/catalogue-semantic.ts';
import {selectionKey} from '../src/catalogue-prebuilt.ts';
import {landingRequests,publishLandingCatalogues,theatreLandingOwners} from '../projection/landing_catalogue.mjs';

const qmlRoot=fileURLToPath(new URL('../../../qml/',import.meta.url));
const owners=theatreLandingOwners(qmlRoot);
const date='2026-12-31';
const plain=value=>JSON.parse(JSON.stringify(value));

test('Comics landing keeps all eight legacy shelves and seven exact curated pages',()=>{
 const requests=landingRequests('comics','comics',{...owners,date});
 assert.equal(requests.length,18);
 assert.deepEqual(requests.filter(r=>r.method==='shelf').map(r=>r.args),[
  ['stocked','',24],['publisher','Marvel',24],['publisher','DC',24],['publisher','Image',24],
  ['decade','2020',24],['decade','2010',24],['deep','',24],['fanmade','',24]]);
 assert.deepEqual(requests.filter(r=>r.method==='discoverPageFiltered').map(r=>r.args),[
  ['recently-available',[],false,0,24],['complete-runs',[],false,0,24],['near-complete',[],false,0,24],
  ['popular',[{axis:'format',key:'omnibus'}],false,0,24],['popular',[{axis:'format',key:'deluxe'}],false,0,24],
  ['popular',[{axis:'format',key:'graphic novel'}],false,0,24],['community-collections',[],false,0,24]]);
 assert.deepEqual(requests.filter(r=>r.method.startsWith('curated')), [{method:'curatedReady',args:[]},{method:'curatedRanked',args:[]},{method:'curatedGenreShelves',args:[8]}]);
 assert.throws(()=>landingRequests('mal','manga',{...owners,date}),/Unsupported landing/);
});

test('Theatre primes exact stable owner recipes, preview cap, and deduplication across UTC days',()=>{
 for(const tab of ['movies','shows','anime']){
  const dataset=tab==='anime'?'mal':'imdb';
  const defs=owners.rules.defaultRows(tab);
  const expected=new Map();
  for(const def of defs){
   const queries=tab==='anime'?owners.api.animeQueriesFor(def.recipe):owners.rules.indexQueriesFor(def.recipe);
   for(const q of queries){const method=tab==='anime'?'animeCatalog':'titleCatalog',args=[q,0,20];expected.set(selectionKey(dataset,method,args),{method,args});}
  }
  assert.deepEqual(plain(landingRequests(dataset,tab,{...owners,date})),plain([...expected.values()]));
 }
 const movieDays=['2026-10-16','2026-10-17'].map(date=>plain(landingRequests('imdb','movies',{...owners,date})));
 assert.deepEqual(movieDays[0],movieDays[1]);
 const primed=new Set(movieDays[0].map(r=>selectionKey('imdb',r.method,r.args)));
 const dailyDays=['2026-10-16','2026-10-17'].map(day=>{
  const rows=owners.rules.dailyRows(Date.parse(day+'T00:00:00Z'),6);
  const misses=rows.flatMap(def=>owners.rules.indexQueriesFor(def.recipe))
   .map(q=>selectionKey('imdb','titleCatalog',[q,0,owners.api.PREVIEW_ROW_CAP]))
   .filter(key=>!primed.has(key));
  assert.ok(misses.length>0,'daily-only queries must miss the stable landing cache');
  return plain({rows:rows.map(row=>row.key),misses});
 });
 assert.notDeepEqual(dailyDays[0].rows,dailyDays[1].rows);
 assert.notDeepEqual(dailyDays[0].misses,dailyDays[1].misses);
});

test('Publisher stores single-dataset exact results with exclusive UTC expiry and fixed keys',async()=>{
 const calls=[],written=[];
 const queryFn=async(dataset,method,args)=>{const result={dataset,method,args,sentinel:null,flag:false};calls.push({dataset,method,args,result});return result;};
 await publishLandingCatalogues({date,qmlRoot,owners,queryFn,fixed:(dataset,method,args,result)=>written.push({dataset,method,args,result})});
 assert.equal(written.length,4);
 for(const {dataset,method,args,result:bundle} of written){
  assert.equal(method,'landing');assert.deepEqual(args,[bundle.tab]);assert.equal(bundle.dataset,dataset);
  assert.equal(bundle.version,1);assert.equal(bundle.validFrom,date);assert.equal(bundle.validUntil,dataset==='comics'?'2027-01-01':'9999-12-31');
  for(const q of bundle.queries){const call=calls.find(c=>c.dataset===dataset&&selectionKey(dataset,c.method,c.args)===selectionKey(dataset,q.method,q.args));assert.strictEqual(q.result,call.result);}
 }
 const spring=[];
 await publishLandingCatalogues({date:'2026-10-16',qmlRoot,owners,queryFn,fixed:(_d,_m,_a,b)=>spring.push(b)});
 assert.equal(spring[0].validUntil,'2027-01-01');
 for(const b of spring.slice(1)){
  assert.equal(b.validUntil,'9999-12-31');
  // Mirror validLanding's exclusive ISO UTC date bounds, including year rollover.
  for(const today of ['2026-10-16','2026-10-17','2027-01-01','2028-10-17'])
   assert.ok(b.validFrom<=today && today<b.validUntil);
  assert.ok('2026-10-15'<b.validFrom);
 }
});

test('Bundle results equal individual semantic queries against a populated SQLite fixture',async()=>{
 const sqlite=new DatabaseSync(':memory:');
 sqlite.exec(`CREATE TABLE den_comics_series(gcd_id INTEGER,title TEXT,year INTEGER,publisher TEXT,cover TEXT,__downloads INTEGER,__fanMade INTEGER);
 INSERT INTO den_comics_series VALUES(1,'Legacy',2026,'Marvel','legacy-cover',20,1);
 CREATE TABLE den_comics_curated_series(locg_id TEXT,rank INTEGER,title TEXT,norm_title TEXT,year INTEGER,publisher TEXT,cover TEXT,synopsis TEXT,__genres TEXT,__formats TEXT,__editionCount INTEGER,__availableCount INTEGER,__publishedMaxYear INTEGER,__coverage TEXT);
 CREATE VIEW lib_comics_curated_series AS SELECT * FROM den_comics_curated_series;
 CREATE TABLE lib_comics_curated_series_coverage(locg_id TEXT);
 CREATE TABLE lib_comics_curated_genre(genre TEXT);
 INSERT INTO lib_comics_curated_genre VALUES('Action');
 INSERT INTO den_comics_curated_series VALUES('a',1,'Curated','curated',2026,'DC','curated-cover',NULL,'["Action"]','["omnibus","deluxe","graphic novel"]',1,1,2026,'{"official_source_count":1,"latest_source_date":"2026-10-01","availability_state":"complete","community_source_count":1}');
 CREATE TABLE den_imdb_title(tt TEXT,type TEXT,title TEXT,origLang TEXT,year INTEGER,endYear INTEGER,runtimeMin INTEGER,votes INTEGER,episodes INTEGER,rating REAL,genres TEXT,isAnime INTEGER,__membership TEXT);
 INSERT INTO den_imdb_title VALUES('tt1','movie','Film','ja',2026,0,90,300000,0,9.0,'["Animation"]',0,'["Animation"]');
 CREATE TABLE lib_mal_anime(mal_id INTEGER,title TEXT,score REAL,members INTEGER,scored_by INTEGER,type TEXT,status TEXT,year INTEGER,__tags TEXT);
 CREATE VIEW den_mal_anime AS SELECT * FROM lib_mal_anime;
 INSERT INTO lib_mal_anime VALUES(1,'Anime',9,400000,10000,'TV','Currently Airing',2026,'["Action"]');`);
 const db={prepare(sql){return{bind(...args){return{async all(){return{success:true,results:sqlite.prepare(sql).all(...args)}}}}}}};
 try{
  const written=[];
  await publishLandingCatalogues({date,qmlRoot,owners,queryFn:(dataset,method,args)=>catalogueMethod(db,dataset,method,args),fixed:(_d,_m,_a,b)=>written.push(b)});
  for(const bundle of written)for(const q of bundle.queries)assert.deepEqual(q.result,await catalogueMethod(db,bundle.dataset,q.method,q.args));
  assert.equal(written[0].queries.find(q=>q.method==='shelf').result[0].cover,'legacy-cover');
  assert.ok(written[0].queries.find(q=>q.method==='discoverPageFiltered').result.items.length);
  assert.ok(written.find(b=>b.tab==='movies').queries.some(q=>q.result.length));
  assert.ok(written.find(b=>b.tab==='anime').queries.some(q=>q.result.length));
 }finally{sqlite.close();}
});

test('Invalid dates, missing results and oversized bundles fail publication',async()=>{
 const options={date,qmlRoot,owners,queryFn:async()=>[],fixed:()=>{}};
 await assert.rejects(publishLandingCatalogues({...options,date:'2026-02-30'}),/Invalid landing date/);
 await assert.rejects(publishLandingCatalogues({...options,queryFn:async()=>undefined}),/Missing landing result/);
 let writes=0;
 await assert.rejects(publishLandingCatalogues({...options,queryFn:async()=> 'x'.repeat(4*1024*1024),fixed:()=>writes++}),/Landing exceeds 4MiB/);
 assert.equal(writes,0);
});
