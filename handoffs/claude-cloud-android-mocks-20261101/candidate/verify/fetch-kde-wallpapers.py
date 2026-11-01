#!/usr/bin/env python3
"""Restore Batch 5's omitted KDE wallpaper files byte-exactly.

The packet omitted the 12 upstream KDE images Pro fetched for wallpaper-search
(see baseline/review-batches/05-tracker-utilities/evidence/kde-assets.json). This
script downloads the exact recorded upstream URLs with curl and keeps a file only if
its SHA-256 matches the recorded hash. Nothing is substituted. Files land in
candidate/review-batches/05-tracker-utilities/finished-mocks/assets/kde/ (git-ignored).
Without them the page still runs; those tiles show the inherited missing-image gap.
"""
import hashlib, json, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rec = json.load(open(os.path.join(ROOT, "baseline/review-batches/05-tracker-utilities/evidence/kde-assets.json"), encoding="utf-8"))
dest = os.path.join(ROOT, "candidate/review-batches/05-tracker-utilities/finished-mocks")
ok = bad = 0
for r in rec["records"]:
    if "url" not in r or not r["local"].startswith("assets/kde/"):
        continue
    out = os.path.join(dest, r["local"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out) and hashlib.sha256(open(out, "rb").read()).hexdigest() == r["sha256"]:
        ok += 1; continue
    tmp = out + ".part"
    subprocess.run(["curl", "-sSfL", "-o", tmp, r["url"]], check=False)
    got = hashlib.sha256(open(tmp, "rb").read()).hexdigest() if os.path.exists(tmp) else None
    if got == r["sha256"]:
        os.replace(tmp, out); ok += 1; print("verified", r["local"])
    else:
        bad += 1; print("MISMATCH/UNAVAILABLE", r["local"], got, file=sys.stderr)
        if os.path.exists(tmp): os.remove(tmp)
print(json.dumps({"verified": ok, "failed": bad}))
sys.exit(1 if bad else 0)
