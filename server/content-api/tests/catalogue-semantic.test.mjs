import test from 'node:test';
import assert from 'node:assert/strict';
import {imdbMethod,malMethod,imdbRow,malRow} from '../src/catalogue-semantic.ts';
function recorder(rows=[]){const calls=[];return {calls,prepare(sql){return {bind(...args){calls.push({sql,args});return {async all(){return {success:true,results:rows};}};}};}};}
test('IMDb mapping retains QVariant conversions and JSON arrays',()=>{
 const r=imdbRow({tt:'tt1',year:null,rating:null,isAnime:1,genres:'["Drama"]'},true);
 assert.equal(r.year,0);assert.equal(r.rating,0);assert.equal(r.isAnime,true);assert.deepEqual(r.genres,['Drama']);assert.equal(r.synopsis,'');
});
test('MAL optional score and year remain absent; credit and cover shape matches Jikan',()=>{
 const r=malRow({mal_id:1,score:null,year:0,cover:'cover',tags:'["Action"]',credits:'["Writer"]'},'manga');
 assert.equal('score'in r,false);assert.equal('year'in r,false);assert.deepEqual(r.authors,[{name:'Writer'}]);assert.equal(r.images.jpg.large_image_url,'cover');
});
test('Unknown filters and methods do not become SQL',async()=>{
 const db=recorder();assert.deepEqual(await imdbMethod(db,'titleCatalog',[{sql:'DROP TABLE title'}]),[]);assert.deepEqual(await malMethod(db,'animeCatalog',[{bad:1}]),[]);assert.equal(db.calls.length,0);
 await assert.rejects(imdbMethod(db,'DROP TABLE title',[]),/UNKNOWN_CATALOGUE_METHOD/);
});
test('Membership preserves duplicate JOIN rows and binds hostile values',async()=>{
 const db=recorder();await imdbMethod(db,'titleCatalog',[{genre:"Drama' OR 1=1--",notGenre:['Animation']},-2,900]);
 assert.match(db.calls[0].sql,/json_each\(t.__membership\) g/);assert.equal(db.calls[0].args[0],"Drama' OR 1=1--");assert.deepEqual(db.calls[0].args.slice(-2),[100,0]);assert.equal(db.calls[0].sql.includes("Drama'"),false);
});
test('MAL Bayesian mean is computed over same materialized scoped duplicate population',async()=>{
 const db=recorder();await malMethod(db,'discoverPageFiltered',['top-rated',[{axis:'genre',key:'romance'},{axis:'demographic',key:'seinen'}],false,0,24]);
 assert.match(db.calls[0].sql,/WITH scoped AS MATERIALIZED/);assert.match(db.calls[0].sql,/AVG\(score\).*FROM scoped/);assert.match(db.calls[0].sql,/m.explicit=0/);assert.match(db.calls[0].sql,/EXISTS\(SELECT 1 FROM json_each/);assert.deepEqual(db.calls[0].args,['genre','romance','demographic','seinen',24,0]);
});
test('MAL genre score confidence and direct limit preserve native behavior',async()=>{
 const db=recorder();await malMethod(db,'genreEntries',['anime','Action','score',2000]);assert.match(db.calls[0].sql,/scored_by>=5000/);assert.equal(db.calls[0].args[1],2000);
});
test('Identity uses already normalized native title and searches each requested medium',async()=>{
 const db=recorder();await malMethod(db,'search',['one piece',20,'']);assert.equal(db.calls.length,2);assert.deepEqual(db.calls[0].args.slice(-2),['one piece',20]);assert.match(db.calls[0].sql,/lookup_mal_anime_prefix/);
});
test('Trending and unknown catalogs return exact empty page envelope',async()=>{
 const db=recorder();assert.deepEqual(await malMethod(db,'discoverPageFiltered',['unknown',[],false,4,24]),{items:[],nextOffset:4,exhausted:true,freshness:'bundled',fallbackCatalog:''});const r=await malMethod(db,'discoverPageFiltered',['trending',[],false,0,24]);assert.equal(r.fallbackCatalog,'popular');
});
