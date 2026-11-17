import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {fileURLToPath} from 'node:url';
import {audiobooksMethod} from '../src/catalogue-audiobooks.ts';
import {catalogueMethod,catalogueMethods} from '../src/catalogue-semantic.ts';
import {prebuiltCatalogue} from '../src/catalogue-prebuilt.ts';

const fixture=fileURLToPath(new URL('./fixtures/audiobooks-cc0-fixture.sqlite',import.meta.url));
const db=new DatabaseSync(fixture,{readOnly:true});
const adapter={prepare(sql){return {bind(...args){return {async all(){
  return {success:true,results:db.prepare(sql).all(...args)};
}}}}}};
test.after(()=>db.close());

test('audiobook catalogue methods are registered for semantic RPC',()=>{
  assert.deepEqual(catalogueMethods.audiobooks,['search','work','recordingByAsin','recordingByIsbn','series','latest','byPerson']);
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
});
test('latest and person listings page and contain stable work ids',async()=>{
  const first=await audiobooksMethod(adapter,'latest',[0]);
  const next=await audiobooksMethod(adapter,'latest',[1]);
  assert.ok(first.length>0);
  assert.ok(first.every(r=>r.id&&r.workId));
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
