import {indexedRows} from './catalogue-lookup.ts';
import {comicsMethod} from './catalogue-comics.ts';
// Semantic RPC queries run in D1; callers cannot supply SQL or table names.
export type CatalogueDb = D1Database;
export type Row = Record<string, any>;
export async function query(db: CatalogueDb, sql: string, args: any[] = []): Promise<Row[]> {
  const result = await db.prepare(sql).bind(...args).all<Row>();
  if (!result.success) throw new Error('CATALOGUE_QUERY_FAILED');
  return result.results;
}
export const text = (v: any) => v == null ? '' : String(v);
export const integer = (v: any) => Number.isFinite(Number(v)) ? Math.trunc(Number(v)) : 0;
export const number = (v: any) => Number.isFinite(Number(v)) ? Number(v) : 0;
export const clamp = (v: any, fallback = 24) => Math.min(100, Math.max(1, v === undefined ? fallback : integer(v)));
export function array(v: any): any[] { try { const a = JSON.parse(text(v)); return Array.isArray(a) ? a : []; } catch { return []; } }
export const named = (v: any) => array(v).map(v => ({name: typeof v === 'string' ? v : ''}));
export function imdbRow(r: Row, identity = false): Row {
  const out: Row = {};
  for (const key of ['tt','type','title','origLang']) out[key] = text(r[key]);
  for (const key of ['year','endYear','runtimeMin','votes','episodes']) out[key] = integer(r[key]);
  out.rating = number(r.rating); out.genres = array(r.genres).map(v => typeof v === 'string' ? v : ''); out.isAnime = integer(r.isAnime) !== 0;
  if (identity) out.synopsis = '';
  return out;
}
export async function imdbMethod(db: CatalogueDb, method: string, args: any[]): Promise<any> {
  if (method === 'titleCatalog') {
    const q = args[0] && typeof args[0] === 'object' && !Array.isArray(args[0]) ? args[0] : {};
    const allowed = ['type','order','ratingMin','votesMin','votesMax','yearFrom','yearTo','runtimeMax','genre','lang','notLang','excludeAnime','episodesMin','notGenre'];
    if (Object.keys(q).some(k => !allowed.includes(k))) return [];
    const orders: Row = {votes:'t.votes DESC',rating:'t.rating DESC,t.votes DESC',year:'t.year DESC,t.votes DESC',episodes:'t.episodes DESC,t.votes DESC'};
    const order = orders[text(q.order) || 'votes']; if (!order) return [];
    const where: string[] = [], binds: any[] = []; let from = 'den_imdb_title t';
    const typ = text(q.type); if (!['','movie','series','mini'].includes(typ)) return [];
    if (typ) {where.push(typ === 'series' ? "t.type IN ('series','mini')" : 't.type=?'); if (typ !== 'series') binds.push(typ);}
    if ('genre' in q) {from += ',json_each(t.__membership) g';where.push('g.value=?');binds.push(text(q.genre));}
    const clauses: Row = {ratingMin:'t.rating>=?',votesMin:'t.votes>=?',votesMax:'t.votes<=?',yearFrom:'t.year>=?',yearTo:'t.year<=? AND t.year>0',runtimeMax:'t.runtimeMin<=? AND t.runtimeMin>0',lang:'t.origLang=?',notLang:"t.origLang!=? AND t.origLang!=''",episodesMin:'t.episodes>=?'};
    for (const [key, clause] of Object.entries(clauses)) if (key in q) {where.push(clause as string);binds.push(['lang','notLang'].includes(key)?text(q[key]):number(q[key]));}
    if (q.excludeAnime) where.push('t.isAnime=0');
    if (Array.isArray(q.notGenre) && q.notGenre.length) {where.push('NOT EXISTS(SELECT 1 FROM json_each(t.__membership) ng WHERE ng.value IN ('+q.notGenre.map(()=>'?').join(',')+'))');binds.push(...q.notGenre.map(text));}
    binds.push(clamp(args[2],100),Math.max(0,integer(args[1])));
    return (await query(db,'SELECT t.* FROM '+from+(where.length?' WHERE '+where.join(' AND '):'')+' ORDER BY '+order+' LIMIT ? OFFSET ?',binds)).map(r=>imdbRow(r));
  }
  if (method === 'search' || method === 'matchByTitle') {
    // Native VaultKit performs the authoritative normalization before transport.
    const norm = text(args[0]); if (!norm) return [];
    const match = method === 'matchByTitle';
    const sql = match ? 'norm_title=? AND (?=0 OR year=?) ORDER BY year DESC,rating DESC,votes DESC,tt ASC' : 'norm_title LIKE ? ORDER BY CASE WHEN norm_title=? THEN 0 ELSE 1 END,year DESC,rating DESC,votes DESC,tt ASC LIMIT ?';
    const indexed=await indexedRows(db,'imdb','title',match?'norm':'prefix',norm,match?'AND (?=0 OR h.year=?) ORDER BY h.year DESC,h.rating DESC,h.votes DESC,h.tt ASC':'ORDER BY CASE WHEN h.norm_title=? THEN 0 ELSE 1 END,h.year DESC,h.rating DESC,h.votes DESC,h.tt ASC LIMIT ?',match?[integer(args[1]),integer(args[1])]:[norm,clamp(args[1],100)]);
    return (indexed??await query(db,'SELECT * FROM lib_imdb_title WHERE '+sql,match?[norm,integer(args[1]),integer(args[1])]:[norm+'%',norm,clamp(args[1],100)])).map(r=>imdbRow(r,true));
  }
  if (method === 'titleFacts') {
    const ids = Array.isArray(args[0]) ? args[0].map(text) : [], out: Row = {};
    for (let i=0;i<ids.length;i+=100) for (const r of await query(db,'SELECT tt,rating,votes,isAnime FROM lib_imdb_title WHERE tt IN (SELECT value FROM json_each(?))',[JSON.stringify(ids.slice(i,i+100))])) out[text(r.tt)]={rating:number(r.rating),votes:integer(r.votes),isAnime:integer(r.isAnime)!==0};
    return out;
  }
  if (method === 'titlePresentation') {
    const id=text(args[0]); if (!id.startsWith('tt')) return {};
    const [r]=(await indexedRows(db,'imdb','title','key',id))??await query(db,'SELECT title,type FROM lib_imdb_title WHERE tt=?',[id]);return r?{title:text(r.title),type:text(r.type)}:{};
  }
  throw new Error('UNKNOWN_CATALOGUE_METHOD');
}

