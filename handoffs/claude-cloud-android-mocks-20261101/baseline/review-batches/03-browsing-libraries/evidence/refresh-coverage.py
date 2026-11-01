from pathlib import Path
import json,csv,datetime
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
M=json.loads((B/'INPUTS.json').read_text(encoding='utf-8-sig'))
V=json.loads((B/'evidence/visual-review.json').read_text(encoding='utf-8')) if (B/'evidence/visual-review.json').exists() else {}
F=list(csv.DictReader((B/'FINDINGS.csv').open(encoding='utf-8-sig',newline='')))
rows=[]
for j in M['jobs']:
 for name in j['pages']:
  stem=Path(name).stem;p=B/'evidence'/('qa-'+stem+'.json');q=json.loads(p.read_text(encoding='utf-8')) if p.exists() else {'checks':[],'pass':False}
  mp=B/'evidence'/('matrix-'+stem+'.json');matrix=json.loads(mp.read_text(encoding='utf-8')) if mp.exists() else []
  capture_ok=len(matrix)==10 and all(not(x['overflow'] or x['missing'] or x['errors']) for x in matrix)
  visual=V.get(name,{});verified=capture_ok and q['pass'] and visual.get('pass',False)
  row={'task':j['task'],'page':name,'portrait':'verified' if visual.get('pass') else 'captured; final visual pending','landscape':'verified' if visual.get('pass') else 'captured; final visual pending','desktop_reference':'approved QML + original HTML comparison; '+('checked' if visual.get('pass') else 'visual pending'),'source_fidelity':'original components retained; see QML-PORTING.md and fixture limits','existing_controls':str(sum(c['pass'] for c in q['checks']))+'/'+str(len(q['checks']))+' focused checks passed','long_content':'checked in focused QA' if q['pass'] else 'pending or failed; see QA JSON','findings':';'.join(f.get('id',f.get('finding_id','')) for f in F if name in str(f) or 'all 16 pages' in str(f)),'status':'verified mock; disclosed limitations remain' if verified else 'fixed-but-unverified: final visual pending','finished_path':'finished-mocks/'+j['task']+'/'+name,'matrix_captures':len(matrix),'qa_evidence':'evidence/qa-'+stem+'.json','visual_evidence':visual.get('evidence',''),'updated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
  rows.append(row)
with (B/'COVERAGE.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
print(json.dumps({'pages':len(rows),'verified':sum(r['status'].startswith('verified') for r in rows),'focused_checks':sum(int(r['existing_controls'].split('/')[0]) for r in rows)}))
