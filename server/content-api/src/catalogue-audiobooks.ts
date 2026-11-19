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
  asin:text(r.asin),isbn:text(r.isbn),
  ...(r.release_date?{releaseDate:text(r.release_date)}:{}),
  ...(r.publisher?{publisher:text(r.publisher)}:{}),
  ...(r.cover_url?{coverUrl:text(r.cover_url)}:{}),
  ...(r.added_at?{addedAt:text(r.added_at)}:{}),
  dramatized:integer(r.dramatized)!==0,
  dramatizedSignals:JSON.parse(text(r.dramatized_signals)||'[]')
});
const seriesRow = (r:Row) => ({
  id:text(r.id),name:text(r.name),numberText:text(r.number_text),
  positionSort:r.position_sort==null?null:Number(r.position_sort)
});
const listPage = (v:any) => page(v)*24;
// Match AudiobookCatalog.cpp. Discovery has one language policy; direct lookups
// and released search/person listings intentionally allow any language.
const discoveryLanguages = "'en','eng','english'";
const released = (alias: string) => `${alias}.release_date<=date('now')
  AND date(${alias}.release_date,'+0 days')=${alias}.release_date`;
const discoverable = (alias: string) => `${released(alias)}
  AND lower(trim(${alias}.language)) IN (${discoveryLanguages})`;
const usableCover = "COALESCE(r.cover_url LIKE 'https://_%' OR r.cover_url LIKE 'http://_%',0)";
const releaseOrder = 'r.release_date DESC,r.added_at DESC,r.work_id,r.id';
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
  const entries=await query(db,`
    SELECT w.id,w.title,ws.number_text,ws.position_sort,
      (SELECT r.cover_url FROM recordings r WHERE r.work_id=w.id
       AND ${discoverable('r')} AND ${usableCover} ORDER BY ${releaseOrder} LIMIT 1) AS cover_url
    FROM work_series ws JOIN works w ON w.id=ws.work_id
    WHERE ws.series_id=? AND EXISTS (
      SELECT 1 FROM recordings r WHERE r.work_id=w.id AND ${discoverable('r')})
    ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id`,[key]);
  return {id:text(s.id),name:text(s.name),works:entries.map(e=>({
    id:text(e.id),title:text(e.title),numberText:text(e.number_text),
    positionSort:e.position_sort==null?null:Number(e.position_sort),
    ...(e.cover_url?{coverUrl:text(e.cover_url)}:{})
  }))};
}
async function latest(db:CatalogueDb,offset:number){
  const rows=await query(db,`
    SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id
    WHERE ${released('r')} ORDER BY r.release_year DESC,r.work_id,r.id LIMIT 24 OFFSET ?`,[offset]);
  return rows.map(r=>({...recordingRow(r),title:text(r.title)}));
}
async function byPerson(db:CatalogueDb,key:string,offset:number){
  if(!key)return [];
  const rows=await query(db,`
    SELECT w.* FROM works w JOIN (
      SELECT a.work_id FROM work_authors a WHERE a.person_id=? AND EXISTS (
        SELECT 1 FROM recordings r WHERE r.work_id=a.work_id AND ${released('r')})
      UNION
      SELECT n.work_id FROM recording_narrators n JOIN recordings r
        ON r.work_id=n.work_id AND r.id=n.recording_id
      WHERE n.person_id=? AND ${released('r')}
    ) credited ON credited.work_id=w.id
    ORDER BY (w.first_year IS NULL),w.first_year DESC,w.title COLLATE NOCASE,w.id LIMIT 24 OFFSET ?`,
    [key,key,offset]);
  return rows.map(workRow);
}

const ROW_SIZE=20;
const itemSql=`
 SELECT r.work_id AS work_id,r.id AS recording_id,w.title,
        r.cover_url,r.runtime_min,r.release_date,r.dramatized,
        (SELECT json_group_array(name) FROM (
          SELECT p.name AS name FROM work_authors a JOIN people p ON p.id=a.person_id
          WHERE a.work_id=r.work_id ORDER BY a.ord,p.id)) AS authors_json,
        (SELECT json_group_array(name) FROM (
          SELECT p.name AS name FROM recording_narrators n JOIN people p ON p.id=n.person_id
          WHERE n.work_id=r.work_id AND n.recording_id=r.id ORDER BY n.ord,p.id)) AS narrators_json,
        (SELECT CASE WHEN COUNT(*)=1 THEN MAX(s.name) END
         FROM work_series ws JOIN series s ON s.id=ws.series_id
         WHERE ws.work_id=r.work_id) AS series_name,
        (SELECT CASE WHEN COUNT(*)=1 THEN MAX(ws.number_text) END
         FROM work_series ws WHERE ws.work_id=r.work_id) AS series_number
 FROM recordings r JOIN works w ON w.id=r.work_id`;
