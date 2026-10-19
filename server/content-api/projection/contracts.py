"""Bounded native-query fixtures, not a catalogue store or native ranking replacement.

SQL scalar arrays retain NULL. QVariant/QML conversion and native curated scoring
must still be qualified at the consumer. Explicit selections scope both oracles.
"""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
IMDB_COLUMNS = 'tt type title year endYear runtimeMin genres rating votes episodes origLang isAnime'.split()
MAL_COLUMNS = 'mal_id title title_english type score scored_by members favorites year start_date cover tags explicit'.split()
COMICS_COLUMNS = 'gcd_id title year year_ended publisher cover downloads'.split()
CURATED_COLUMNS = 'locg_id rank title norm_title year publisher cover synopsis genres ed_count avail_count avail_frac max_year availability_state coverage_ratio source_count official_source_count community_source_count latest_source_date'.split()


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False)


def rows_digest(rows):
    normalized = [[int(v) if isinstance(v, float) and v.is_integer() else v for v in row] for row in rows]
    return hashlib.sha256(compact(normalized).encode()).hexdigest()


def _table(dataset, name, projection):
    return 'proj_' + dataset + '_' + name if projection else dataset + '.' + name


def _root(dataset, name, key, roots, projection):
    sql = 'SELECT * FROM ' + _table(dataset, name, projection)
    args = []
    if roots is not None:
        args = list(roots)
        sql += ' WHERE ' + key + ' IN (' + ','.join('?' for _ in args) + ')' if args else ' WHERE 0'
    return 'WITH r AS (' + sql + ') ', args


def _query(sql, args, columns):
    return {'sql': sql, 'args': args, 'columns': columns}


def imdb_query(query, roots=None, prefix=None, projection=False, offset=0, limit=25):
    allowed = {'type','order','ratingMin','votesMin','votesMax','yearFrom','yearTo','runtimeMax','genre','lang','notLang','excludeAnime','episodesMin','notGenre'}
    if set(query) - allowed: raise ValueError('Unknown IMDb filter')
    head, args = _root('imdb','title','tt',roots,projection)
    where = []; order = query.get('order','votes')
    orders = {'votes':'t.votes DESC','rating':'t.rating DESC,t.votes DESC','year':'t.year DESC,t.votes DESC','episodes':'t.episodes DESC,t.votes DESC'}
    if order not in orders: raise ValueError('Unknown IMDb order')
    typ = query.get('type','')
    if typ not in ('','movie','series','mini'): raise ValueError('Unknown IMDb type')
    if typ: where.append("t.type IN ('series','mini')" if typ=='series' else "t.type='"+typ+"'")
    join = ''
    if 'genre' in query:
        join = ' JOIN '+_table('imdb','genre',projection)+' g ON g.tt=t.tt'
        where.append('g.genre=?');args.append(query['genre'])
    clauses = {'ratingMin':'t.rating>=?','votesMin':'t.votes>=?','votesMax':'t.votes<=?','yearFrom':'t.year>=?','yearTo':'t.year<=? AND t.year>0','runtimeMax':'t.runtimeMin<=? AND t.runtimeMin>0','lang':'t.origLang=?','notLang':"t.origLang!=? AND t.origLang!=''",'episodesMin':'t.episodes>=?'}
    for key, clause in clauses.items():
        if key in query: where.append(clause);args.append(query[key])
    if query.get('excludeAnime'): where.append('t.isAnime=0')
    excluded = query.get('notGenre',[])
    if excluded:
        where.append('t.tt NOT IN (SELECT tt FROM '+_table('imdb','genre',projection)+' WHERE genre IN ('+','.join('?' for _ in excluded)+'))');args.extend(excluded)
    order_sql = orders[order]
    if prefix is not None:
        # Caller supplies VaultKit-normalized input; no competing normalizer here.
        where.append('t.norm_title LIKE ?');args.append(prefix+'%')
        order_sql = 'CASE WHEN t.norm_title=? THEN 0 ELSE 1 END,t.year DESC,t.rating DESC,t.votes DESC,t.tt ASC'
        args.append(prefix)
    sql=head+'SELECT '+','.join('t.'+c for c in IMDB_COLUMNS)+' FROM r t'+join
    if where: sql+=' WHERE '+' AND '.join(where)
    args.extend([max(1,min(limit,100)),max(0,offset)])
    return _query(sql+' ORDER BY '+order_sql+' LIMIT ? OFFSET ?',args,IMDB_COLUMNS)


