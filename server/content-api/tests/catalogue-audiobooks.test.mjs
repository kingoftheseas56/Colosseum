import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {fileURLToPath} from 'node:url';
import {audiobooksMethod} from '../src/catalogue-audiobooks.ts';
import {catalogueMethod,catalogueMethods} from '../src/catalogue-semantic.ts';
import {prebuiltCatalogue} from '../src/catalogue-prebuilt.ts';

const fixture=fileURLToPath(new URL('./fixtures/audiobooks-cc0-fixture.sqlite',import.meta.url));
const db=new DatabaseSync(fixture,{readOnly:true});
const sqliteAdapter=database=>({prepare(sql){return {bind(...args){return {async all(){
  return {success:true,results:database.prepare(sql).all(...args)};
}}}}}});
const adapter=sqliteAdapter(db);
test.after(()=>db.close());

test('audiobook catalogue methods are registered for semantic RPC',()=>{
  assert.deepEqual(catalogueMethods.audiobooks,['search','work','recordingByAsin','recordingByIsbn','series','latest','byPerson','shelves','shelf','genres','genre']);
});
test('fixture is a real CC0 AudioSilo subset with exactly thirty works',()=>{
  assert.equal(db.prepare('SELECT count(*) n FROM works').get().n,30);
  assert.equal(db.prepare('SELECT count(*) n FROM works WHERE title=?').get('Rhythm of War').n,1);
  assert.equal(db.prepare('SELECT count(*) n FROM works WHERE id=?').get('dune').n,1);
  assert.equal(db.prepare("SELECT count(*) n FROM sqlite_master WHERE name='work_descriptions'").get().n,0);
});
test('search indexes title and associated people, rejects malformed text and clamps limits',async()=>{
  const works=await catalogueMethod(adapter,'audiobooks','search',['Rhythm of War',1000]);
  assert.ok(works.some(r=>r.id==='rhythm-of-war'));
  assert.equal(works.length<=100,true);
  assert.deepEqual(await audiobooksMethod(adapter,'search',['Bank Note',10]),[]);
  assert.ok((await audiobooksMethod(adapter,'search',['ari kami',10])).some(r=>r.id==='1-ari-kami-book-1'));
  assert.deepEqual(await audiobooksMethod(adapter,'search',['',10]),[]);
  assert.deepEqual(await audiobooksMethod(adapter,'search',['"""',10]),[]);
});
test('work returns the real full cast and single-reader recordings and linked authors',async()=>{
  const v=await audiobooksMethod(adapter,'work',['rhythm-of-war']);
  assert.equal(v.id,'rhythm-of-war');
  assert.equal(v.recordings.length,2);
  assert.deepEqual(new Set(v.recordings.map(r=>r.id)),new Set(['full-cast-2020','kate-reading-2020']));
  assert.ok(v.recordings.every(r=>r.narrators.length>0));
  assert.ok(v.authors.length>0);
  const undated=await audiobooksMethod(adapter,'work',['1-000-000-bank-note-other-new-stories']);
  assert.equal(undated.recordings.length,1);
  assert.equal(undated.recordings[0].releaseDate,undefined);
});
test('Dune retains separate ordered series numbers without choosing one',async()=>{
  const v=await audiobooksMethod(adapter,'work',['dune']);
  assert.equal(v.title,'Dune');
  assert.ok(v.series.length>1);
  assert.ok(v.series.some(s=>s.name==='Dune'&&s.numberText==='1'));
  assert.ok(v.series.some(s=>s.name==='The Dune Sequence'&&s.numberText==='12'));
  assert.equal(v.series.find(s=>s.name==='The Dune Sequence').positionSort,12);
});
test('all known ASIN and ISBN links resolve to their actual recording',async()=>{
  const asins=db.prepare('SELECT * FROM recording_asins ORDER BY asin LIMIT 6').all();
  assert.ok(asins.length>0);
  for(const a of asins){const r=await audiobooksMethod(adapter,'recordingByAsin',[a.asin]); assert.equal(r.id,a.recording_id);assert.equal(r.workId,a.work_id);}
  const isbns=db.prepare('SELECT * FROM recording_isbns ORDER BY isbn LIMIT 3').all();
  for(const i of isbns){const r=await audiobooksMethod(adapter,'recordingByIsbn',[i.isbn]); assert.equal(r.id,i.recording_id);assert.equal(r.workId,i.work_id);}
});
test('series returns works in numeric position sort and preserves display text',async()=>{
  const s=db.prepare('SELECT series_id FROM work_series WHERE position_sort IS NOT NULL GROUP BY series_id HAVING count(*)>=2 ORDER BY count(*) DESC LIMIT 1').get();
  assert.ok(s);
  const v=await audiobooksMethod(adapter,'series',[s.series_id]);
  assert.equal(v.id,s.series_id);
  assert.ok(v.works.length>=2);
  for(let i=1;i<v.works.length;i++)assert.ok(v.works[i-1].positionSort<=v.works[i].positionSort);
  assert.ok(v.works.every(r=>typeof r.numberText==='string'));

  const memory=new DatabaseSync(':memory:');
  try{
    memory.prepare('ATTACH DATABASE ? AS fixture').run(fixture);
    for(const table of ['works','work_series','series','recordings']){
      memory.exec('CREATE TABLE '+table+' AS SELECT * FROM fixture.'+table);
    }
    memory.exec('CREATE VIRTUAL TABLE search_fts USING fts5(kind UNINDEXED,id UNINDEXED,title,people)');
    memory.exec("INSERT INTO search_fts VALUES('work','rhythm-of-war','Rhythm of War The Stormlight Archive','')");
    memory.exec(`INSERT INTO work_series VALUES
      ('1-ari-kami-book-1','the-stormlight-archive','2.1',2.1),
      ('1-000-000-bank-note-other-new-stories','the-stormlight-archive','2.2',2.2),
      ('2061-odyssey-three','the-stormlight-archive','2.3',2.3);
      UPDATE recordings SET release_date='9999-12-31',cover_url='https://example.test/future.jpg'
      WHERE (work_id='rhythm-of-war' AND id='full-cast-2020') OR work_id='2061-odyssey-three';`);
    const stormlight=await audiobooksMethod(sqliteAdapter(memory),'series',['the-stormlight-archive']);
    assert.deepEqual(stormlight.works.map(r=>r.id),['rhythm-of-war']);
    const found=await audiobooksMethod(sqliteAdapter(memory),'search',['stormlight archive',20]);
    assert.deepEqual(found.map(r=>r.id),['1-ari-kami-book-1','rhythm-of-war']);
    assert.deepEqual(await audiobooksMethod(sqliteAdapter(memory),'search',['The Stormlight Archive',1]),found.slice(0,1));
    assert.equal(stormlight.works[0].coverUrl,'https://m.media-amazon.com/images/I/51FF+pax2vL._SL500_.jpg');
  }finally{memory.close();}
});
test('latest and person listings page and contain stable work ids',async()=>{
  const first=await audiobooksMethod(adapter,'latest',[0]);
  const next=await audiobooksMethod(adapter,'latest',[1]);
  assert.ok(first.length>0);
  assert.ok(first.every(r=>r.id&&r.workId));
  const today=new Date().toISOString().slice(0,10);
  assert.ok([...first,...next].every(r=>r.releaseDate?.length===10 && r.releaseDate<=today));
  assert.ok([...first,...next].some(r=>r.language==='ru'));
  assert.equal(new Set([...first,...next].map(r=>r.workId+'|'+r.id)).size,first.length+next.length);
  const author=db.prepare('SELECT person_id FROM work_authors LIMIT 1').get();
  const works=await audiobooksMethod(adapter,'byPerson',[author.person_id,0]);
  assert.ok(works.some(r=>r.id));
});
test('unknown and malicious identifiers produce empty results instead of errors',async()=>{
  for(const method of ['work','series','recordingByAsin','recordingByIsbn']){
    assert.deepEqual(await audiobooksMethod(adapter,method,["x' OR 1=1 --"]),{});
    assert.deepEqual(await catalogueMethod(adapter,'audiobooks',method,['not-a-real-id']),{});
  }
  assert.deepEqual(await audiobooksMethod(adapter,'byPerson',['missing',0]),[]);
});

