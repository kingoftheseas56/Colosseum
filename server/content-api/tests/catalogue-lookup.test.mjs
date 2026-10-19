import test from 'node:test';import assert from 'node:assert/strict';import {DatabaseSync}from'node:sqlite';
import {imdbMethod,malMethod,imdbRow,malRow}from'../src/catalogue-semantic.ts';
import {indexedStatement}from'../src/catalogue-lookup.ts';
const path=process.env.CATALOGUE_INDEXED_DB,source=process.env.CATALOGUE_SOURCE_DIR;
const adapt=db=>({prepare(sql){return{bind(...args){return{async all(){return{success:true,results:db.prepare(sql).all(...args)}}}}}}});
test('indexed key and dual normalized-title prefix results preserve full native SQL',{skip:!path||!source},async()=>{
 const packed=new DatabaseSync(path,{readOnly:true}),original=new DatabaseSync(':memory:');for(const name of ['imdb','mal'])original.prepare('ATTACH DATABASE ? AS '+name).run(source+'/'+name+'_catalog.db');original.exec('PRAGMA query_only=ON');const db=adapt(packed);
 try{
  for(const norm of ['star','one piece','naruto','a','zzzzzzzz']){
   const rows=original.prepare('SELECT * FROM imdb.title WHERE norm_title LIKE ? ORDER BY CASE WHEN norm_title=? THEN 0 ELSE 1 END,year DESC,rating DESC,votes DESC,tt ASC LIMIT 20').all(norm+'%',norm);
   assert.deepEqual(await imdbMethod(db,'search',[norm,20]),rows.map(r=>imdbRow(r,true)));
   for(const medium of ['anime','manga']){
    const rows=original.prepare('SELECT * FROM mal.'+medium+' WHERE norm_title LIKE ? OR norm_title_english LIKE ? ORDER BY CASE WHEN norm_title=? THEN 0 ELSE 1 END,year DESC,score DESC,mal_id ASC LIMIT 20').all(norm+'%',norm+'%',norm);
    assert.deepEqual(await malMethod(db,'search',[norm,20,medium]),rows.map(r=>malRow(r,medium,true)));
    const exact=original.prepare('SELECT * FROM mal.'+medium+' WHERE (norm_title=? OR norm_title_english=?) AND (?=0 OR year=?) ORDER BY year DESC,score DESC,mal_id ASC').all(norm,norm,0,0);
    assert.deepEqual(await malMethod(db,'matchByTitle',[norm,0,medium]),exact.map(r=>malRow(r,medium,true)));
   }
  }
  for(const id of [2,13,99999999]){const r=original.prepare('SELECT * FROM mal.manga WHERE mal_id=?').get(id);assert.deepEqual(await malMethod(db,'mangaById',[id]),r?malRow(r,'manga'):{});}
  const s=indexedStatement('mal','manga','key',2);const plan=packed.prepare('EXPLAIN QUERY PLAN '+s.sql).all(...s.args);assert.ok(plan.some(r=>String(r.detail).includes('INTEGER PRIMARY KEY')));
 }finally{packed.close();original.close();}
});
