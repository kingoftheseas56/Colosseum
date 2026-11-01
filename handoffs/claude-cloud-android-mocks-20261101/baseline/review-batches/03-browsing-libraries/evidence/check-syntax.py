from pathlib import Path
import subprocess,json
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries")
out=[]
for f in (p/"finished-mocks").rglob("*.js"):
 r=subprocess.run(["node","--check",str(f)],capture_output=True,text=True);out.append({"file":str(f.relative_to(p)),"ok":r.returncode==0,"error":r.stderr})
(p/"evidence"/"syntax.json").write_text(json.dumps(out,indent=2),encoding="utf-8")
print(json.dumps(out))