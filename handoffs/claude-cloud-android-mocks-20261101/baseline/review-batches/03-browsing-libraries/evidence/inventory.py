from pathlib import Path
import json,hashlib,datetime
B=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries")
M=json.loads((B/"INPUTS.json").read_text(encoding="utf-8-sig"))
R=Path(M["batchRoot"])
files=[]; jobs=[]; sources=[]
def entry(p):
    st=p.stat(); return {"path":str(p),"bytes":st.st_size,"mtimeUtc":datetime.datetime.fromtimestamp(st.st_mtime,datetime.timezone.utc).isoformat(),"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
for j in M["jobs"]:
    d=Path(j["output"]); direct=[p for p in d.iterdir() if p.is_file()]
    files.extend(entry(p) for p in direct)
    caps=[entry(p) for p in (d/"captures").rglob("*") if p.is_file() and p.suffix.lower() in [".png",".json",".md",".js",".cjs"]] if (d/"captures").exists() else []
    jobs.append({"task":j["task"],"files":[{"file":p.name,"bytes":p.stat().st_size,"lines":len(p.read_text(encoding="utf-8",errors="replace").splitlines())} for p in direct],"captureFiles":caps})
    for s in j["sources"]:
        p=R/"reference"/"desktop"/"qml"/s
        found=[p] if p.exists() else list((R/"reference"/"desktop").rglob(s))
        sources.append({"task":j["task"],"source":s,"matches":[entry(x)|{"lines":len(x.read_text(encoding="utf-8",errors="replace").splitlines())} for x in found]})
result={"startedUtc":"2026-10-31T22:37:56Z","snapshotUtc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"files":files,"jobs":jobs,"sources":sources,"pins":M["references"]}
(B/"evidence"/"INPUT-VERSIONS-START.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
for j in jobs: print(json.dumps({"task":j["task"],"files":j["files"],"captureCount":len(j["captureFiles"])}))
for s in sources: print(json.dumps({"task":s["task"],"source":s["source"],"matches":[{"path":x["path"],"lines":x["lines"]} for x in s["matches"]]}))
