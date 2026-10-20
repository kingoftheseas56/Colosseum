#!/usr/bin/env python3
"""Record source non-completion and bounded candidate fallback, NOT exact parity."""
import argparse,json,pathlib,subprocess,struct
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--oracle',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out).resolve();rows=[]
for name,data in [('empty',b''),('absent_moov',struct.pack('>I4s',108,b'free')+bytes(100))]:
 f=out/'fixtures'/(name+'.mp4');f.write_bytes(data);case=out/(name+'-case.json');case.write_text(json.dumps([{'name':name,'mode':'mp4','file':str(f)}]))
 ref=subprocess.run(['node',str(pathlib.Path(__file__).with_name('run_samples_oracle.cjs')),a.oracle,str(case)],capture_output=True,timeout=5)
 actual=json.loads(subprocess.check_output([a.candidate,'async-mp4',str(f)],timeout=5));assert not actual['available'] and actual['error']=='max seeks exceeded'
 rows.append({'name':name,'source_exit':ref.returncode,'source_stdout':ref.stdout.decode(),'source_completion_observed':bool(ref.stdout),'candidate':actual,'classification':'documented liveness deviation'})
f=out/'fallback-group.json';f.write_text(json.dumps({'stream':{'track':'video','timescale':1000},'timescale':90000,'segmentDuration':2,'sampleDuration':40,'samples':[]}));result=json.loads(subprocess.check_output([a.candidate,'group',str(f)]));assert result['predicted'] and result['segmentDuration']==2
rows.append({'name':'empty_index_predicts','candidate':result});(out/'fallback.json').write_text(json.dumps(rows,indent=2));print('3 bounded fallback checks passed; 2 source non-completion deviations recorded')
