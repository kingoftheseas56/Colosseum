#!/usr/bin/env python3
"""Integrate Sol's verified receiving corrections into the candidate.

Each corrected file is hash-checked against the value bound in the brief /
remediation record, then placed at the finished-mock path it corrects, applying the
same escaping-URL rewrite used to seed the candidate. A ledger records
source hash, integrated hash and rewrites.
"""
import hashlib, json, os, posixpath, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
URL = re.compile(r"""(?P<q>["'(])(?P<u>(?:\./)?(?:\.\./)+[^"'()\s<>?#]*)""")
FIXES = [
  ("02-detail-pages", "codex-review-sol/remediation/app.js", "finished-mocks/17-theatre-details/app.js",
   "64fd16d35f596a89175e1c573924527a48a9c237acb3ba68a72dcac2053c7ce5", "SOL-B2-001 Android window-action gating (chromeMarkup)"),
  ("03-browsing-libraries", "codex-review-sol/candidate/app.js", "finished-mocks/20-search-browse/app.js",
   "5f3adf984432ea9a0fde19c7a8b6050c93e5b4682ed8d805f39235cba32e27f4", "SOL-B3-01/02 + REM-F01 Search query/caret/Enter/focus identity"),
  ("05-tracker-utilities", "codex-review-sol/tracker.js", "finished-mocks/tracker.js",
   "866b0560ac0794cf305676e063fbf362f9d2badb580dbe1417b4379e9f5b75c7", "SOL-B5-001 exact dossier opener / nested dialog focus"),
]
def h(b): return hashlib.sha256(b).hexdigest()
ledger = []
for batch, src, dst, want, why in FIXES:
    sp = os.path.join(ROOT, "baseline/review-batches", batch, src)
    raw = open(sp, "rb").read()
    if h(raw) != want: sys.exit("hash mismatch " + sp)
    rel = posixpath.join("review-batches", batch, dst)
    here, tree = posixpath.dirname(rel), posixpath.join("review-batches", batch, "finished-mocks")
    changes = []
    def fix(m):
        u = m.group("u"); t = posixpath.normpath(posixpath.join(here, u))
        if t == tree or t.startswith(tree + "/") or t.startswith(".."): return m.group(0)
        n = posixpath.relpath(posixpath.join("../baseline", t), here)
        if u.endswith("/") and not n.endswith("/"): n += "/"
        changes.append([u, n]); return m.group("q") + n
    out = URL.sub(fix, raw.decode("utf-8")).encode("utf-8")
    dp = os.path.join(ROOT, "candidate", rel)
    before = h(open(dp, "rb").read())
    open(dp, "wb").write(out)
    ledger.append({"fix": why, "source": "baseline/review-batches/%s/%s" % (batch, src), "sourceSha256": want,
                   "target": "candidate/" + rel, "replacedSeededSha256": before, "integratedSha256": h(out), "rewrites": changes})
json.dump(ledger, open(os.path.join(ROOT, "candidate/verify/sol-integration.json"), "w"), indent=1)
for l in ledger: print(l["target"], len(l["rewrites"]), l["integratedSha256"][:12])
