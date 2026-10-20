"""Materialize exact pristine and hardened donor states in NEW directories."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PIN='99a18601fa12858cc8c62576dc72b87edbd81b9a'
def run(*args):subprocess.run(args,check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('destination',type=Path)
    p.add_argument('--pristine',action='store_true')
    args=p.parse_args()
    base=Path(__file__).resolve().parent
    if args.destination.exists():raise SystemExit('Use a new destination; never overwrite donor work')
    # A bundle from a shallow repository omits its .git/shallow boundary.
    # Import its pack directly and restore that explicit boundary before traversal.
    # Do not claim this is a complete history or invent an unreachable parent.
    run('git','init',str(args.destination))
    run('git','-C',str(args.destination),'config','core.autocrlf','false')
    refs={}
    with (base/'donor-pristine-pin.bundle').open('rb') as bundle:
        if bundle.readline().strip()!=b'# v2 git bundle':raise SystemExit('Unexpected bundle format')
        while True:
            line=bundle.readline().strip()
            if not line:break
            sha,ref=line.decode().split(' ',1);refs[ref]=sha
        # Pass remaining bytes, not a buffered file descriptor whose OS offset
        # may already be ahead of the Python read position.
        subprocess.run(['git','-C',str(args.destination),'index-pack','--stdin'],input=bundle.read(),check=True)
    boundary=(base/'DONOR-GIT-SHALLOW.txt').read_text().strip()
    if boundary!=PIN:raise SystemExit('Unexpected shallow boundary')
    (args.destination/'.git/shallow').write_text(PIN+'\n',encoding='ascii')
    run('git','-C',str(args.destination),'update-ref','refs/heads/source-pin',PIN)
    run('git','-C',str(args.destination),'update-ref','refs/tags/v0.1.5',refs['refs/tags/v0.1.5'])
    run('git','-C',str(args.destination),'checkout','--detach',PIN)
    actual=subprocess.check_output(['git','-C',str(args.destination),'rev-parse','v0.1.5^{commit}']).decode().strip()
    if actual!=PIN:raise SystemExit('Tag pin mismatch')
    checked=0
    if not args.pristine:
        manifest=json.loads((base/'SOURCE-MANIFEST.json').read_text())
        upstream=set(subprocess.check_output(['git','-C',str(args.destination),'ls-tree','-r','--name-only',PIN]).decode().splitlines())
        intent=[]
        for entry in manifest['repositories']['donor']['files']:
            src=base/'source-donor'/entry['path']; dst=args.destination/entry['path']
            data=src.read_bytes()
            if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:
                raise SystemExit('Hardened donor snapshot mismatch: '+entry['path'])
            dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);checked+=1
            if entry['classification']=='tracked-working-copy' and entry['path'] not in upstream:
                intent.append(entry['path'])
        # Preserve original Windows checkout bytes while reproducing the exact
        # Git clean-filter semantics used to generate the audited donor patch.
        # These are settings on the NEW working donor only, never global.
        run('git','-C',str(args.destination),'config','core.autocrlf','true')
        run('git','-C',str(args.destination),'config','core.filemode','false')
        # The original audited delta includes a new integrity test already
        # represented in its index. Reproduce that visibility, without staging
        # its contents, so diff --binary HEAD includes the same new file.
        if intent:run('git','-C',str(args.destination),'add','--intent-to-add','--',*intent)
    print(json.dumps({'pin':PIN,'shallow':True,'destination':str(args.destination),'state':'pristine' if args.pristine else 'hardened-working-copy',
                      'verified_overlay_files':checked}))