test('audiobooks series bypasses legacy prebuilt lookup and reaches D1',async()=>{
  const revision='a'.repeat(64);
  const assets={async fetch(request){
    if(new URL(request.url).pathname.endsWith('/manifest.json'))
      return Response.json({version:1,revision,lists:{},fixed:{},search:{},lookups:{}});
    return new Response(null,{status:404});
  }};
  assert.equal(await prebuiltCatalogue(assets,revision,'audiobooks','series',['dune']),null);
  assert.equal((await catalogueMethod(adapter,'audiobooks','series',['dune'])).name,'Dune');
});

test('Discover shelves expose exactly five static rows, with baked narrator picks',async()=>{
  const rows=await catalogueMethod(adapter,'audiobooks','shelves',[]);
  assert.equal(rows.length,5);
  assert.deepEqual(rows.slice(0,2),[
    {key:'new-releases',title:'New releases'},
    {key:'dramatized',title:'Fully dramatized'}]);
  const picks=db.prepare('SELECT person_id,name FROM narrator_shelves ORDER BY rank').all();
  assert.deepEqual(picks.map(p=>[p.person_id,p.name]),[
    ['scott-brick','Scott Brick'],['simon-vance','Simon Vance'],['robin-miles','Robin Miles']]);
  assert.deepEqual(rows.slice(2),picks.map(p=>({key:'narrator:'+p.person_id,title:'Narrated by '+p.name})));
  assert.equal(new Set(rows.map(r=>r.key)).size,5);
});
test('Discover pages use 20 recordings, stable identity, no future releases, sparse item shape',async()=>{
  const result=await catalogueMethod(adapter,'audiobooks','shelf',['new-releases',0]);
  assert.equal(result.page,0);
  assert.ok(result.items.length>0 && result.items.length<=20);
  assert.equal(result.hasMore,db.prepare("SELECT count(*) c FROM recordings WHERE language='en' AND length(release_date)=10 AND date(release_date)<=date('now')").get().c>20);
  assert.equal(result.items[0].workId,'1-800-starship');
  for(const item of result.items){
    assert.equal(typeof item.workId,'string');assert.equal(typeof item.recordingId,'string');
    assert.ok(item.title);assert.ok(Array.isArray(item.authors));assert.ok(Array.isArray(item.narrators));
    assert.equal(db.prepare('SELECT language FROM recordings WHERE work_id=? AND id=?').get(item.workId,item.recordingId).language,'en');
    assert.equal(typeof item.dramatized,'boolean');
    assert.ok(item.releaseDate<=(new Date()).toISOString().slice(0,10));
    assert.ok(Object.values(item).every(v=>v!==null && v!==undefined && v!==''));
  }
  const next=await audiobooksMethod(adapter,'shelf',['new-releases',1]);
  assert.equal(next.page,1);
  assert.equal(new Set([...result.items,...next.items].map(x=>x.workId+'/'+x.recordingId)).size,result.items.length+next.items.length);
});

