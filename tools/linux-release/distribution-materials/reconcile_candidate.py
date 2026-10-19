#!/usr/bin/env python3
"""Prove reuse against exact old byte inventory, never relabel old evidence."""
import argparse, json
from pathlib import Path
import inventory
NEW_SHA='ddf011d2955de39e19375d492284867db09d48a80f3b4fe717da086a3b3ec67b'
NEW_BYTES=333715118
NEW_COMMIT='7792bd1d9b3a41185cea522a9984ecd6586e3b44'
OLD_SHA='e35e704c901cd67421679fdc82fe4925f6fd28518a1c926c477bcb471df034ad'
def reconcile(package, prior, out):
    old_identity=json.loads((prior/'package-identity.json').read_text())
    if old_identity['sha256']!=OLD_SHA: raise ValueError('Wrong prior identity')
    inventory.PACKAGE_SHA256=NEW_SHA; inventory.PACKAGE_BYTES=NEW_BYTES
    inventory.collect(package,out)
    old=json.loads((prior/'payload-inventory.json').read_text())
    new=json.loads((out/'payload-inventory.json').read_text())
    key=lambda r:(r['section'],r['path'])
    a={key(r):r for r in old}; b={key(r):r for r in new}
    added=sorted(set(b)-set(a));removed=sorted(set(a)-set(b));changed=[];unchanged=[]
    for k in sorted(a.keys() & b.keys()):
        fields=('kind','size','sha256','target')
        if any(a[k].get(f)!=b[k].get(f) for f in fields):changed.append({'section':k[0],'path':k[1],'old':a[k],'new':b[k]})
        else:unchanged.append(k)
    proofs=json.loads((prior/'elf-provenance-inventory.json').read_text()); final=[]
    for row in proofs:
        path=row['path'];actual=b.get(('payload',path))
        if not actual or not actual.get('elf'): raise ValueError('Missing old ELF '+path)
        if actual['sha256']==row['shipped_sha256']:
            row=dict(row,reuse_basis={'prior_package_sha256':OLD_SHA,'current_package_sha256':NEW_SHA,'same_actual_file_sha256':actual['sha256']})
        elif path=='opt/colosseum/usr/bin/colosseum':
            row={'path':path,'family':'colosseum','shipped_sha256':actual['sha256'],'source_commit':NEW_COMMIT,'status':'recorded-build-identity','build_identity':'retained/payload/opt/colosseum/BUILD-IDENTITY.json'}
        else:raise ValueError('Changed dependency needs fresh proof '+path)
        final.append(row)
    identity=json.loads((out/'retained/payload/opt/colosseum/BUILD-IDENTITY.json').read_text())
    if NEW_COMMIT not in json.dumps(identity):raise ValueError('Candidate source identity missing')
    inventory.write_json(out/'elf-provenance-inventory.json',final)
    report={'prior_package':old_identity,'candidate_package':json.loads((out/'package-identity.json').read_text()),'unchanged_entries':len(unchanged),'changed':changed,'added':added,'removed':removed,'unchanged_dependency_elfs':sum('reuse_basis' in r for r in final),'total_elfs':len(final)}
    inventory.write_json(out/'comparison.json',report)
    print(json.dumps({'unchanged_entries':len(unchanged),'changed':len(changed),'added':len(added),'removed':len(removed),'reused_elfs':report['unchanged_dependency_elfs']}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);p.add_argument('--prior',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();reconcile(a.package,a.prior,a.output)
