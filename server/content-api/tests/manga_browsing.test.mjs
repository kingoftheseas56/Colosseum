import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {catalogueMethod} from '../src/catalogue-semantic.ts';

test('manga metadata shelves preserve identity, unknown counts, format scope, filters and stable pagination',async()=>{
 const sqlite=new DatabaseSync(':memory:');
 sqlite.exec(`CREATE TABLE den_mal_manga(mal_id INTEGER,title TEXT,type TEXT,status TEXT,volumes,year INTEGER,members INTEGER,score REAL,explicit INTEGER,__membership TEXT,tags TEXT);
 INSERT INTO den_mal_manga VALUES
 (1,'Finished five','Manga','Finished',5,1990,100,9,0,'[["genre","Action"],["demographic","Seinen"]]','[]'),
 (2,'Finished one','Manga','Finished',1,1999,100,9,0,'[["genre","Action"]]','[]'),
 (3,'Unknown count','Manga','Finished',NULL,2000,90,8,0,'[]','[]'),
 (4,'Zero count','Manga','Finished',0,2009,80,8,0,'[]','[]'),
 (5,'Publishing','Manga','Publishing',2,2010,70,8,0,'[]','[]'),
 (6,'Short','One-shot','Finished',1,2019,60,8,0,'[]','[]'),
 (7,'Novel','Novel','Finished',1,1995,200,10,0,'[]','[]'),
 (8,'Adult','Manga','Finished',2,1995,300,10,1,'[]','[]'),
 (9,'Long','Manga','Finished',6,2020,50,8,0,'[]','[]'),
 (10,'Unknown year','Manga','Finished',2,NULL,40,8,0,'[]','[]'),
 (11,'Text count','Manga','Finished','2',0,30,8,0,'[]','[]');`);
 const db={prepare(sql){return{bind(...args){return{async all(){return{success:true,results:sqlite.prepare(sql).all(...args)}}}}}}};
 const page=(id,filters=[],explicit=false,offset=0,limit=24)=>catalogueMethod(db,'mal','discoverPageFiltered',[id,filters,explicit,offset,limit]);
 const ids=async(...args)=>(await page(...args)).items.map(r=>r.mal_id);
 try{
  assert.deepEqual(await ids('short-completed'),[1,2,6,10]);
  assert.deepEqual(await ids('publishing'),[5]);
  assert.deepEqual(await ids('one-shots'),[6]);
  assert.deepEqual((await catalogueMethod(db,'mal','discoverPage',['completed','genre','Action',false,0,24])).items.map(r=>r.mal_id),[1,2]);
  assert.deepEqual(await ids('started-1990s'),[1,2]);
  assert.deepEqual(await ids('started-2000s'),[3,4]);
  assert.deepEqual(await ids('started-2010s'),[5,6]);
  assert.deepEqual(await ids('short-completed',[],true),[8,1,2,6,10]);
  assert.deepEqual(await ids('completed',[{axis:'genre',key:'action'},{axis:'demographic',key:'seinen'}]),[1]);
  assert.deepEqual(await ids('completed',[{axis:'genre',key:"Action' OR 1=1 --"}]),[]);
  assert.deepEqual(await ids('completed',[{axis:'unsupported',key:'x'}]),[]);
  assert.deepEqual(await ids("completed' OR 1=1 --"),[]);
  const first=await page('completed',[],false,0,2),second=await page('completed',[],false,first.nextOffset,2);
  assert.deepEqual(first.items.map(r=>r.mal_id),[1,2]);assert.equal(first.exhausted,false);
  assert.deepEqual(second.items.map(r=>r.mal_id),[3,4]);
  assert.ok((await ids('popular')).includes(7),'legacy broad catalogue is unchanged');
  assert.ok((await ids('completed')).includes(3),'unknown volume count does not hide completed series');
 }finally{sqlite.close();}
});
