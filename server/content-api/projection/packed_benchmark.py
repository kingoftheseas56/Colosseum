"""Bounded source/packed SQL semantic and query-cost experiment (not hosted proof)."""
import argparse,json,sqlite3,time
from pathlib import Path
from contracts import imdb_query,mal_query,comics_search_query
from prepare import encode


def measure(db,sql,args,budget=2):
    start=time.perf_counter();ticks=0
    def progress():
        nonlocal ticks
        ticks+=1
        return time.perf_counter()-start>budget
    db.set_progress_handler(progress,10000)
    try:
        rows=db.execute(sql,args).fetchall()
        return {'elapsedMs':round((time.perf_counter()-start)*1000,2),'vmStepsLowerBound':ticks*10000,'rows':rows,'interrupted':False}
    except sqlite3.OperationalError as e:
        if str(e)!='interrupted': raise
        return {'elapsedMs':round((time.perf_counter()-start)*1000,2),'vmStepsLowerBound':ticks*10000,'interrupted':True}
    finally: db.set_progress_handler(None,0)


def benchmark(source_dir,artifact):
    source=sqlite3.connect(':memory:',uri=True);packed=sqlite3.connect(Path(artifact).resolve().as_uri()+'?mode=ro',uri=True)
    for dataset in ('imdb','mal','comics'):
        source.execute('ATTACH DATABASE ? AS '+dataset,( (Path(source_dir)/(dataset+'_catalog.db')).resolve().as_uri()+'?mode=ro',))
    cases=[('imdb_votes',imdb_query({})),('imdb_genre',imdb_query({'genre':'Drama','ratingMin':6})),('imdb_point',{'sql':'SELECT * FROM imdb.title WHERE tt=?','args':['tt0111161']}),('mal_popular',mal_query('popular',[])),('mal_genre',mal_query('popular',[('genre','romance')])),('comics_search',comics_search_query('batman'))]
    report=[]
    for name,query in cases:
        sql=query['sql'];psql=sql
        for dataset in ('imdb','mal','comics'):
            psql=psql.replace(dataset+'.','lib_'+dataset+'_')
        a=measure(source,sql,query['args']);b=measure(packed,psql,query['args'])
        equal=not a['interrupted'] and not b['interrupted'] and a['rows']==b['rows']
        for result in (a,b):
            if 'rows' in result: result['resultRows']=len(result.pop('rows'))
        row={'case':name,'source':a,'packed':b,'equal':equal,'sql':psql,'args':query['args'],'plan':[list(r) for r in packed.execute('EXPLAIN QUERY PLAN '+psql,query['args'])]}
        report.append(row);print(encode(row),flush=True)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--artifact',type=Path,required=True);a=p.parse_args()
    r=benchmark(a.source_dir,a.artifact);a.artifact.with_name('packed-benchmark.json').write_text(encode(r)+'\n',encoding='utf-8')