def mal_query(catalog, filters=(), roots=None, include_explicit=False, as_of='2026-10-14', projection=False, offset=0, limit=25):
    if catalog not in ('popular','trending','top-rated','new-releases','mean'): raise ValueError('Unknown MAL catalogue')
    head,args=_root('mal','manga','mal_id',roots,projection)
    clauses=[];join='';active=[]
    for axis,key in filters:
        if axis not in ('','genre','demographic'): raise ValueError('Unknown MAL facet')
        if axis and key: active.append((axis,key))
    classification=_table('mal','classification',projection)
    if active:
        join=' JOIN '+classification+' c ON c.mal_id=m.mal_id'
        clauses.append("c.medium='manga' AND c.axis=? AND LOWER(c.value)=LOWER(?)");args.extend(active[0])
    for axis,key in active[1:]:
        clauses.append("m.mal_id IN (SELECT mal_id FROM "+classification+" WHERE medium='manga' AND axis=? AND LOWER(value)=LOWER(?))");args.extend([axis,key])
    if not include_explicit: clauses.append('m.explicit=0')
    if catalog in ('top-rated','mean'): clauses.append('m.score IS NOT NULL')
    if catalog=='new-releases': clauses.append('date(m.start_date) IS NOT NULL AND date(m.start_date)<=date(?)');args.append(as_of)
    scoped='SELECT m.* FROM r m'+join+(' WHERE '+' AND '.join(clauses) if clauses else '')
    head+=', scoped AS ('+scoped+') '
    if catalog=='mean': return _query(head+'SELECT COALESCE(AVG(score),0) AS mean_score FROM scoped',args,['mean_score'])
    order='members DESC,COALESCE(score,0) DESC,mal_id ASC'
    if catalog=='new-releases': order='start_date DESC,mal_id DESC'
    if catalog=='top-rated':
        order='((CAST(scored_by AS REAL)/(scored_by+3000.0))*score+(3000.0/(CAST(scored_by AS REAL)+3000.0))*(SELECT COALESCE(AVG(score),0) FROM scoped)) DESC,mal_id ASC'
    args.extend([max(1,min(limit,100)),max(0,offset)])
    return _query(head+'SELECT '+','.join(MAL_COLUMNS)+' FROM scoped ORDER BY '+order+' LIMIT ? OFFSET ?',args,MAL_COLUMNS)


def _escape(text): return text.replace('\\','\\\\').replace('%','\\%').replace('_','\\_')


def comics_search_query(text, roots=None, projection=False, limit=30):
    head,args=_root('comics','series','gcd_id',roots,projection)
    words=[];word=''
    for ch in text.strip():
        if unicodedata.category(ch)[0] in ('L','N'): word+=ch
        elif word: words.append(word);word=''
    if word: words.append(word)
    if projection: downloads='COALESCE(s.downloads,0)';join=''
    else: downloads='COALESCE(t.downloads,0)';join=' LEFT JOIN comics.series_stats t ON t.series_id=s.gcd_id'
    sql=head+'SELECT s.gcd_id,s.title,s.year,s.year_ended,s.publisher,s.cover,'+downloads+' AS downloads FROM r s'+join+' WHERE '+(' AND '.join("s.title LIKE ? ESCAPE '\\'" for _ in words) if words else '0')
    args.extend('%'+_escape(w)+'%' for w in words)
    args.extend([text.strip(),_escape(text.strip())+'%',limit])
    return _query(sql+" ORDER BY CASE WHEN LOWER(s.title)=LOWER(?) THEN 0 WHEN s.title LIKE ? ESCAPE '\\' THEN 1 ELSE 2 END,downloads DESC,s.year DESC LIMIT ?",args,COMICS_COLUMNS)


