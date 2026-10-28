import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {DatabaseSync} from 'node:sqlite';
import {comicsMethod} from '../src/catalogue-comics.ts';
const sourcePath=process.env.COLOSSEUM_COMICS_SOURCE||(process.env.CATALOGUE_SOURCE_DIR?process.env.CATALOGUE_SOURCE_DIR+'/comics_catalog.db':undefined);
const packedPath=process.env.COLOSSEUM_COMICS_PACKED||process.env.CATALOGUE_PACKED_DB;
const enabled=!!sourcePath&&!!packedPath&&existsSync(sourcePath)&&existsSync(packedPath);
let source,packed,db;
if(enabled){source=new DatabaseSync(sourcePath,{readOnly:true});packed=new DatabaseSync(packedPath,{readOnly:true});db={prepare(sql){return{bind(...args){return{async all(){return{success:true,results:packed.prepare(sql).all(...args)}}}}}}};}
const num=v=>v==null?0:Number(v),str=v=>v==null?'':String(v);
const legacy=r=>({gcdId:num(r.gcd_id),title:str(r.title),year:num(r.year),yearEnded:num(r.year_ended),publisher:str(r.publisher),cover:str(r.cover),downloads:num(r.dls)});
const sourceLegacy=(where,args=[],order='dls DESC,s.year DESC',limit=30)=>source.prepare('SELECT s.*,COALESCE(t.downloads,0) AS dls FROM series s LEFT JOIN series_stats t ON t.series_id=s.gcd_id WHERE '+where+' ORDER BY '+order+' LIMIT ?').all(...args,limit).map(legacy);

test('SQLite fixture distinguishes absent ranks, NULL maps, canonical ties and exact page exhaustion',async()=>{
 const fixture=new DatabaseSync(':memory:');
 fixture.exec(`CREATE TABLE den_comics_curated_series(locg_id TEXT,rank INTEGER,title TEXT,norm_title TEXT,year INTEGER,publisher TEXT,cover TEXT,synopsis TEXT,__genres TEXT,__formats TEXT,__editionCount INTEGER,__availableCount INTEGER,__publishedMaxYear INTEGER,__coverage TEXT);
 CREATE VIEW lib_comics_curated_series AS SELECT * FROM den_comics_curated_series;
 CREATE TABLE lib_comics_curated_series_coverage(locg_id TEXT);
 INSERT INTO den_comics_curated_series VALUES ('b',1,'Same','same',NULL,NULL,NULL,NULL,'[]','[]',0,0,0,NULL),('a',1,'Same','same',NULL,NULL,NULL,NULL,'[]','[]',0,0,0,NULL),('unranked',NULL,NULL,NULL,NULL,NULL,NULL,NULL,'[]','[]',0,0,0,NULL);`);
 const adapter={prepare(sql){return{bind(...args){return{async all(){return{success:true,results:fixture.prepare(sql).all(...args)}}}}}}};
 try{
  const alphabetical=await comicsMethod(adapter,'discoverPageFiltered',['all',[],false,-10,1000]);
  assert.deepEqual(alphabetical.items.map(r=>r.locgId),['unranked','a','b']);assert.equal(alphabetical.exhausted,true);assert.equal(alphabetical.nextOffset,3);
  const nullRow=alphabetical.items[0];assert.equal(nullRow.title,'');assert.equal(nullRow.year,0);assert.equal(nullRow.houseComponents.popularity,0);assert.ok(Math.abs(nullRow.houseComponents.recency-(0.1+0.65*(0.1/0.3))*0.5)<1e-12);
  const popular=await comicsMethod(adapter,'discoverPageFiltered',['popular',[],true,0,2]);assert.deepEqual(popular.items.map(r=>r.locgId),['a','b']);assert.equal(popular.exhausted,false);
  const last=await comicsMethod(adapter,'discoverPageFiltered',['all',[],false,2,1]);assert.equal(last.items.length,1);assert.equal(last.exhausted,true);
  assert.deepEqual(await comicsMethod(adapter,'discoverPageFiltered',['popular',[{axis:'sql',key:'DROP TABLE'}],false,0,24]),{items:[],nextOffset:0,exhausted:true,freshness:'bundled'});
 }finally{fixture.close();}
});

