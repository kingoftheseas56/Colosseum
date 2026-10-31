import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {DatabaseSync} from 'node:sqlite';
import {catalogueMethod} from '../src/catalogue-semantic.ts';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';

for(const backend of ['SQLite','D1'])test(backend+': publication filters precede pagination and preserve ordered MAL identity', async () => {
 const fixture=readFileSync(new URL('../../../tests/fixtures/manhwa_discover.sql',import.meta.url),'utf8');
 const view=`CREATE VIEW den_mal_manga AS SELECT m.*,
  (SELECT json_group_array(json_array(axis,value)) FROM classification c WHERE c.medium='manga' AND c.mal_id=m.mal_id) AS __membership FROM manga m`;
 const sqlite=backend==='SQLite'?new DatabaseSync(':memory:'):null;
 const mf=backend==='D1'?new Miniflare(convertV4MiniflareOptions({modules:true,script:'export default {fetch(){return new Response("ok")}}',compatibilityDate:'2026-10-06',d1Databases:{LIBRARY:'manhwa-parity'}})):null;
 const db=sqlite?{prepare(sql){return{bind(...args){return{async all(){return{success:true,results:sqlite.prepare(sql).all(...args)}}}}}}}:await mf.getD1Database('LIBRARY');
 if(sqlite){sqlite.exec(fixture);sqlite.exec(view);}
 else{for(const sql of fixture.split(';').filter(s=>s.trim()))await db.prepare(sql).run();await db.prepare(view).run();}
 const format={axis:'format',key:'manhwa'},action={axis:'genre',key:'action'},seinen={axis:'demographic',key:'seinen'};
 const page=(catalog='popular',filters=[format],explicit=false,offset=0,limit=24)=>catalogueMethod(db,'mal','discoverPageFiltered',[catalog,filters,explicit,offset,limit]);
 const ids=page=>page.items.map(r=>r.mal_id);
 try {
  const first=await page(),second=await page('popular',[format],false,first.nextOffset);
  assert.deepEqual(ids(first),[2,6,...Array.from({length:22},(_,i)=>100+i)]);
  assert.equal(first.nextOffset,24);assert.equal(first.exhausted,false);
  assert.deepEqual(ids(second),[122,123,124,125,126,127,128,129,7,8]);
  assert.equal(second.nextOffset,34);assert.equal(second.exhausted,true);
  assert.equal(first.items[0].title,'Solo Leveling');assert.equal(first.items[0].type,'Manhwa');
  for(const filters of [[format,action,seinen],[seinen,format,action],[action,seinen,format]])
   assert.deepEqual(ids(await page('popular',filters)),[2]);
  assert.deepEqual(ids(await page('popular',[action,format])),[2,6]);
  assert.deepEqual(ids(await page('popular',[format],true)).slice(0,3),[5,2,6]);
  assert.deepEqual(ids(await page('popular',[{axis:'format',key:'MANHUA'}])),[3]);
  assert.deepEqual(ids(await page('popular',[{axis:'format',key:'manga'}])),[1]);
  for(const key of ['novel','unknown',"Manhwa' OR 1=1 --"])
   assert.deepEqual(ids(await page('popular',[{axis:'format',key}])),[]);
  assert.deepEqual(ids(await page('popular',[format,{axis:'unknown',key:'x'}])),[]);
  assert.deepEqual(ids(await page('popular',[])).slice(0,5),[1,2,3,4,6],'legacy broad scope includes Manhua and prose');
  const top=await page('top-rated');assert.equal(ids(top)[0],2,'established Solo outranks a single-vote 9.9');
  const scopedTop=await page('top-rated',[format,action]);assert.deepEqual(ids(scopedTop),[6,2],'Bayesian mean is scoped to this filtered population');
  const latest=await page('new-releases');assert.deepEqual(ids(latest).slice(0,2),[6,2]);
  assert.ok(!ids(latest).includes(7)&&!ids(latest).includes(8));
  const trending=await page('trending');assert.deepEqual(ids(trending),ids(first));assert.equal(trending.fallbackCatalog,'popular');
 } finally {sqlite?.close();await mf?.dispose();}
});
