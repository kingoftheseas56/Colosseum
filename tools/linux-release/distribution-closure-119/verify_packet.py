#!/usr/bin/env python3
"""Verify a receiving index independently, with strict path/size/hash checks."""
import argparse, hashlib, json
from pathlib import Path, PurePosixPath

def verify(root, index):
    rows=json.loads(index.read_text()); seen=set()
    for r in rows:
        rel=PurePosixPath(r['path'])
        if rel.is_absolute() or '..' in rel.parts or str(rel) in seen: raise ValueError('Unsafe/duplicate path')
        seen.add(str(rel)); p=root/rel
        if not p.is_file() or p.is_symlink() or p.stat().st_size!=r['bytes']: raise ValueError('Missing/wrong size '+str(rel))
        with p.open('rb') as f: h=hashlib.file_digest(f,'sha256').hexdigest()
        if h!=r['sha256']: raise ValueError('Hash mismatch '+str(rel))
    print(f'PASS: {len(rows)} indexed files verified')
    return len(rows)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--index',type=Path);a=p.parse_args();verify(a.root,a.index or a.root/'SHA256-INDEX.json')