def max_published_year(text):
    return max([0]+[int(y) for y in re.findall(r'(\d{4})',text or '') if 1900<=int(y)<=2100])


def curated_query(filters, roots=None, projection=False, catalog='all'):
    if catalog not in ('all','popular','new-releases','most-stocked','recently-available','complete-runs','near-complete','community-collections'): raise ValueError('Unknown curated catalogue')
    head,args=_root('comics','curated' if projection else 'curated_series','locg_id',roots,projection)
    clauses=[]
    for axis,key in filters:
        if axis not in ('','genre','publisher','format','availability'): raise ValueError('Unknown curated facet')
        if not axis or not key: continue
        if axis=='publisher': clauses.append('LOWER(s.publisher)=LOWER(?)');args.append(key)
        elif axis in ('genre','format'):
            table=_table('comics','curated_genre' if axis=='genre' else ('curated_format' if projection else 'curated_edition'),projection)
            value='genre' if axis=='genre' else 'format'
            clauses.append('EXISTS(SELECT 1 FROM '+table+' f WHERE f.locg_id=s.locg_id AND LOWER('+('trim(f.format)' if axis=='format' else 'f.genre')+')=LOWER(?))');args.append(key)
        elif key=='available': clauses.append('c.official_source_count>0')
        else: clauses.append('LOWER(c.availability_state)=LOWER(?)');args.append(key)
    if catalog=='recently-available': clauses.append("c.official_source_count>0 AND c.latest_source_date!=''")
    elif catalog in ('complete-runs','near-complete'): clauses.append('c.availability_state=?');args.append('complete' if catalog=='complete-runs' else 'near_complete')
    elif catalog=='community-collections': clauses.append('c.community_source_count>0')
    if projection:
        join='';coverage='s';clauses=[c.replace('c.','s.') for c in clauses]
        aggregates='s.genres,s.ed_count,s.avail_count,s.avail_frac,s.max_year'
    else:
        join=' LEFT JOIN comics.curated_series_coverage c ON c.locg_id=s.locg_id';coverage='c'
        aggregates="COALESCE((SELECT group_concat(genre) FROM comics.curated_genre g WHERE g.locg_id=s.locg_id),''),(SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id),(SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id AND available=1 AND getcomics_post!=''),CASE WHEN (SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id)>0 THEN CAST((SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id AND available=1 AND getcomics_post!='') AS REAL)/(SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id) ELSE 0.0 END,COALESCE((SELECT MAX(max_published_year(published)) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id AND published!=''),0)"
    sql=head+'SELECT '+','.join('s.'+c for c in CURATED_COLUMNS[:8])+','+aggregates+','+','.join(coverage+'.'+c for c in CURATED_COLUMNS[13:])+' FROM r s'+join
    if clauses: sql+=' WHERE '+' AND '.join(clauses)
    # This is canonical fact ordering, NOT the consumer's houseScore ranking.
    return _query(sql+' ORDER BY s.norm_title,s.year,s.locg_id',args,CURATED_COLUMNS)


@contextmanager
def original_sources(source_dir):
    source_dir=Path(source_dir);manifest=json.loads((ROOT/'source-manifest.json').read_text())
    paths={k:source_dir/(k+'_catalog.db') for k in manifest['sources']}
    hashes={k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in paths.items()}
    if any(hashes[k]!=manifest['sources'][k]['sha256'] for k in hashes): raise ValueError('Source hash mismatch')
    db=sqlite3.connect(':memory:',uri=True)
    db.create_function('max_published_year',1,max_published_year,deterministic=True)
    try:
        for name,path in paths.items(): db.execute('ATTACH DATABASE ? AS '+name,(path.resolve().as_uri()+'?mode=ro',))
        db.execute('PRAGMA query_only=ON')
        yield db,hashes
    finally:
        db.close()
        if any(hashlib.sha256(p.read_bytes()).hexdigest()!=hashes[k] for k,p in paths.items()): raise ValueError('Source changed during contracts')


