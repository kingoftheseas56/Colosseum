#!/usr/bin/env python3
"""Seed candidate/ from the frozen baseline finished mocks.

Copies each batch's finished-mocks tree to the same relative location under
candidate/review-batches/, then rewrites only relative URLs that would escape the
copied tree so they keep resolving to the identical baseline target
(e.g. ../../../../reference/desktop -> ../../../../../baseline/reference/desktop).
Everything else is byte-for-byte. A JSON ledger of every rewrite is written so the
mechanical change is auditable. Run once from the handoff root; it refuses to
overwrite an existing candidate tree.
"""
import hashlib, json, os, posixpath, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = os.path.join(ROOT, "baseline")
CAND = os.path.join(ROOT, "candidate")
BATCHES = ["01-sources-manga", "02-detail-pages", "03-browsing-libraries",
           "04-accounts-configuration", "05-tracker-utilities"]
TEXT = (".html", ".js", ".css", ".mjs", ".cjs")
URL = re.compile(r"""(?P<q>["'(])(?P<u>(?:\./)?(?:\.\./)+[^"'()\s<>?#]*)""")

def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()

def main():
    ledger = {"rule": "relative URL escaping the copied finished-mocks tree -> same target under baseline/", "files": []}
    for b in BATCHES:
        src = os.path.join(BASE, "review-batches", b, "finished-mocks")
        dst = os.path.join(CAND, "review-batches", b, "finished-mocks")
        if os.path.exists(dst):
            sys.exit("refusing to overwrite " + dst)
        shutil.copytree(src, dst)
        for dirpath, _, names in os.walk(dst):
            for n in names:
                p = os.path.join(dirpath, n)
                rel = os.path.relpath(p, CAND).replace(os.sep, "/")
                if not n.endswith(TEXT):
                    continue
                raw = open(p, "rb").read()
                text = raw.decode("utf-8")
                here = posixpath.dirname(rel)  # relative to candidate root == baseline root
                tree = "review-batches/%s/finished-mocks" % b
                changes = []
                def fix(m):
                    u = m.group("u")
                    target = posixpath.normpath(posixpath.join(here, u))
                    if target == tree or target.startswith(tree + "/"):
                        return m.group(0)
                    if target.startswith(".."):
                        return m.group(0)  # escapes the handoff root entirely; leave untouched
                    new = posixpath.relpath(posixpath.join("../baseline", target), here)
                    if u.endswith("/") and not new.endswith("/"):
                        new += "/"
                    changes.append([u, new])
                    return m.group("q") + new
                out = URL.sub(fix, text)
                if changes:
                    open(p, "wb").write(out.encode("utf-8"))
                    ledger["files"].append({"path": rel, "baselineSha256": hashlib.sha256(raw).hexdigest(),
                                            "seededSha256": sha(p), "rewrites": changes})
    json.dump(ledger, open(os.path.join(CAND, "verify", "seed-rewrites.json"), "w"), indent=1)
    print("rewritten files:", len(ledger["files"]), "rewrites:", sum(len(f["rewrites"]) for f in ledger["files"]))

if __name__ == "__main__":
    main()
