# Linux 1.1.9 candidate distribution receiving

Application target remains 7792bd1d9b3a41185cea522a9984ecd6586e3b44. Only source-material tooling, recipes and receiving evidence are added by this task branch. The original collector implementation is retained unchanged from f554aa75a7c2055d1f88842056d23de34b56db27; its older package constants remain intentional for validating original input evidence.

Extract `materials.tar.gz` into an empty directory and verify its `SHA256-INDEX.json` with `verify_packet.py`. Start with `reports/CLOSURE-REPORT.md` and `font-correction/switzer/ASSESSMENT.md`. Fraunces provenance is closed and its authentic OFL accompanies the notice set. The packet leaves one specific Switzer source-scope decision unresolved; it is not a release authorization or blanket legal clearance.

`reconcile_candidate.py` in the sibling distribution-materials directory verifies the exact current package and regenerates the candidate inventory while requiring unchanged dependency byte identities. `prepare_sources.py` is a retained local preparation recipe, not run by final CI. It generates the current source tree from fully verified prior sources and current application Git archive. `verify_packet.py` rejects unsafe paths, duplicate entries, missing/wrong-sized files and incorrect SHA256.

The bounded `linux-distribution-review-119.yml` workflow receives existing artifacts, verifies all 7,937 prior indexed files, reconciles the actual candidate and uploads only safe review materials and verification receipts. It does not acquire 180 packages again, rebuild an installer, publish a release or upload final production assets. Its expiring artifacts are receiving evidence only. Public source delivery requires the stated final approvals and gates.

Source archive: 2,003,416,952 bytes, SHA256 ed66bb813829fb2ddf8976d952006dac320a02bba623fa3f094a71eabaf92d03. Current application source contains its existing Switzer font resources; extra downloaded comparison fonts/full-family ZIPs are excluded. Prepared transport does not settle source-archive font permission.

No production merges, tags, releases, source offers, security changes or third-party outreach are performed here.