export function malRow(r: Row, medium: string, identity = false, discover = false): Row {
  const m: Row={mal_id:integer(r.mal_id),title:text(r.title),type:text(r.type)};
  if (text(r.title_english)) m.title_english=text(r.title_english);
  if (r.score != null) m.score=number(r.score);
  const year=integer(r.year); if(year>0)m.year=year;
  if (identity) {m.coverUrl=text(r.cover);m.synopsis=text(r.synopsis);m.credits=named(r.credits);m.genres=named(r.tags);m.medium=medium;}
  else {m.scored_by=integer(r.scored_by);m.members=integer(r.members);}
  if(discover) {m.favorites=integer(r.favorites);m.start_date=text(r.start_date);m.cover=text(r.cover);m.classifications=array(r.tags);m.explicit=integer(r.explicit)!==0;m.availability=false;return m;}
  if(medium==='anime')m.episodes=integer(r.episodes);else {m.volumes=integer(r.volumes);m.chapters=integer(r.chapters);}
  if(!identity){m.status=text(r.status);if(year>0)m.published={prop:{from:{year}}};m.images={jpg:{large_image_url:text(r.cover)}};m.synopsis=text(r.synopsis);m[medium==='anime'?'studios':'authors']=named(r.credits);m.genres=named(r.tags);}
  return m;
}
export async function malMethod(db: CatalogueDb, method: string, args: any[]): Promise<any> {
 const valid=(m:string)=>m==='anime'||m==='manga';
 if(method==='genreCount'||method==='genreCounts'){
  const medium=text(args[0]);if(!valid(medium))return method==='genreCount'?0:[];
  if(method==='genreCount'){const[r]=await query(db,'SELECT total FROM lib_mal_tag_count WHERE medium=? AND tag=?',[medium,text(args[1])]);return integer(r?.total);}
  return(await query(db,'SELECT tag,total FROM lib_mal_tag_count WHERE medium=? ORDER BY total DESC',[medium])).map(r=>({name:text(r.tag),count:integer(r.total)}));
 }
 if(method==='genreEntries'){
  const medium=text(args[0]),genre=text(args[1]);if(!valid(medium)||!genre)return [];
  const rank=text(args[2])==='score'?'r.scored_by>=5000 ORDER BY r.score DESC':'1=1 ORDER BY r.members DESC';
  return(await query(db,'SELECT r.* FROM den_mal_'+medium+' r,json_each(r.__tags) t WHERE t.value=? AND '+rank+' LIMIT ?',[genre,args[3]===undefined?24:integer(args[3])])).map(r=>malRow(r,medium));
 }
 if(method==='animeCatalog'){
  const q=args[0]&&typeof args[0]==='object'&&!Array.isArray(args[0])?args[0]:{};
  const allowed=['order','status','type','tag','yearFrom','yearTo','voteFloor','membersMin','membersMax'];if(Object.keys(q).some(k=>!allowed.includes(k)))return [];
  const orders:Row={members:'r.members DESC',score:'r.score DESC',year:'r.year DESC'};const order=orders[text(q.order)||'members'];if(!order)return [];
  const where:string[]=[],binds:any[]=[];let from='den_mal_anime r';
  if('tag'in q){from+=',json_each(r.__tags) t';where.push('t.value=?');binds.push(text(q.tag));}
  const clauses:Row={status:'r.status=?',type:'r.type=?',yearFrom:'r.year>=?',yearTo:'r.year<=? AND r.year>0',voteFloor:'r.scored_by>=?',membersMin:'r.members>=?',membersMax:'r.members<=?'};
  for(const[key,clause]of Object.entries(clauses))if(key in q){where.push(clause as string);binds.push(['status','type'].includes(key)?text(q[key]):integer(q[key]));}
  binds.push(clamp(args[2]),Math.max(0,integer(args[1])));
  return(await query(db,'SELECT r.* FROM '+from+(where.length?' WHERE '+where.join(' AND '):'')+' ORDER BY '+order+' LIMIT ? OFFSET ?',binds)).map(r=>malRow(r,'anime'));
 }
 if(method==='mangaById'){const id=integer(args[0]);if(id<=0)return{};const[r]=(await indexedRows(db,'mal','manga','key',id))??await query(db,'SELECT * FROM lib_mal_manga WHERE mal_id=?',[id]);return r?malRow(r,'manga'):{};}
 if(method==='matchByTitle'||method==='search'){
  const norm=text(args[0]),medium=text(args[2]);if(!norm||(medium&&!valid(medium)))return[];const out:Row[]=[];
  for(const table of medium?[medium]:['anime','manga']){
   const match=method==='matchByTitle';
   const sql=match?'(norm_title=? OR norm_title_english=?) AND (?=0 OR year=?) ORDER BY year DESC,score DESC,mal_id ASC':'norm_title LIKE ? OR norm_title_english LIKE ? ORDER BY CASE WHEN norm_title=? THEN 0 ELSE 1 END,year DESC,score DESC,mal_id ASC LIMIT ?';
   const binds=match?[norm,norm,integer(args[1]),integer(args[1])]:[norm+'%',norm+'%',norm,clamp(args[1],20)];
   const indexed=await indexedRows(db,'mal',table,match?'norm':'prefix',norm,match?'AND (?=0 OR h.year=?) ORDER BY h.year DESC,h.score DESC,h.mal_id ASC':'ORDER BY CASE WHEN h.norm_title=? THEN 0 ELSE 1 END,h.year DESC,h.score DESC,h.mal_id ASC LIMIT ?',match?[integer(args[1]),integer(args[1])]:[norm,clamp(args[1],20)]);
   out.push(...(indexed??await query(db,'SELECT * FROM lib_mal_'+table+' WHERE '+sql,binds)).map(r=>malRow(r,table,true)));
  }return out;
 }
 if(method==='discoverFilters'||method==='cachedDiscoverFilters'){
  const axis=text(args[0]);if(!['genre','demographic'].includes(axis))return[];
  const sql="SELECT json_extract(c.value,'$[1]') AS value,COUNT(*) AS total FROM den_mal_manga m,json_each(m.__membership) c WHERE json_extract(c.value,'$[0]')=?"+(!args[1]?' AND m.explicit=0':'')+' GROUP BY value ORDER BY total DESC,value ASC';
  return(await query(db,sql,[axis])).map(r=>({value:text(r.value),count:integer(r.total)}));
 }
 if(method==='discoverPage'||method==='discoverPageFiltered'){
  let[catalog,filters,explicit,offset,limit]=args;
  if(method==='discoverPage'){catalog=args[0];filters=args[1]||args[2]?[{axis:args[1],key:args[2]}]:[];explicit=args[3];offset=args[4];limit=args[5];}
  catalog=text(catalog);const off=Math.max(0,integer(offset)),lim=clamp(limit);
  const pack=(items:Row[])=>({items,nextOffset:off+items.length,exhausted:items.length<lim,freshness:'bundled',fallbackCatalog:catalog==='trending'?'popular':''});
  if(!['popular','trending','top-rated','new-releases'].includes(catalog))return pack([]);
  const facets:{axis:string,key:string}[]=[];
  for(const f of Array.isArray(filters)?filters:[]){const axis=text(f?.axis),key=text(f?.key);if(!['','genre','demographic'].includes(axis))return pack([]);if(axis&&key)facets.push({axis,key});}
  let from='den_mal_manga m';const where:string[]=[],binds:any[]=[];
  if(facets.length){from+=',json_each(m.__membership) c';where.push("json_extract(c.value,'$[0]')=? AND LOWER(json_extract(c.value,'$[1]'))=LOWER(?)");binds.push(facets[0].axis,facets[0].key);}
  for(const f of facets.slice(1)){where.push("EXISTS(SELECT 1 FROM json_each(m.__membership) c2 WHERE json_extract(c2.value,'$[0]')=? AND LOWER(json_extract(c2.value,'$[1]'))=LOWER(?))");binds.push(f.axis,f.key);}
  if(!explicit)where.push('m.explicit=0');let order='m.members DESC,COALESCE(m.score,0) DESC,m.mal_id ASC';
  if(catalog==='new-releases'){where.push('date(m.start_date) IS NOT NULL AND date(m.start_date)<=date(?)');binds.push(new Date().toISOString().slice(0,10));order='m.start_date DESC,m.mal_id DESC';}
  if(catalog==='top-rated')where.push('m.score IS NOT NULL');
  const scoped='SELECT m.* FROM '+from+(where.length?' WHERE '+where.join(' AND '):'');
  if(catalog==='top-rated')order='((CAST(m.scored_by AS REAL)/(m.scored_by+3000.0))*m.score+(3000.0/(CAST(m.scored_by AS REAL)+3000.0))*(SELECT COALESCE(AVG(score),0) FROM scoped)) DESC,m.mal_id ASC';
  return pack((await query(db,'WITH scoped AS MATERIALIZED ('+scoped+') SELECT m.* FROM scoped m ORDER BY '+order+' LIMIT ? OFFSET ?',[...binds,lim,off])).map(r=>malRow(r,'manga',false,true)));
 }
 throw new Error('UNKNOWN_CATALOGUE_METHOD');
}
export const catalogueMethods:Record<string,string[]>={imdb:['titleCatalog','matchByTitle','search','titleFacts','titlePresentation'],comics:['series','search','downloadsFor','exactMatches','curatedReady','curatedRanked','curatedSeries','curatedCoverage','curatedByNorm','curatedGenreShelves','curatedHasDownloadable','discoverFilters','discoverPage','discoverPageFiltered','shelf'],mal:['genreEntries','genreCount','genreCounts','animeCatalog','matchByTitle','mangaById','search','discoverFilters','cachedDiscoverFilters','discoverPage','discoverPageFiltered']};
export async function catalogueMethod(db:CatalogueDb,dataset:string,method:string,args:any[]):Promise<any>{
 if(dataset==='comics'&&catalogueMethods.comics.includes(method))return comicsMethod(db,method,args);
 if(dataset==='imdb'&&catalogueMethods.imdb.includes(method))return imdbMethod(db,method,args);
 if(dataset==='mal'&&catalogueMethods.mal.includes(method))return malMethod(db,method,args);
 throw new Error('UNKNOWN_CATALOGUE_METHOD');
}
