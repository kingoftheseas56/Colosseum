#!/usr/bin/env python3
"""Verify and extract immutable cloud input references; never modifies product source."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, tarfile, gzip
ROOT=Path(__file__).resolve().parents[1]
def digest(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def safe(base,name):
 posix=PurePosixPath(name)
 if posix.is_absolute() or '..' in posix.parts or chr(92) in name:raise ValueError('Unsafe member: '+name)
 dest=(base/Path(*posix.parts)).resolve()
 if not dest.is_relative_to(base.resolve()):raise ValueError('Path escape: '+name)
 return dest
def verify():
 manifest=json.loads((ROOT/'MANIFEST.json').read_text(encoding='utf-8'))
 for name,expected in manifest['files'].items():
  path=safe(ROOT,name)
  if not path.is_file() or digest(path)!=expected['sha256'] or path.stat().st_size!=expected['bytes']:raise ValueError('Input mismatch: '+name)
 print(json.dumps({'result':'INPUTS_VERIFIED','files':len(manifest['files']),'beta_commit':manifest['beta_commit'],'alpha_commit':manifest['alpha_commit']}))
 return manifest
def extract(destination):
 manifest=verify();dest=Path(destination).expanduser().resolve()
 checkout=ROOT.parents[2]
 if dest.is_relative_to(checkout):raise ValueError('Donor references must stay OUTSIDE product checkout')
 dest.mkdir(parents=True,exist_ok=True)
 for stem in ('alpha-source','alpha-working','visual-overlays'):
  inventory=json.loads((ROOT/'inputs'/(stem+'-manifest.json')).read_text(encoding='utf-8'))
  expected={row['path']:row for row in inventory['files']};seen=set();out=dest/stem;out.mkdir(exist_ok=True)
  with tarfile.open(ROOT/'inputs'/(stem+'.tar.gz'),'r:gz') as tar:
   for member in tar:
    if not member.isfile() or member.name not in expected or member.name in seen:raise ValueError('Unexpected member: '+member.name)
    row=expected[member.name]
    if member.size!=row['bytes']:raise ValueError('Size mismatch: '+member.name)
    data=tar.extractfile(member).read()
    if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Hash mismatch: '+member.name)
    path=safe(out,member.name);path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists() and (not path.is_file() or path.read_bytes()!=data):raise ValueError('Existing donor drift preserved: '+str(path))
    if not path.exists():path.write_bytes(data)
    seen.add(member.name)
  if seen!=expected.keys():raise ValueError('Missing archive members: '+stem)
 patch=gzip.decompress((ROOT/'inputs/visual-overlays.patch.gz').read_bytes())
 if hashlib.sha256(patch).hexdigest()!=manifest['visual_overlay_patch_sha256']:raise ValueError('Patch content drift')
 patchpath=dest/'visual-overlays.patch'
 if patchpath.exists() and patchpath.read_bytes()!=patch:raise ValueError('Existing patch drift preserved')
 if not patchpath.exists():patchpath.write_bytes(patch)
 print(json.dumps({'result':'DONORS_EXTRACTED','destination':str(dest),'product_source_modified':False}))
def card(identifier):
 path=safe(ROOT/'tasks',identifier+'.md')
 if not path.is_file():raise ValueError('Unknown task '+identifier)
 print(path.read_text(encoding='utf-8'))
def main():
 parser=argparse.ArgumentParser();parser.add_argument('action',choices=('verify','extract','card'));parser.add_argument('value',nargs='?');args=parser.parse_args()
 if args.action=='verify':verify()
 elif args.action=='extract':
  if not args.value:parser.error('extract requires an outside-checkout destination')
  extract(args.value)
 else:
  if not args.value:parser.error('card requires an audit ID')
  card(args.value)
if __name__=='__main__':main()
