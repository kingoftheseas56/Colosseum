import {query,type CatalogueDb,type Row} from './catalogue-semantic.ts';
const allowed=new Set(['imdb_title','mal_anime','mal_manga','comics_series','comics_download','comics_curated_series','comics_curated_edition','comics_curated_source','comics_curated_series_coverage']);
// Directory ranges prune packed pages BEFORE json_each expansion. Root blocks use
// INTEGER PRIMARY KEY lookup; DISTINCT removes dual native/English-name hits.
export function indexedStatement(dataset:string,family:string,mode:'key'|'norm'|'prefix',value:any,tail='',tailArgs:any[]=[]){
 const name=dataset+'_'+family;if(!allowed.has(name))throw Error('UNKNOWN_LOOKUP_FAMILY');
 const kind=mode==='key'?'key':'prefix',block=mode==='key'?1:2,ordinal=block+1;
 let predicate:string,binds:any[];
 if(mode==='prefix') {const end=value+'\uffff';predicate="i.max_key>=? AND i.min_key<? AND json_extract(k.value,'$[0]')>=? AND json_extract(k.value,'$[0]')<?";binds=[value,end,value,end];}
 else {predicate="i.min_key<=? AND i.max_key>=? AND json_extract(k.value,'$[0]')=?";binds=[value,value,value];}
 if(mode==='prefix'){
  const limit=tailArgs.at(-1)??100;
  const cols=[1,2,3,4,5,6,7].map((n,i)=>`json_extract(k.value,'$[${n}]') AS c${i}`).join(',');
  const candidate=`SELECT DISTINCT ${cols} FROM lookup_${name}_${kind} i CROSS JOIN json_each(i.payload) k WHERE ${predicate} ORDER BY CASE WHEN c3=? THEN 0 ELSE 1 END,c4 DESC,c5 DESC,c6 DESC,c0 ASC LIMIT ?`;
  return{sql:`WITH candidates AS MATERIALIZED (${candidate}) SELECT h.* FROM candidates c CROSS JOIN hit_${name} h WHERE h.__block=c.c1 AND h.__offset=c.c2 ${tail}`,args:[...binds,value,limit,...tailArgs]};
 }
 return {sql:`SELECT DISTINCT h.* FROM lookup_${name}_${kind} i CROSS JOIN json_each(i.payload) k CROSS JOIN hit_${name} h WHERE ${predicate} AND h.__block=json_extract(k.value,'$[${block}]') AND h.__offset=json_extract(k.value,'$[${ordinal}]') ${tail}`,args:[...binds,...tailArgs]};
}
export async function indexedRows(db:CatalogueDb,dataset:string,family:string,mode:'key'|'norm'|'prefix',value:any,tail='',args:any[]=[]):Promise<Row[]|null>{
 const s=indexedStatement(dataset,family,mode,value,tail,args);
 try{return await query(db,s.sql,s.args);}catch(e){if(String(e).includes('no such table'))return null;throw e;}
}
