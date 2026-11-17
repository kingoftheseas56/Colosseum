// CC0 AudioSilo projection. This module is the sole D1 semantic SQL owner.
import {query, text, integer, clamp, type Row, type CatalogueDb} from './catalogue-semantic.ts';

const id = (v:any) => text(v).trim().slice(0,256);
const page = (v:any) => Math.min(100000, Math.max(0, integer(v)));
const workRow = (r:Row) => ({
  id:text(r.id), title:text(r.title), sortTitle:text(r.sort_title),
  originalLanguage:text(r.original_language), firstYear:r.first_year==null?null:integer(r.first_year)
});
const personRow = (r:Row) => ({id:text(r.id),name:text(r.name)});
const recordingRow = (r:Row) => ({
  id:text(r.id),workId:text(r.work_id),language:text(r.language),
  runtimeMin:r.runtime_min==null?null:integer(r.runtime_min),
  releaseYear:r.release_year==null?null:integer(r.release_year),
  abridgement:r.abridgement==null?null:integer(r.abridgement),
  asin:text(r.asin),isbn:text(r.isbn)
});
const seriesRow = (r:Row) => ({
  id:text(r.id),name:text(r.name),numberText:text(r.number_text),
  positionSort:r.position_sort==null?null:Number(r.position_sort)
});
const listPage = (v:any) => page(v)*24;
async function narrators(db:CatalogueDb,r:Row) {
  return (await query(db,'SELECT p.id,p.name FROM recording_narrators n JOIN people p ON p.id=n.person_id WHERE n.work_id=? AND n.recording_id=? ORDER BY n.ord,p.id',[r.work_id,r.id])).map(personRow);
}
async function recording(db:CatalogueDb,r:Row) {
  const value:Row={...recordingRow(r),narrators:await narrators(db,r)};
  value.asins=(await query(db,'SELECT asin,region FROM recording_asins WHERE work_id=? AND recording_id=? ORDER BY asin,region',[r.work_id,r.id]))
    .map(a=>({asin:text(a.asin),region:text(a.region)}));
  value.isbns=(await query(db,'SELECT isbn FROM recording_isbns WHERE work_id=? AND recording_id=? ORDER BY isbn',[r.work_id,r.id]))
    .map(a=>text(a.isbn));
  return value;
}
async function work(db:CatalogueDb,key:string):Promise<Row>{
  if(!key)return {};
  const [w]=await query(db,'SELECT * FROM works WHERE id=?',[key]);
  if(!w)return {};
  const value:Row=workRow(w);
  value.authors=(await query(db,'SELECT p.id,p.name FROM work_authors a JOIN people p ON p.id=a.person_id WHERE a.work_id=? ORDER BY a.ord,p.id',[key])).map(personRow);
  value.series=(await query(db,'SELECT s.id,s.name,ws.number_text,ws.position_sort FROM work_series ws JOIN series s ON s.id=ws.series_id WHERE ws.work_id=? ORDER BY s.name COLLATE NOCASE,s.id',[key])).map(seriesRow);
  const records=await query(db,'SELECT * FROM recordings WHERE work_id=? ORDER BY release_year DESC,id',[key]);
  value.recordings=[];
  for(const r of records)value.recordings.push(await recording(db,r));
  return value;
}
async function recordingBy(db:CatalogueDb,kind:'asin'|'isbn',key:string):Promise<Row>{
  if(!key)return {};
  const table=kind==='asin'?'recording_asins':'recording_isbns';
  const matches=await query(db,'SELECT DISTINCT r.* FROM '+table+' x JOIN recordings r ON r.work_id=x.work_id AND r.id=x.recording_id WHERE x.'+kind+'=? ORDER BY r.work_id,r.id LIMIT 2',[key]);
  // Conflicting identifiers must not silently choose a recording.
  return matches.length===1?recording(db,matches[0]):{};
}
async function series(db:CatalogueDb,key:string):Promise<Row>{
  if(!key)return {};
  const [s]=await query(db,'SELECT id,name FROM series WHERE id=?',[key]);
  if(!s)return {};
  const entries=await query(db,'SELECT w.id,w.title,ws.number_text,ws.position_sort FROM work_series ws JOIN works w ON w.id=ws.work_id WHERE ws.series_id=? ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id',[key]);
  return {id:text(s.id),name:text(s.name),works:entries.map(e=>({
    id:text(e.id),title:text(e.title),numberText:text(e.number_text),
    positionSort:e.position_sort==null?null:Number(e.position_sort)
  }))};
}
async function latest(db:CatalogueDb,offset:number){
  const rows=await query(db,'SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id ORDER BY (r.release_year IS NULL),r.release_year DESC,r.work_id,r.id LIMIT 24 OFFSET ?',[offset]);
  return rows.map(r=>({...recordingRow(r),title:text(r.title)}));
}
async function byPerson(db:CatalogueDb,key:string,offset:number){
  if(!key)return [];
  const rows=await query(db,'SELECT DISTINCT w.* FROM works w WHERE EXISTS(SELECT 1 FROM work_authors a WHERE a.work_id=w.id AND a.person_id=?) OR EXISTS(SELECT 1 FROM recording_narrators n WHERE n.work_id=w.id AND n.person_id=?) ORDER BY (w.first_year IS NULL),w.first_year DESC,w.title COLLATE NOCASE,w.id LIMIT 24 OFFSET ?',[key,key,offset]);
  return rows.map(workRow);
}
export const audiobookMethods = ['search','work','recordingByAsin','recordingByIsbn','series','latest','byPerson'];
export async function audiobooksMethod(db:CatalogueDb,method:string,args:any[]):Promise<any>{
  if(method==='search'){
    const words=text(args[0]).normalize('NFKC').match(/[\p{L}\p{N}]+/gu)?.slice(0,8)??[];
    if(!words.length)return [];
    const token=words.map(w=>'"'+w.slice(0,96)+'"*').join(' ');
    const result=await query(db,'SELECT w.* FROM search_fts JOIN works w ON w.id=search_fts.id WHERE search_fts MATCH ? AND search_fts.kind=\'work\' ORDER BY bm25(search_fts),w.title COLLATE NOCASE,w.id LIMIT ?',[token,clamp(args[1],24)]);
    return result.map(workRow);
  }
  if(method==='work')return work(db,id(args[0]));
  if(method==='recordingByAsin')return recordingBy(db,'asin',id(args[0]).toUpperCase());
  if(method==='recordingByIsbn')return recordingBy(db,'isbn',id(args[0]));
  if(method==='series')return series(db,id(args[0]));
  if(method==='latest')return latest(db,listPage(args[0]));
  if(method==='byPerson')return byPerson(db,id(args[0]),listPage(args[1]));
  throw new Error('UNKNOWN_CATALOGUE_METHOD');
}
