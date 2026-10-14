"""Full-population independent SQL parity and bounded local cost evidence."""
import argparse,hashlib,json,sqlite3
from pathlib import Path
from packed_benchmark import measure
from prepare import encode

def run(source_dir,artifact):
 source=sqlite3.connect(':memory:',uri=True);packed=sqlite3.connect(artifact.resolve().as_uri()+'?mode=ro',uri=True)
 for dataset in ('imdb','mal','comics'):source.execute('ATTACH DATABASE ? AS '+dataset,((source_dir/(dataset+'_catalog.db')).resolve().as_uri()+'?mode=ro',))
 cases=[
 ('imdb_genre','SELECT t.tt FROM imdb.genre g JOIN imdb.title t ON t.tt=g.tt WHERE g.genre=? AND t.rating>=? ORDER BY t.votes DESC LIMIT 25','SELECT t.tt FROM den_imdb_title t,json_each(t.__membership) g WHERE g.value=? AND t.rating>=? ORDER BY t.votes DESC LIMIT 25',['Drama',6]),
 ('mal_genre',"SELECT m.mal_id FROM mal.classification c JOIN mal.manga m ON m.mal_id=c.mal_id WHERE c.medium='manga' AND c.axis=? AND LOWER(c.value)=LOWER(?) AND m.explicit=0 ORDER BY m.members DESC,COALESCE(m.score,0) DESC,m.mal_id ASC LIMIT 25","SELECT m.mal_id FROM den_mal_manga m,json_each(m.__membership) c WHERE json_extract(c.value,'$[0]')=? AND LOWER(json_extract(c.value,'$[1]'))=LOWER(?) AND m.explicit=0 ORDER BY m.members DESC,COALESCE(m.score,0) DESC,m.mal_id ASC LIMIT 25",['genre','romance']),
 ('mal_facets',"SELECT c.value,COUNT(*) total FROM mal.classification c WHERE c.medium='manga' AND c.axis=? AND c.mal_id IN(SELECT mal_id FROM mal.manga WHERE explicit=0) GROUP BY c.value ORDER BY total DESC,c.value ASC","SELECT json_extract(c.value,'$[1]') value,COUNT(*) total FROM den_mal_manga m,json_each(m.__membership)c WHERE json_extract(c.value,'$[0]')=? AND m.explicit=0 GROUP BY value ORDER BY total DESC,value ASC",['genre']),
 ('comics_downloads','SELECT d.post_id,d.title,d.date FROM comics.download d WHERE d.series_id=? ORDER BY d.date DESC','SELECT d.post_id,d.title,d.date FROM den_comics_download d WHERE d.series_id=? ORDER BY d.date DESC',[690]),
 ('comics_curated_genres',"SELECT s.locg_id,(SELECT group_concat(genre) FROM comics.curated_genre g WHERE g.locg_id=s.locg_id),(SELECT COUNT(*) FROM comics.curated_edition e WHERE e.locg_id=s.locg_id) FROM comics.curated_series s ORDER BY s.rank ASC","SELECT s.locg_id,(SELECT group_concat(value) FROM json_each(s.__genres)),s.__editionCount FROM den_comics_curated_series s ORDER BY s.rank ASC",[])
 ]
 report=[]
 for name,a,b,args in cases:
  if name=='comics_downloads': args=[source.execute('SELECT series_id FROM comics.series_stats ORDER BY downloads DESC LIMIT 1').fetchone()[0]]
  x=measure(source,a,args,5);y=measure(packed,b,args,5);equal=not x['interrupted'] and not y['interrupted'] and x['rows']==y['rows']
  for result in (x,y):
   if 'rows'in result:rows=result.pop('rows');result['resultRows']=len(rows);result['resultSha256']=hashlib.sha256(encode(rows).encode()).hexdigest()
  row={'case':name,'source':x,'packed':y,'equal':equal,'packedSql':b,'args':args,'providerMeasured':False};report.append(row);print(encode(row),flush=True)
 artifact.with_name('packed-den-benchmark.json').write_text(encode(report)+'\n');return all(r['equal'] for r in report)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--artifact',type=Path,required=True);a=p.parse_args();raise SystemExit(0 if run(a.source_dir,a.artifact)else 1)
