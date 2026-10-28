"""Lossless full-source packed JSON1 experiment. Inputs are immutable; no remote writes.

Views preserve declared column affinity and source scan order. Packing saves physical
writes but removes source indexes; performance must pass the bounded hosted probe.
"""
import argparse, hashlib, json, re, sqlite3, time
from collections import defaultdict
from contextlib import closing
from pathlib import Path
from prepare import encode, file_hash, quote

ROOT = Path(__file__).resolve().parents[1]


def affinity(declared):
    declared = declared.upper()
    if 'INT' in declared: return 'INTEGER'
    if any(x in declared for x in ('CHAR','CLOB','TEXT')): return 'TEXT'
    if any(x in declared for x in ('REAL','FLOA','DOUB')): return 'REAL'
    if not declared or 'BLOB' in declared: return None
    return 'NUMERIC'


def build(source_dir, output_dir, pack_size=128, datasets=None, tankoban_source=None):
    if pack_size not in (64,128): raise ValueError('Pack size must be 64 or 128')
    manifest=json.loads((ROOT/'source-manifest.json').read_text())
    datasets=list(manifest['sources']) if datasets is None else datasets
    output_dir=Path(output_dir);output_dir.mkdir(parents=True,exist_ok=True)
    artifact=output_dir/'packed.sqlite';sql_path=output_dir/'packed.sql';report_path=output_dir/'packed-report.json'
    if any(p.exists() for p in (artifact,sql_path,report_path)): raise ValueError('Output already exists')
    report={'format':'packed-json1-v1','packSize':pack_size,'tables':[], 'sources':{},'hostedQualified':False}
    schemas=[];written=0
    with closing(sqlite3.connect(artifact)) as out, sql_path.open('w',encoding='utf-8',newline='\n') as sql:
        out.execute('PRAGMA journal_mode=OFF')
        for dataset in datasets:
            expected=manifest['sources'][dataset];path=Path(tankoban_source) if dataset=='tankoban' and tankoban_source else Path(source_dir)/(dataset+'_catalog.db')
            if any(Path(str(path)+s).exists() for s in ('-wal','-journal')): raise ValueError('Source not frozen')
            if path.stat().st_size!=expected['bytes'] or file_hash(path)!=expected['sha256']: raise ValueError('Source hash mismatch: '+dataset)
            report['sources'][dataset]=expected
            with closing(sqlite3.connect(path.resolve().as_uri()+'?mode=ro',uri=True)) as src:
                for (table,) in src.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"):
                    cols=list(src.execute('PRAGMA table_info('+quote(table)+')'))
                    name='lib_'+dataset+'_'+table;physical='pack_'+dataset+'_'+table
                    ddl='CREATE TABLE '+quote(physical)+' (id INTEGER PRIMARY KEY,payload TEXT NOT NULL CHECK(json_valid(payload)));'
                    expressions=[]
                    for idx, col in enumerate(cols):
                        expr="json_extract(j.value,'$["+str(idx)+"]')"
                        aff=affinity(col[2])
                        if aff: expr='CAST('+expr+' AS '+aff+')'
                        expressions.append(expr+' AS '+quote(col[1]))
                    view='CREATE VIEW '+quote(name)+' AS SELECT '+','.join(expressions)+' FROM '+quote(physical)+' p,json_each(p.payload) j;'
                    out.execute(ddl);out.execute(view);schemas.extend((ddl,view));sql.write(ddl+'\n'+view+'\n')
                    # rowid scan reproduces the source table scan; WITHOUT ROWID tables use PK order.
                    without=src.execute('SELECT sql FROM sqlite_master WHERE name=?',(table,)).fetchone()[0]
                    order=' ORDER BY '+','.join(quote(c[1]) for c in sorted(cols,key=lambda c:c[5]) if c[5]) if 'WITHOUT ROWID' in without.upper() else ' ORDER BY rowid'
                    cursor=src.execute('SELECT * FROM '+quote(table)+order)
                    count=0;packs=0;maximum=0
                    pending=cursor.fetchone()
                    while pending is not None:
                        rows=[];payload_bytes=2
                        while pending is not None and len(rows)<pack_size:
                            record_bytes=len(encode(pending).replace("'","''").encode("utf-8"))+1
                            if payload_bytes+record_bytes>94000:
                                if not rows: raise ValueError('Single record exceeds D1 SQL statement limit')
                                break
                            rows.append(pending);payload_bytes+=record_bytes;pending=cursor.fetchone()
                        if any(isinstance(v,bytes) for row in rows for v in row): raise ValueError('BLOB packing needs explicit codec')
                        payload=encode(rows);size=len(payload.encode('utf-8'))
                        if size>1900000: raise ValueError('Packed row exceeds safe D1 row cap')
                        maximum=max(maximum,size);packs+=1;count+=len(rows)
                        out.execute('INSERT INTO '+quote(physical)+' VALUES (?,?)',(packs,payload))
                        sql.write('INSERT INTO '+quote(physical)+" VALUES ("+str(packs)+",'"+payload.replace("'","''")+"');\n")
                    # Full row/SQLite storage-type comparison, in original source scan order.
                    original=src.execute('SELECT * FROM '+quote(table)+order)
                    projected=out.execute('SELECT * FROM '+quote(name))
                    mismatch=next(((a,b) for a,b in zip(original,projected) if a!=b or any(type(x)!=type(y) for x,y in zip(a,b))),None)
                    if mismatch: raise ValueError('Typed source parity failed for '+name+': '+repr(mismatch))
                    report['tables'].append({'dataset':dataset,'sourceTable':table,'view':name,'physical':physical,'columns':[c[1] for c in cols],'rows':count,'packedRows':packs,'maxPayloadBytes':maximum,'fullTypedRowParity':True})
                    written+=packs
                # Membership stored beside each root eliminates unindexed cross-view joins.
                for root in (['title'] if dataset=='imdb' else ['anime','manga'] if dataset=='mal' else ['series','curated_series','download'] if dataset=='comics' else []):
                    root_cols=list(src.execute('PRAGMA table_info('+quote(root)+')'))
                    children=defaultdict(list); tags=defaultdict(list)
                    if dataset=='imdb':
                        for key,value in src.execute('SELECT tt,genre FROM genre ORDER BY rowid'): children[key].append(value)
                    elif dataset=='mal':
                        for key,axis,value in src.execute('SELECT mal_id,axis,value FROM classification WHERE medium=? ORDER BY rowid',(root,)): children[key].append([axis,value])
                        for key,value in src.execute('SELECT mal_id,tag FROM tag WHERE medium=? ORDER BY rowid',(root,)): tags[key].append(value)
                    if dataset=='comics':
                        src.row_factory=sqlite3.Row
                        stats={r['series_id']:dict(r) for r in src.execute('SELECT * FROM series_stats')}
                        fanmade={r[0] for r in src.execute('SELECT DISTINCT series_id FROM download WHERE fan_made=1')}
                        mirrors=defaultdict(list)
                        for r in src.execute('SELECT post_id,url,host,label FROM download_mirror ORDER BY rowid'): mirrors[r[0]].append({'url':r[1],'host':r[2],'label':r[3]})
                        genres=defaultdict(list);formats=defaultdict(list);editions=defaultdict(list)
                        for r in src.execute('SELECT locg_id,genre FROM curated_genre ORDER BY rowid'): genres[r[0]].append(r[1])
                        for r in src.execute('SELECT locg_id,format,available,getcomics_post,published FROM curated_edition ORDER BY rowid'):
                            editions[r[0]].append(dict(r));formats[r[0]].append(r[1])
                        coverage={r['locg_id']:dict(r) for r in src.execute('SELECT * FROM curated_series_coverage')}
                        src.row_factory=None
                    extra=({'series':['__downloads','__kinds','__latestPost','__fanMade'],'curated_series':['__genres','__formats','__editionCount','__availableCount','__publishedMaxYear','__coverage'],'download':['__mirrors']}[root] if dataset=='comics' else ['__membership']+(['__tags'] if dataset=='mal' else []))
                    physical='denpack_'+dataset+'_'+root;viewname='den_'+dataset+'_'+root
                    ddl='CREATE TABLE '+quote(physical)+' (id INTEGER PRIMARY KEY,payload TEXT NOT NULL CHECK(json_valid(payload)));'
                    expressions=[]
                    for idx,col in enumerate(root_cols):
                        expr="json_extract(j.value,'$["+str(idx)+"]')";aff=affinity(col[2])
                        if aff: expr='CAST('+expr+' AS '+aff+')'
                        expressions.append(expr+' AS '+quote(col[1]))
                    for idx,name in enumerate(extra,len(root_cols)): expressions.append("json_extract(j.value,'$["+str(idx)+"]') AS "+quote(name))
                    view='CREATE VIEW '+quote(viewname)+' AS SELECT '+','.join(expressions)+' FROM '+quote(physical)+' p,json_each(p.payload) j;'
                    out.execute(ddl);out.execute(view);schemas.extend((ddl,view));sql.write(ddl+'\n'+view+'\n')
                    rows=[];packs=0;count=0;maximum=0;payload_bytes=2
                    def emit(records):
                        nonlocal packs,count,maximum,written
                        payload=encode(records);packs+=1;count+=len(records);written+=1;maximum=max(maximum,len(payload.encode('utf-8')))
                        out.execute('INSERT INTO '+quote(physical)+' VALUES (?,?)',(packs,payload))
                        sql.write('INSERT INTO '+quote(physical)+" VALUES ("+str(packs)+",'"+payload.replace("'","''")+"');\n")
                    for row in src.execute('SELECT * FROM '+quote(root)+' ORDER BY rowid'):
                        if dataset=='comics':
                            key=row[0]
                            if root=='series':
                                st=stats.get(key,{})
                                enrich=[st.get('downloads'),st.get('kinds'),st.get('latest_post'),int(key in fanmade)]
                            elif root=='download': enrich=[encode(mirrors[key])]
                            else:
                                ed=editions[key];years=[int(y) for e in ed for y in re.findall(r'(\d{4})',e['published'] or '') if 1900<=int(y)<=2100]
                                enrich=[encode(genres[key]),encode(formats[key]),len(ed),sum(1 for e in ed if e['available']==1 and e['getcomics_post'] not in ('',None)),max([0]+years),encode(coverage.get(key))]
                            record=list(row)+enrich
                        else: record=list(row)+[encode(children[row[0]])]+([encode(tags[row[0]])] if dataset=='mal' else [])
                        record_bytes=len(encode(record).replace("'","''").encode("utf-8"))+1
                        if rows and (len(rows)>=pack_size or payload_bytes+record_bytes>94000): emit(rows);rows=[];payload_bytes=2
                        if record_bytes>94000: raise ValueError("Single denormalized root exceeds statement cap")
                        rows.append(record);payload_bytes+=record_bytes
                    if rows: emit(rows)
                    report['tables'].append({'dataset':dataset,'sourceTable':root,'view':viewname,'physical':physical,'columns':[c[1] for c in root_cols]+extra,'rows':count,'packedRows':packs,'maxPayloadBytes':maximum,'derivedMembership':True})
            if file_hash(path)!=expected['sha256']: raise ValueError('Source changed')
        out.commit()
        report['integrityCheck']=out.execute('PRAGMA integrity_check').fetchone()[0]
    report['revision']=hashlib.sha256(encode(report['sources']).encode()).hexdigest()
    report['schemaSql']=schemas;report['sqliteBytes']=artifact.stat().st_size;report['sqlBytes']=sql_path.stat().st_size;report['sqliteSha256']=file_hash(artifact);report['sqlSha256']=file_hash(sql_path)
    report['physicalRows']=written;report['logicalRows']=sum(t['rows'] for t in report['tables'] if not t.get('derivedMembership'));report['derivedRows']=sum(t['rows'] for t in report['tables'] if t.get('derivedMembership'))
    report['writeEstimate']={'dataRows':written,'indexRows':0,'excludes':['DDL','receipts','cleanup'],'providerMeasured':False}
    report_path.write_text(encode(report)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--pack-size',type=int,default=128);p.add_argument('--datasets',nargs='+');p.add_argument('--tankoban-source',type=Path)
    a=p.parse_args();r=build(a.source_dir,a.output_dir,a.pack_size,a.datasets,a.tankoban_source)
    print(encode({k:r[k] for k in ('sqliteBytes','sqlBytes','physicalRows','logicalRows','integrityCheck','revision')}))