def build_contracts(source_dir,selections=None,as_of='2026-10-14'):
    selections={} if selections is None else selections
    if set(selections)-{'imdb','manga','anime','comics','curated'}:
        raise ValueError('Unknown selection family; refusing an accidental full population')
    specs=[]
    def add(id,family,fn,kwargs): specs.append((id,family,fn,kwargs))
    for id,query in [('imdb_filtered',{'type':'movie','genre':'Drama','ratingMin':6,'votesMin':100,'votesMax':1000000,'yearFrom':1980,'yearTo':2026,'runtimeMax':180,'notLang':'ja','excludeAnime':True,'notGenre':['Animation','Reality-TV']}),('imdb_episode_order',{'type':'series','episodesMin':10,'order':'episodes'}),('imdb_rating_order',{'order':'rating'}),('imdb_year_order',{'order':'year'})]:
        add(id,'imdb',imdb_query,{'query':query,'roots':selections.get('imdb')})
    add('imdb_native_prefix','imdb',imdb_query,{'query':{},'prefix':'star','roots':selections.get('imdb')})
    for catalog in ('popular','trending','top-rated','mean','new-releases'):
        add('mal_'+catalog,'mal',mal_query,{'catalog':catalog,'filters':[('genre','romance'),('demographic','seinen')],'roots':selections.get('manga'),'as_of':as_of})
    add('mal_explicit','mal',mal_query,{'catalog':'popular','include_explicit':True,'roots':selections.get('manga')})
    for i,text in enumerate(('batman','amazing spider man','100%_hero')):
        add('comics_words_'+str(i),'comics',comics_search_query,{'text':text,'roots':selections.get('comics')})
    for catalog in ('all','recently-available','complete-runs','near-complete','community-collections'):
        add('curated_'+catalog,'curated',curated_query,{'filters':[],'roots':selections.get('curated'),'catalog':catalog})
    with original_sources(source_dir) as (db,hashes):
        # Choose a real multi-facet key combination rather than an invented empty case.
        row=db.execute("SELECT s.publisher,g.genre,e.format FROM comics.curated_series s JOIN comics.curated_genre g USING(locg_id) JOIN comics.curated_edition e USING(locg_id) WHERE s.publisher!='' AND trim(e.format)!='' ORDER BY s.locg_id,g.genre,e.format LIMIT 1").fetchone()
        if row: add('curated_and_facets','curated',curated_query,{'filters':[('publisher',row[0]),('genre',row[1]),('format',row[2].strip())],'roots':selections.get('curated')})
        cases=[]
        for id,family,fn,kwargs in specs:
            source=fn(**kwargs);projected=fn(**kwargs,projection=True)
            rows=[list(r) for r in db.execute(source['sql'],source['args'])]
            cases.append({'id':id,'family':family,'sourceSql':source['sql'],'projectionSql':projected['sql'],'args':source['args'],'expectedColumns':source['columns'],'expectedRows':rows,'expectedDigest':rows_digest(rows),'coverage':'raw scalar query/fact equality; no QVariant conversion or native composite ranking proof','tiePolicy':'native SQL leaves final equal sort keys unspecified' if family in ('imdb','comics') and id!='imdb_native_prefix' else 'explicit ordering'})
    return {'version':1,'sourceHashes':hashes,'asOf':as_of,'population':'full-snapshot' if not selections else 'explicit-selected-roots','selections':selections,'cases':cases,'remainingGaps':['native curated houseScore/order','VaultKit normalizedTitle input normalization','QVariant/QML null and argument conversion','MAL animeCatalog and all remaining APIs','nested source claims/download mirrors native presentation']}
