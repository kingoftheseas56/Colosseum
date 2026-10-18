"""Run exact production verifier filesystem assertions, no Miniflare/router part."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();s=a.source.resolve();w=a.output.resolve()
f=s/'server/content-api/projection/verify_public_static.mjs';text=f.read_text();end=text.index('const cfg=');segment=text[:end]
segment=segment.replace("import {Miniflare,convertV4MiniflareOptions} from 'miniflare';\n",'').replace("from './public_static.mjs'","from "+json.dumps((s/'server/content-api/projection/public_static.mjs').as_uri()))
segment+='\nwriteFileSync(evidence,JSON.stringify({filesystemIntegrity:"PASS",router:"NOT_RUN",nativeRuntime:"NOT_RUN",revision:entry.revision,files:paths.length,hashedFiles,totalBytes:bytes},null,2));\n'
(w/'production-filesystem-verifier.mjs').write_text(segment)
for label in ['A','B']:
 previous=w/('content-A' if label=='A' else 'publication-A')
 subprocess.run(['node',str(w/'production-filesystem-verifier.mjs'),str(w/('publication-'+label)),str(w/('publication-'+label+'-report.json')),str(w/('verification-'+label+'.json')),str(previous)],check=True)
(w/'verifier-extraction.json').write_text(json.dumps({'source':'server/content-api/projection/verify_public_static.mjs','sourceSha256':hashlib.sha256(f.read_bytes()).hexdigest(),'range':'Start through immediately before const cfg= (all filesystem assertions intact)','changes':['Remove unused Miniflare import','Resolve stock public_static.mjs import absolutely','Append filesystem-only receipt'],'router':'NOT_RUN: no local sockets authorized','fullVerifierPass':False},indent=2))