function item(r:Row):Row {
  const value:Row={
    workId:text(r.work_id),recordingId:text(r.recording_id),title:text(r.title),
    dramatized:integer(r.dramatized)!==0
  };
  const authors=JSON.parse(text(r.authors_json)||'[]'),narrators=JSON.parse(text(r.narrators_json)||'[]');
  if(authors.length)value.authors=authors;
  if(narrators.length)value.narrators=narrators;
  if(typeof r.cover_url==='string' && /^https?:\/\/.+/i.test(r.cover_url))value.coverUrl=r.cover_url;
  if(r.runtime_min!=null && integer(r.runtime_min)>0)value.runtimeMin=integer(r.runtime_min);
  if(typeof r.release_date==='string' && /^\d{4}-\d{2}-\d{2}$/.test(r.release_date))value.releaseDate=r.release_date;
  if(r.series_name)value.seriesName=text(r.series_name);
  if(r.series_number)value.seriesNumber=text(r.series_number);
  return value;
}
async function pageItems(db:CatalogueDb,where:string,binds:any[],number:number):Promise<Row> {
  const n=page(number);
  const offset=n*ROW_SIZE;
  const eligible=` WHERE ${where} AND ${discoverable('r')}`;
  const count=ROW_SIZE+1;
  // Each cover group is date-ordered independently, so SQLite can stop on the
  // release-date index when the covered group supplies the page.
  const rows=await query(db,`${itemSql}${eligible} AND ${usableCover}
    ORDER BY ${releaseOrder} LIMIT ? OFFSET ?`,[...binds,count,offset]);
  if(rows.length<count){
    let uncoveredOffset=0;
    if(offset>0 && !rows.length){
      const [covered]=await query(db,`SELECT count(*) AS n FROM recordings r
        ${eligible} AND ${usableCover}`,binds);
      uncoveredOffset=offset-integer(covered.n);
    }
    rows.push(...await query(db,`${itemSql}${eligible} AND NOT ${usableCover}
      ORDER BY ${releaseOrder} LIMIT ? OFFSET ?`,[...binds,count-rows.length,uncoveredOffset]));
  }
  return {items:rows.slice(0,ROW_SIZE).map(item),page:n,hasMore:rows.length>ROW_SIZE};
}
const emptyPage=(v:any)=>({items:[],page:page(v),hasMore:false});
async function shelves(db:CatalogueDb) {
  const narrators=await query(db,'SELECT person_id,name FROM narrator_shelves ORDER BY rank');
  return [{key:'new-releases',title:'New releases'},{key:'dramatized',title:'Fully dramatized'},
    ...narrators.map(n=>({key:'narrator:'+text(n.person_id),title:'Narrated by '+text(n.name)}))];
}
async function shelf(db:CatalogueDb,key:string,p:any) {
  if(key==='new-releases')return pageItems(db,'1',[],p);
  if(key==='dramatized')return pageItems(db,'r.dramatized=1',[],p);
  if(key.startsWith('narrator:') && key.length>9){
    const who=key.slice(9);
    const known=await query(db,'SELECT person_id FROM narrator_shelves WHERE person_id=?',[who]);
    if(!known.length)return emptyPage(p);
    return pageItems(db,`(r.librivox_id IS NULL OR r.librivox_id='') AND r.rowid IN
      (SELECT nr.rowid FROM recording_narrators n JOIN recordings nr
       ON nr.work_id=n.work_id AND nr.id=n.recording_id WHERE n.person_id=?)`,[who],p);
  }
  return emptyPage(p);
}
async function genres(db:CatalogueDb) {
  // Count the supplied bake in work order. Rank recent distinct URLs first;
  // only genres that still need covers look farther back into the catalogue.
  const rows=await query(db,`
    WITH eligible_counts AS MATERIALIZED (
      SELECT g.genre,COUNT(*) AS n
      FROM recordings r NOT INDEXED CROSS JOIN work_genres g USING(work_id)
      WHERE ${discoverable('r')} GROUP BY g.genre HAVING COUNT(*)>=4
    ), recent_recordings AS MATERIALIZED (
      SELECT r.work_id,r.cover_url,r.release_date FROM recordings r
      WHERE ${discoverable('r')} AND ${usableCover}
      ORDER BY r.release_date DESC,r.work_id,r.id LIMIT 4096
    ), recent_covers AS MATERIALIZED (
      SELECT g.genre,r.cover_url AS url,MAX(r.release_date) AS release_date
      FROM recent_recordings r JOIN work_genres g USING(work_id) GROUP BY g.genre,r.cover_url
    ), sparse_genres AS MATERIALIZED (
      SELECT c.genre FROM eligible_counts c
      LEFT JOIN (SELECT genre FROM recent_covers GROUP BY genre HAVING COUNT(*)>=4) plentiful USING(genre)
      WHERE plentiful.genre IS NULL
    ), ranked AS (
      SELECT genre,url,ROW_NUMBER() OVER (PARTITION BY genre ORDER BY release_date DESC,url) AS position
      FROM (
        SELECT genre,url,release_date FROM recent_covers WHERE genre NOT IN (SELECT genre FROM sparse_genres)
        UNION ALL
        SELECT s.genre,r.cover_url,MAX(r.release_date)
        FROM sparse_genres s CROSS JOIN work_genres g ON g.genre=s.genre
        CROSS JOIN recordings r ON r.work_id=g.work_id
        WHERE ${discoverable('r')} AND ${usableCover} GROUP BY s.genre,r.cover_url
      )
    )
    SELECT genre,MAX(n) AS recording_count,json_group_array(url) AS covers_json
    FROM (SELECT genre,url,n FROM ranked JOIN eligible_counts USING(genre) WHERE position<=4 ORDER BY genre,position)
    GROUP BY genre HAVING COUNT(*)=4 ORDER BY recording_count DESC,genre`);
  return rows.map(r=>({genre:text(r.genre),count:integer(r.recording_count),covers:JSON.parse(text(r.covers_json))}));
}
async function genre(db:CatalogueDb,key:string,p:any) {
  if(!key)return emptyPage(p);
  // A short release-index scan can fill the first page for high-volume genres.
  // Small or unknown genres should read only their matching work IDs.
  if(page(p)===0){
    const [summary]=await query(db,'SELECT recording_count FROM genres_summary WHERE genre=?',[key]);
    if(integer(summary?.recording_count)>=1024)
      return pageItems(db,'EXISTS (SELECT 1 FROM work_genres g WHERE g.work_id=r.work_id AND g.genre=?)',[key],p);
  }
  return pageItems(db,'r.work_id IN (SELECT work_id FROM work_genres WHERE genre=?)',[key],p);
}