test('Dramatized and narrator shelves contain only evidenced recordings',async()=>{
  const rows=await audiobooksMethod(adapter,'shelves',[]);
  for(const s of rows.slice(1)){
    const p=await audiobooksMethod(adapter,'shelf',[s.key,0]);
    assert.equal(p.page,0);assert.equal(typeof p.hasMore,'boolean');
    for(const i of p.items){
      if(s.key==='dramatized') assert.equal(i.dramatized,true);
      else assert.ok(db.prepare("SELECT 1 FROM recordings r JOIN recording_narrators n ON n.work_id=r.work_id AND n.recording_id=r.id WHERE r.work_id=? AND r.id=? AND n.person_id=? AND r.language='en'").get(i.workId,i.recordingId,s.key.slice(9)));
    }
  }
  assert.deepEqual(await audiobooksMethod(adapter,'shelf',['bad-shelf',0]),{items:[],page:0,hasMore:false});
});
test('narrator shelves exclude LibriVox and non-English recordings, across all pages',async()=>{
  for(const who of ['scott-brick','simon-vance','robin-miles']){
    const actual=[];
    for(let page=0;;page++){
      const result=await audiobooksMethod(adapter,'shelf',['narrator:'+who,page]);
      for(const item of result.items){
        assert.ok(db.prepare("SELECT 1 FROM recordings WHERE work_id=? AND id=? AND language='en' AND (librivox_id IS NULL OR librivox_id='')").get(item.workId,item.recordingId));
        actual.push(item.workId+'/'+item.recordingId);
      }
      if(!result.hasMore)break;
    }
    assert.equal(actual.length,db.prepare('SELECT english_recordings n FROM narrator_shelves WHERE person_id=?').get(who).n);
    assert.ok(actual.length>0);
    assert.equal(new Set(actual).size,actual.length);
  }
});
test('narrator commercial filtering is not merely incidental to fixture selection',async()=>{
  const memory=new DatabaseSync(':memory:');
  try{
    for(const {sql} of db.prepare("SELECT sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'search_fts%' ORDER BY name").all())memory.exec(sql);
    memory.exec(`INSERT INTO works VALUES('w','Work','Work','en',2020);
      INSERT INTO people VALUES('scott-brick','Scott Brick');
      INSERT INTO narrator_shelves VALUES(1,'scott-brick','Scott Brick',2);`);
    for(const [rid,language,lv,date,cover] of [
      ['null','en',null,'2020-01-01','https://example.test/cover.jpg'],
      ['empty',' English ','','2021-01-01',null],
      ['code','eng',null,'2019-01-01','https://example.test/back.jpg'],
      ['lv','en',123,'2020-01-01',null],['zero','en',0,'2020-01-01',null],
      ['foreign','fr',null,'2020-01-01',null],['future','en',null,'9999-12-31',null],
      ['undated','en',null,null,null],['invalid','en',null,'2020-02-31',null]]){
      memory.prepare("INSERT INTO recordings(id,work_id,language,dramatized,dramatized_signals,librivox_id,release_date,cover_url) VALUES(?,'w',?,1,'[]',?,?,?)").run(rid,language,lv,date,cover);
      memory.prepare("INSERT INTO recording_narrators VALUES('w',?,'scott-brick',0)").run(rid);
    }
    const local=sqliteAdapter(memory);
    const result=await audiobooksMethod(local,'shelf',['narrator:scott-brick',0]);
    assert.deepEqual(result.items.map(i=>i.recordingId),['null','code','empty']);
    assert.equal(result.hasMore,false);
    memory.exec("INSERT INTO work_genres VALUES('w','fantasy')");
    for(const [method,key,expected] of [
      ['shelf','new-releases',['null','code','empty','lv','zero']],
      ['shelf','dramatized',['null','code','empty','lv','zero']],
      ['genre','fantasy',['null','code','empty','lv','zero']]]){
      assert.deepEqual((await audiobooksMethod(local,method,[key,0])).items.map(i=>i.recordingId),expected);
    }
    // Place the cover boundary exactly between pages to exercise the offset
    // after all covered recordings have been returned.
    const addCovered=memory.prepare(`INSERT INTO recordings(id,work_id,language,
      dramatized,dramatized_signals,release_date,cover_url)
      VALUES(?,'w','en',1,'[]','2018-01-01',?)`);
    for(let n=0;n<18;n++)addCovered.run('covered-'+n,'https://example.test/'+n+'.jpg');
    const first=await audiobooksMethod(local,'genre',['fantasy',0]);
    const second=await audiobooksMethod(local,'genre',['fantasy',1]);
    assert.equal(first.items.length,20);
    assert.equal(first.hasMore,true);
    assert.ok(first.items.every(r=>r.coverUrl));
    assert.deepEqual(second.items.map(r=>r.recordingId),['empty','lv','zero']);
    assert.equal(second.hasMore,false);
  }finally{memory.close();}
});
test('Genre mosaic contains only grounded cover pointers, genre pages and unknown genre',async()=>{
  const gs=await audiobooksMethod(adapter,'genres',[]);
  assert.ok(gs.length>0);
  assert.equal(gs.find(g=>g.genre==='fantasy').count,7);
  assert.ok(!gs.some(g=>g.genre==='classics'||g.genre==='urban-fantasy'));
  assert.equal(gs.find(g=>g.genre==='fantasy').covers[0],db.prepare(
    "SELECT cover_url FROM recordings WHERE work_id='dune' AND id='nikola-muckajev-2021'").get().cover_url);
  for(const g of gs){
    assert.ok(g.genre && g.count>0);
    assert.equal(g.covers.length,4);
    assert.ok(g.covers.every(s=>typeof s==='string' && /^https?:\/\//.test(s)));
  }
  assert.ok(gs.every((g,i)=>i===0 || gs[i-1].count>=g.count));
  const page=await audiobooksMethod(adapter,'genre',[gs[0].genre,0]);
  assert.ok(page.items.length>0 && page.items.length<=20);
  assert.ok(page.items.every(i=>db.prepare('SELECT 1 FROM work_genres WHERE work_id=? AND genre=?').get(i.workId,gs[0].genre)));
  assert.deepEqual(await audiobooksMethod(adapter,'genre',['does-not-exist',0]),{items:[],page:0,hasMore:false});
});
