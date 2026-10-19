#!/usr/bin/env python3
"""Prepare candidate-specific source assets locally, without publishing."""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
COMMIT='7792bd1d9b3a41185cea522a9984ecd6586e3b44'
APP_SHA='69222a8fd32dee331849a2a1a160001b5d47c7e79b390fb25ee21c939298dac1'
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def prepare(prior, repo, materials, output):
 output.mkdir(exist_ok=False)
 for name in ('sources','upstream'):shutil.copytree(prior/name,output/name)
 (output/'application').mkdir();app=output/'application'/f'colosseum-{COMMIT}.tar.gz'
 subprocess.run(['git','-C',str(repo),'archive','--format=tar.gz',f'--prefix=colosseum-{COMMIT}/',f'--output={app.resolve()}',COMMIT],check=True)
 if digest(app)!=APP_SHA:raise ValueError('Application archive identity differs')
 shutil.copytree(materials/'supplements',output/'supplements',ignore=shutil.ignore_patterns('build-input-primary-sources.tar.gz'))
 for name in ('ubuntu-source-index.json','icu-candidate-source.json'):shutil.copy2(prior/name,output/name)
 shutil.copy2(materials/'upstream-source-index.updated.json',output/'upstream-source-index.json')
 shutil.copy2(materials/'SOURCE-README.txt',output/'README.txt')
 index=[{'path':str(p.relative_to(output)),'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(output.rglob('*')) if p.is_file()]
 index.sort(key=lambda row: row['path'])
 (output/'SHA256-INDEX.json').write_text(json.dumps(index,indent=2)+'\n')
 print(json.dumps({'files':len(index),'application_sha256':digest(app)}))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--repository',type=Path,required=True);p.add_argument('--materials',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();prepare(a.prior,a.repository,a.materials,a.output)
