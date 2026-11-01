from pathlib import Path
import json,datetime
B=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries")
M=json.loads((B/"INPUTS.json").read_text(encoding="utf-8-sig"))
for j in M["jobs"]:
    d=B/"finished-mocks"/j["task"]; d.mkdir(parents=True,exist_ok=True)
    for name in j["pages"]+["app.js","style.css"]:
        src=Path(j["output"])/name
        text=src.read_text(encoding="utf-8-sig").replace("../../reference/","../../../../reference/")
        (d/name).write_text(text,encoding="utf-8")
(B/"CHECKPOINT.md").write_text("# Batch 3 checkpoint\n\nScope updated by Hemanth: finish corrected HTML mocks inside finished-mocks/; preserve originals and frozen references.\nStarted 2026-10-31 22:37:56 UTC; handoff no later than 2026-11-01 01:37:56 UTC (07:07 IST).\n16 required pages inventoried and original SHA-256 manifest saved.\nCurrent: finished copies prepared with relocated local asset paths; these copies are not yet reviewed or verified.\nOriginal reports absent for jobs 08,09,15,16,20 at inventory time; fidelity notes absent for 08,20.\nNext: isolated-browser baseline, page-by-page fidelity and interaction repairs, independent retest.\nNo app/repository/build/device/service/publication work authorized.\n",encoding="utf-8")
print("Prepared 16 HTML pages and 12 shared JS/CSS files in finished-mocks; originals preserved.")
