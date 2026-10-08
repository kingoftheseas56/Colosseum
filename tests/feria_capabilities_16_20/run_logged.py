#!/usr/bin/env python3
"""Keep raw command exits, including expected baseline failure, without masking red."""
import datetime,json,pathlib,subprocess,sys
label=sys.argv[1];cmd=sys.argv[2:];d=pathlib.Path('evidence');d.mkdir(exist_ok=True)
start=datetime.datetime.now(datetime.timezone.utc).isoformat()
with (d/(label+'.log')).open('w') as f:
    print('COMMAND',repr(cmd),flush=True)
    p=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
    for line in p.stdout:print(line,end='',flush=True);f.write(line);f.flush()
    rc=p.wait()
r={'command':cmd,'cwd':str(pathlib.Path.cwd()),'startedAt':start,'exitCode':rc,'finishedAt':datetime.datetime.now(datetime.timezone.utc).isoformat()}
(d/(label+'.result.json')).write_text(json.dumps(r,indent=2)+'\n');print('RAW EXIT',label,rc,flush=True)
sys.exit(rc if rc>=0 else 128-rc)