test('Comics full-source SQLite legacy search and exact-match orders',{skip:!enabled},async()=>{
 for(const title of ['Batman','One Piece','Justice League','Spider-Man','%','_','the amazing spider man']){
  const words=title.trim().split(/[^\p{L}\p{N}]+/u).filter(Boolean);const escape=s=>s.replace(/[\\%_]/g,'\\$&');
  const expected=words.length?sourceLegacy(words.map(()=>"s.title LIKE ? ESCAPE '\\'").join(' AND '),[...words.map(w=>'%'+escape(w)+'%'),title,escape(title)+'%'],"CASE WHEN LOWER(s.title)=LOWER(?) THEN 0 WHEN s.title LIKE ? ESCAPE '\\' THEN 1 ELSE 2 END,dls DESC,s.year DESC",50):[];
  assert.deepEqual(await comicsMethod(db,'search',[title,50]),expected,title);
  assert.deepEqual(await comicsMethod(db,'exactMatches',[title]),sourceLegacy('LOWER(s.title)=LOWER(?)',[title],'dls DESC',-1));
 }
});
test('Comics source SQLite shelf and detail QVariant NULL conversion',{skip:!enabled},async()=>{
 const cases=[['stocked','1=1',[]],['deep','COALESCE(t.downloads,0)>=10',[]],['publisher','s.publisher=?',['Marvel']],['decade','s.year BETWEEN ? AND ?',[2010,2019]],['fanmade','EXISTS(SELECT 1 FROM download dl WHERE dl.series_id=s.gcd_id AND dl.fan_made=1)',[]]];
 for(const[kind,where,args]of cases)assert.deepEqual(await comicsMethod(db,'shelf',[kind,kind==='decade'?'2010':args[0]||'',20]),sourceLegacy(where,args,'dls DESC,s.year DESC',20));
 for(const r of source.prepare('SELECT gcd_id FROM series ORDER BY gcd_id LIMIT 10').all()){
  const [s]=source.prepare('SELECT s.*,COALESCE(t.downloads,0) AS dls,COALESCE(t.kinds,\'\') AS kinds,COALESCE(t.latest_post,\'\') AS latest FROM series s LEFT JOIN series_stats t ON t.series_id=s.gcd_id WHERE s.gcd_id=?').all(r.gcd_id);
  assert.deepEqual(await comicsMethod(db,'series',[r.gcd_id]),{...legacy(s),issueCount:num(s.issue_count),synopsis:str(s.synopsis),kinds:str(s.kinds),latestPost:str(s.latest)});
 }
});
test('Download rows and mirrors preserve source identities, ordering and booleans',{skip:!enabled},async()=>{
 for(const {series_id:id}of source.prepare('SELECT DISTINCT series_id FROM download LIMIT 5').all()){
  const expected=source.prepare('SELECT * FROM download WHERE series_id=? ORDER BY date DESC,post_id DESC').all(id).map(r=>({postId:num(r.post_id),title:str(r.title),link:str(r.link),date:str(r.date),kind:str(r.kind),method:str(r.method),fanMade:num(r.fan_made)!==0,yearStart:num(r.year_start),mirrors:source.prepare('SELECT url,host,label FROM download_mirror WHERE post_id=? ORDER BY rowid ASC').all(r.post_id).map(m=>({url:str(m.url),host:str(m.host),label:str(m.label)}))}));
  assert.deepEqual(await comicsMethod(db,'downloadsFor',[id]),expected);
 }
});
test('Full curated ranked list and genre shelves match original SQLite',{skip:!enabled},async()=>{
 const expected=source.prepare("SELECT s.*,COALESCE((SELECT GROUP_CONCAT(genre) FROM curated_genre g WHERE g.locg_id=s.locg_id),'') AS genres FROM curated_series s ORDER BY rank ASC").all().map(r=>({locgId:str(r.locg_id),rank:num(r.rank),title:str(r.title),year:num(r.year),slug:str(r.slug),publisher:str(r.publisher),cover:str(r.cover),genres:str(r.genres)}));
 assert.deepEqual(await comicsMethod(db,'curatedRanked',[]),expected);
 const shelves=source.prepare('SELECT genre,COUNT(*) AS total FROM curated_genre GROUP BY genre ORDER BY total DESC,genre ASC').all().map(g=>({name:str(g.genre),count:num(g.total),covers:source.prepare("SELECT s.cover FROM curated_series s JOIN curated_genre g ON g.locg_id=s.locg_id WHERE g.genre=? AND s.cover!='' ORDER BY s.rank ASC LIMIT 3").all(g.genre).map(r=>str(r.cover))}));
 assert.deepEqual(await comicsMethod(db,'curatedGenreShelves',[3]),shelves);
});
test('Curated coverage/source details match SQLite with original nullable gcd ID',{skip:!enabled},async()=>{
 const ids=source.prepare('SELECT DISTINCT locg_id FROM curated_edition LIMIT 5').all().map(r=>r.locg_id);
 for(const id of ids){const value=await comicsMethod(db,'curatedSeries',[id]);const editions=source.prepare('SELECT * FROM curated_edition WHERE locg_id=? ORDER BY id ASC').all(id);
  assert.equal(value.editions.length,editions.length);
  for(let i=0;i<editions.length;i++){
   const e=editions[i],actual=value.editions[i];assert.equal(actual.available,num(e.available)!==0);assert.equal(actual.getcomicsPost,str(e.getcomics_post));assert.equal(actual.pages,num(e.pages));
   const sources=source.prepare('SELECT * FROM curated_source WHERE curated_edition_id=? ORDER BY fan_made ASC,source_date DESC,post_id DESC').all(e.id);
   assert.deepEqual(actual.sources.map(s=>s.postId),sources.map(s=>num(s.post_id)));
   for(let n=0;n<sources.length;n++){const s=sources[n];assert.equal(actual.sources[n].fanMade,num(s.fan_made)!==0);assert.deepEqual(actual.sources[n].mirrors,source.prepare('SELECT url,host,label FROM download_mirror WHERE post_id=? ORDER BY rowid ASC').all(s.post_id).map(m=>({url:str(m.url),host:str(m.host),label:str(m.label)})));}
  }
  const c=source.prepare('SELECT * FROM curated_series_coverage WHERE locg_id=?').get(id);if(c){assert.equal(value.coverage.gcdSeriesId,c.gcd_series_id);assert.equal(value.coverage.coverageRatio,num(c.coverage_ratio));}
 }
});
let referenceBase;
function referenceDiscovery(catalog,facets=[]){
 if(!referenceBase){
  const genres=new Map(),editions=new Map(),coverage=new Map();
  for(const row of source.prepare('SELECT * FROM curated_genre').all()){const list=genres.get(row.locg_id)||[];list.push(row.genre);genres.set(row.locg_id,list);}
  for(const row of source.prepare('SELECT * FROM curated_edition').all()){const list=editions.get(row.locg_id)||[];list.push(row);editions.set(row.locg_id,list);}
  for(const row of source.prepare('SELECT * FROM curated_series_coverage').all())coverage.set(row.locg_id,row);
  referenceBase=source.prepare('SELECT * FROM curated_series').all().map(r=>{const e=editions.get(r.locg_id)||[],c=coverage.get(r.locg_id)||{},g=genres.get(r.locg_id)||[];let maxYear=0;for(const edition of e)for(const match of str(edition.published).matchAll(/\d{4}/g)){const y=Number(match[0]);if(y>=1900&&y<=2100)maxYear=Math.max(maxYear,y);}return{...r,c,g,e,maxYear,avail:e.filter(e=>e.available===1&&e.getcomics_post!=null&&e.getcomics_post!=='').length};});
 }
 let rows=referenceBase.map(r=>({...r}));
 for(const f of facets)rows=rows.filter(r=>f.axis==='genre'?r.g.some(g=>str(g).toLowerCase()===f.key.toLowerCase()):f.axis==='publisher'?str(r.publisher).toLowerCase()===f.key.toLowerCase():f.axis==='format'?r.e.some(e=>str(e.format).trim().toLowerCase()===f.key.toLowerCase()):f.key==='available'?num(r.c.official_source_count)>0:str(r.c.availability_state).toLowerCase()===f.key.toLowerCase());
 if(catalog==='complete-runs')rows=rows.filter(r=>r.c.availability_state==='complete');else if(catalog==='near-complete')rows=rows.filter(r=>r.c.availability_state==='near_complete');else if(catalog==='recently-available')rows=rows.filter(r=>num(r.c.official_source_count)>0&&str(r.c.latest_source_date));else if(catalog==='community-collections')rows=rows.filter(r=>num(r.c.community_source_count)>0);
 const ranks=rows.filter(r=>r.rank!=null).map(r=>num(r.rank)),min=Math.min(...ranks),max=Math.max(...ranks);
 for(const r of rows){const ranked=r.rank!=null;const pop=ranked?(min===max?1:1-(num(r.rank)-min)/(max-min)):0;const rec=r.maxYear?Math.min(1,Math.max(0,(r.maxYear-1980)/(new Date().getFullYear()-1980))):0.5;const fraction=r.e.length?r.avail/r.e.length:0;const meta=[r.cover,r.synopsis,r.publisher,r.g.join(',')].filter(v=>str(v)).length*0.05/4;r.score=(ranked?0.65:0)*pop+(ranked?0.2:0.2+0.65*(0.2/0.3))*fraction+(ranked?0.1:0.1+0.65*(0.1/0.3))*rec+meta;}
 const cmp=(a,b)=>str(a)<str(b)?-1:str(a)>str(b)?1:0;const canon=(a,b)=>cmp(a.norm_title,b.norm_title)||num(a.year)-num(b.year)||cmp(a.locg_id,b.locg_id);
 rows.sort((a,b)=>catalog==='popular'?b.score-a.score||canon(a,b):catalog==='most-stocked'?b.e.length-a.e.length||b.score-a.score||(b.e.length?b.avail/b.e.length:0)-(a.e.length?a.avail/a.e.length:0)||canon(a,b):catalog==='new-releases'?b.maxYear-a.maxYear||b.score-a.score||canon(a,b):catalog==='recently-available'?cmp(b.c.latest_source_date,a.c.latest_source_date)||b.score-a.score||canon(a,b):catalog==='complete-runs'||catalog==='near-complete'?num(b.c.coverage_ratio)-num(a.c.coverage_ratio)||b.score-a.score||canon(a,b):catalog==='community-collections'?num(b.c.community_source_count)-num(a.c.community_source_count)||cmp(b.c.latest_source_date,a.c.latest_source_date)||canon(a,b):canon(a,b));return rows;
}
test('All eight native discovery rankings, scoped normalization, bounds and ties match source SQLite',{skip:!enabled},async()=>{
 for(const catalog of ['all','popular','most-stocked','new-releases','recently-available','complete-runs','near-complete','community-collections'])for(const facets of [[],[{axis:'publisher',key:'Marvel'}],[{axis:'genre',key:'Superhero'}]]){
  const ref=referenceDiscovery(catalog,facets);const page=await comicsMethod(db,'discoverPageFiltered',[catalog,facets,false,0,100]);assert.deepEqual(page.items.map(r=>r.locgId),ref.slice(0,100).map(r=>str(r.locg_id)),catalog+JSON.stringify(facets));
  for(let i=0;i<page.items.length;i++){assert.ok(Math.abs(page.items[i].houseScore-ref[i].score)<1e-12);assert.ok(Math.abs(Object.values(page.items[i].houseComponents).reduce((a,b)=>a+b,0)-page.items[i].houseScore)<1e-12);}
  assert.equal(page.exhausted,page.items.length>=ref.length);assert.equal(page.nextOffset,page.items.length);
 }
});
test.after(()=>{source?.close();packed?.close();});
