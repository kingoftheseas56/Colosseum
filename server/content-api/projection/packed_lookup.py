"""Build additive packed key/prefix directories. Never mutates the frozen artifact."""
import argparse,json,sqlite3
from pathlib import Path
from prepare import encode,quote,file_hash
ROOT=Path(__file__).resolve().parents[1]
FAMILIES={'imdb':['title'],'mal':['anime','manga'],'comics':['series','download','curated_series','curated_edition','curated_source','curated_series_coverage']}

def build(artifact,output):
 if output.exists():raise ValueError('Output exists')
 output.mkdir(parents=True);report=json.loads(artifact.with_name('packed-report.json').read_text());tables={t['view']:t for t in report['tables']};statements=[];total=0
 with sqlite3.connect(artifact.resolve().as_uri()+'?mode=ro',uri=True)as src,sqlite3.connect(output/'lookup.sqlite')as dst,(output/'lookup.sql').open('w',encoding='utf-8',newline='\n')as sql:
  specs=[]
  for dataset,families in FAMILIES.items():
   for family in families:
    spec=tables.get('den_'+dataset+'_'+family,tables['lib_'+dataset+'_'+family]);records=[];prefix=[];cols=spec['columns']
    for block,payload in src.execute('SELECT id,payload FROM '+quote(spec['physical'])+' ORDER BY id'):
     for ordinal,row in enumerate(json.loads(payload)):
      records.append([row[0],block,ordinal])
      if dataset in ('imdb','mal'):
       for name in ['norm_title']+(['norm_title_english']if dataset=='mal'else[]):
        value=row[cols.index(name)]
        if value:prefix.append([value,row[0],block,ordinal,row[cols.index('norm_title')],row[cols.index('year')],row[cols.index('rating' if dataset=='imdb' else 'score')],row[cols.index('votes')]if dataset=='imdb'else 0])
    specs.append((dataset+'_'+family,'key',records,isinstance(records[0][0],int)if records else False))
    if prefix:specs.append((dataset+'_'+family,'prefix',prefix,False))
  details=[]
  for family,kind,records,numeric in specs:
   records.sort(key=lambda r:r[0]);name='lookup_'+family+'_'+kind;typ='INTEGER'if numeric else'TEXT'
   ddl='CREATE TABLE '+quote(name)+' (id INTEGER PRIMARY KEY,min_key '+typ+' NOT NULL,max_key '+typ+' NOT NULL,payload TEXT NOT NULL);';dst.execute(ddl);sql.write(ddl+'\n');statements.append(ddl);count=0
   for start in range(0,len(records),128):
    rows=records[start:start+128];payload=encode(rows);count+=1
    dst.execute('INSERT INTO '+quote(name)+' VALUES (?,?,?,?)',(count,rows[0][0],rows[-1][0],payload))
    values=[str(count),str(rows[0][0])if numeric else"'"+str(rows[0][0]).replace("'","''")+"'",str(rows[-1][0])if numeric else"'"+str(rows[-1][0]).replace("'","''")+"'","'"+payload.replace("'","''")+"'"]
    line='INSERT INTO '+quote(name)+' VALUES ('+','.join(values)+');';assert len(line.encode())<95000;sql.write(line+'\n')
   details.append({'table':name,'records':len(records),'packedRows':count,'kind':kind,'numeric':numeric});total+=count
  for dataset,families in FAMILIES.items():
   for family in families:
    spec=tables.get('den_'+dataset+'_'+family,tables['lib_'+dataset+'_'+family]);name='hit_'+dataset+'_'+family
    original=next(s for s in report['schemaSql'] if s.startswith('CREATE VIEW '+quote(spec['view'])+' AS SELECT '))
    ddl=original.replace('CREATE VIEW '+quote(spec['view']), 'CREATE VIEW '+quote(name),1).replace(' AS SELECT ',' AS SELECT p.id AS __block,CAST(j.key AS INTEGER) AS __offset,',1)
    dst.execute(ddl);sql.write(ddl+'\n');statements.append(ddl)
  dst.commit()
 result={'revision':report['revision'],'sourceArtifactSha256':file_hash(artifact),'physicalRows':total,'schemaSql':statements,'tables':details,'sqliteBytes':(output/'lookup.sqlite').stat().st_size,'sqlSha256':file_hash(output/'lookup.sql'),'providerMeasured':False}
 (output/'lookup-report.json').write_text(encode(result)+'\n');print(encode(result))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--artifact',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.artifact,a.output)
