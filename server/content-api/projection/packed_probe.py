"""Create bounded import probe from a COMPLETE local packed artifact; no remote IO."""
import argparse,json,sqlite3
from pathlib import Path
from prepare import quote,encode

def probe(artifact,output,max_rows=1000):
 report=json.loads(artifact.with_name('packed-report.json').read_text());remaining=max_rows;counts={}
 if output.exists():raise ValueError('Probe already exists')
 with sqlite3.connect(artifact.resolve().as_uri()+'?mode=ro',uri=True) as db,output.open('w',encoding='utf-8',newline='\n') as f:
  for ddl in report['schemaSql']:f.write(ddl+'\n')
  roots=[t for t in report['tables'] if t.get('derivedMembership')]
  for t in roots:
   cap=min(remaining,400);count=0
   for id,payload in db.execute('SELECT id,payload FROM '+quote(t['physical'])+' ORDER BY id LIMIT ?',(cap,)):
    f.write('INSERT INTO '+quote(t['physical'])+' VALUES ('+str(id)+",'"+payload.replace("'","''")+"');\n");count+=1
   counts[t['physical']]=count;remaining-=count
 metadata={'fullRevision':report['revision'],'physicalRows':max_rows-remaining,'tables':counts,'fullPopulation':False,'hostedQualified':False}
 output.with_suffix('.json').write_text(encode(metadata)+'\n');print(encode(metadata))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--artifact',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();probe(a.artifact,a.output)