export const audiobookMethods = ['search','work','recordingByAsin','recordingByIsbn','series','latest','byPerson','shelves','shelf','genres','genre'];
export async function audiobooksMethod(db:CatalogueDb,method:string,args:any[]):Promise<any>{
  if(method==='search'){
    const input=text(args[0]);
    const words=input.normalize('NFKC').match(/[\p{L}\p{N}]+/gu)?.slice(0,8)??[];
    if(!words.length)return [];
    const token=words.map(w=>'"'+w.slice(0,96)+'"*').join(' ');
    const limit=clamp(args[1],24);
    const slug=input.normalize('NFKD').toLowerCase().replace(/[\p{M}'’]+/gu,'')
      .match(/[\p{L}\p{N}]+/gu)?.join('-')??'';
    const articleVariant=slug.startsWith('the-')?slug.slice(4):'the-'+slug;
    const seriesWorks=await query(db,`
      SELECT w.* FROM work_series ws JOIN works w ON w.id=ws.work_id
      WHERE ws.series_id=(SELECT id FROM series WHERE id IN (?,?) ORDER BY id<>? LIMIT 1)
      AND EXISTS (SELECT 1 FROM recordings r WHERE r.work_id=w.id AND ${released('r')})
      ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id LIMIT ?`,
      [slug,articleVariant,slug,limit]);
    if(seriesWorks.length===limit)return seriesWorks.map(workRow);

    const result=await query(db,`
      SELECT w.* FROM search_fts JOIN works w ON w.id=search_fts.id
      WHERE search_fts MATCH ? AND search_fts.kind='work' AND EXISTS (
        SELECT 1 FROM recordings r WHERE r.work_id=w.id AND ${released('r')})
      ORDER BY bm25(search_fts),w.title COLLATE NOCASE,w.id LIMIT ?`,[token,limit]);
    const seen=new Set(seriesWorks.map(row=>row.id));
    return seriesWorks.concat(result.filter(row=>!seen.has(row.id))).slice(0,limit).map(workRow);
  }
  if(method==='work')return work(db,id(args[0]));
  if(method==='recordingByAsin')return recordingBy(db,'asin',id(args[0]).toUpperCase());
  if(method==='recordingByIsbn')return recordingBy(db,'isbn',id(args[0]));
  if(method==='series')return series(db,id(args[0]));
  if(method==='latest')return latest(db,listPage(args[0]));
  if(method==='byPerson')return byPerson(db,id(args[0]),listPage(args[1]));
  if(method==='shelves')return shelves(db);
  if(method==='shelf')return shelf(db,id(args[0]),args[1]);
  if(method==='genres')return genres(db);
  if(method==='genre')return genre(db,id(args[0]),args[1]);
  throw new Error('UNKNOWN_CATALOGUE_METHOD');
}
