#!/usr/bin/env python3
"""Deterministic process boundary, never substitutes for real media evidence."""
import json,sys,time
url=sys.argv[-1]
if url.endswith('fail'): sys.exit(7)
if url.endswith('slow'):
    print('{"streams":[',flush=True)
    time.sleep(60)
elif url.endswith('multiple'): print('{"format":{"duration":"1"}}\n{"streams":[]}')
elif url.endswith('invalid'): print('{"a":]}')
elif url.endswith('malformed'): print('{bad')
else: print(json.dumps({'format':{'duration':'1.5','format_name':'fixture'},'streams':[]}))
