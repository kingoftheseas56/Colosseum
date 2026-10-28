import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {imdbMethod,malMethod,imdbRow,malRow} from '../src/catalogue-semantic.ts';
const sourceDir=process.env.CATALOGUE_SOURCE_DIR,artifact=process.env.CATALOGUE_PACKED_DB;
const enabled=!!sourceDir&&!!artifact;
function adapter(db){return {prepare(sql){return{bind(...args){return{async all(){return{success:true,results:db.prepare(sql).all(...args)}}}}}}};}
test('full source IMDb/MAL SQL vs packed semantic methods',{skip:!enabled},async()=>{
 const packed=new DatabaseSync(artifact,{readOnly:true}),original=new DatabaseSync(':memory:');
 for(const dataset of ['imdb','mal'])original.prepare('ATTACH DATABASE ? AS '+dataset).run(sourceDir+'/'+dataset+'_catalog.db');
 original.exec('PRAGMA query_only=ON');const d1=adapter(packed);
 try {
  for(const [args,sql,binds]of [
   [[{},0,25],'SELECT * FROM imdb.title ORDER BY votes DESC LIMIT 25',[]],
   [[{genre:'Drama',ratingMin:6},0,25],'SELECT t.* FROM imdb.genre g JOIN imdb.title t ON t.tt=g.tt WHERE g.genre=? AND t.rating>=? ORDER BY t.votes DESC LIMIT 25',['Drama',6]],
   [[{type:'series',episodesMin:10,order:'episodes'},0,25],"SELECT * FROM imdb.title WHERE type IN ('series','mini') AND episodes>=10 ORDER BY episodes DESC,votes DESC LIMIT 25",[]],
   [[{notGenre:['Animation','Reality-TV'],excludeAnime:true,order:'rating'},0,25],"SELECT * FROM imdb.title WHERE isAnime=0 AND tt NOT IN (SELECT tt FROM imdb.genre WHERE genre IN ('Animation','Reality-TV')) ORDER BY rating DESC,votes DESC LIMIT 25",[]]
  ])assert.deepEqual(await imdbMethod(d1,'titleCatalog',args),original.prepare(sql).all(...binds).map(r=>imdbRow(r)));
  for(const medium of ['anime','manga']){
   const native=original.prepare('SELECT r.* FROM mal.tag t JOIN mal.'+medium+' r ON r.mal_id=t.mal_id WHERE t.medium=? AND t.tag=? AND r.scored_by>=5000 ORDER BY r.score DESC LIMIT 24').all(medium,'Action');
   assert.deepEqual(await malMethod(d1,'genreEntries',[medium,'Action','score',24]),native.map(r=>malRow(r,medium)));
  }
  const scoped="FROM mal.classification c JOIN mal.manga m ON m.mal_id=c.mal_id WHERE c.medium='manga' AND c.axis='genre' AND LOWER(c.value)='romance' AND m.explicit=0";
  for(const catalog of ['popular','trending','top-rated']){
   let where=catalog==='top-rated'?' AND m.score IS NOT NULL':'';let order='m.members DESC,COALESCE(m.score,0) DESC,m.mal_id ASC';let binds=[];
   if(catalog==='top-rated'){const mean=original.prepare('SELECT AVG(m.score) AS mean '+scoped+where).get().mean||0;order='((CAST(m.scored_by AS REAL)/(m.scored_by+?))*m.score+(?/(CAST(m.scored_by AS REAL)+?))*?) DESC,m.mal_id ASC';binds=[3000,3000,3000,mean];}
   const native=original.prepare('SELECT m.* '+scoped+where+' ORDER BY '+order+' LIMIT 24').all(...binds).map(r=>malRow(r,'manga',false,true));
   const actual=await malMethod(d1,'discoverPageFiltered',[catalog,[{axis:'genre',key:'romance'}],false,0,24]);assert.deepEqual(actual.items,native);
  }
  const native=original.prepare("SELECT c.value,COUNT(*) AS total FROM mal.classification c WHERE c.medium='manga' AND c.axis=? AND c.mal_id IN(SELECT mal_id FROM mal.manga WHERE explicit=0) GROUP BY c.value ORDER BY total DESC,c.value ASC").all('genre').map(r=>({value:r.value,count:r.total}));
  assert.deepEqual(await malMethod(d1,'discoverFilters',['genre',false]),native);
 }finally{packed.close();original.close();}
});
