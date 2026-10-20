# Codex preparation review — 2026-10-20

Verdict: **SOURCE/HANDOFF PREPARED AND RECOVERY VERIFIED. NOT PUBLISHED, NOT DISPATCHED, NO INTEGRATION.**

This packet is for Anthropic-hosted Claude Code Cloud using GitHub source transport. It grants no assumed access to Hemanth's machines, SSH, BDC or Nokia. Cloud > Colosseum is the recommended environment selection, subject to checking its actual configuration. Default has no Android project setup of its own.

## Personally verified during preparation

- Laptop2 SSH identity `desktop-3k414a9\admin` before read/export work. Original app/donor HEADs remained pinned at `fdd23f09e8b631f5c165e84d969895c99f520a28` / `99a18601fa12858cc8c62576dc72b87edbd81b9a`. Snapshot script checked source hashes twice and compared repository identity, selected index state and full status before/after. No authoritative source edits, staging or commits were performed.
- Full recovery ZIP is 219332211 bytes, SHA256 `920d3e72f4e6d641def87d683ed63e4ae9d82f0bfe66f9190cfe9274543205bf`. Exact raw Windows working bytes for 4495 app and 149 donor files, including modified/new authored code, assets and licenses. Generated build/SDK/cache output, credentials, private configuration and continuity were excluded explicitly. Metadata-only scan found only an ignorable Python bytecode cache under the examined ignored-source roots.
- Executed the supplied `restore_source.py` into a fresh parent-owned directory. Result: 4644 checked, zero mismatches, PASS. This was an actual restoration, not an archive-list claim.
- Generated separate Cloud Git-source manifest from real upstream Git blobs for unchanged app files and preserved exact dirty/new source. Required all 3383 Windows-checkout differences to be line-ending-only. Did not overwrite unrelated source with a mass line-ending conversion. GitHub publication still must verify the imported commit and fresh checkout against this manifest.
- Donor source is shallow. Initial ordinary bundle-clone restoration FAILED with a missing parent; no claim of complete Git history is valid. Final recovery includes the authenticated annotated v0.1.5 tag, original shallow boundary, exact source pack, scoped Git filter settings and original intent-to-add visibility for the new integrity test. Pack import initially exposed a Python buffered-stdin offset error; final helper passes the remaining pack bytes correctly. Failed attempts/logs were retained outside authoritative repositories.
- Ran final `restore_donor.py` into a second fresh receiving directory; 149 exact overlay files verified. `git fsck --full` exited zero. Actual `prepare_nuvio_engine.py --source <restored-donor> --verify-only` exited zero and proved the vendored patch applies to a pristine exact pin. No artifact/stamp gate was weakened.
- Ran actual backend contracts against restored app plus a real reconstructed donor Git checkout. Initial recovery attempts exposed a patch-diff visibility mismatch; preserving scoped clean filters and intent-to-add fixed recovery metadata. Final result: 31 executed, zero skipped, zero failures, exit zero.
- Python helper syntax was checked. The cloud Bash bootstrap is prepared but has not been executed in Claude Cloud. No Android compile, emulator test, APK install or physical-device test was performed in this preparation.

## Included evidence and scope

Current independent task012 receiving review takes precedence over GLM's rejected COMPLETE claim. Historical accepted startup and task008 build reviews are included at their explicit artifact scope. Original task012 captures/logs, synthetic fixture and historical seeder/tracker scripts are included for diagnosis, not accepted as working Cloud services. Separate x86_64 emulator-smoke packet belongs to another source revision/environment and cannot qualify this ARM64 candidate.

The first milestone has C0 environment/source/return-transport qualification; C1 reproducible whole-runtime packaging and fixture ground truth; C2 progression/seek/surface/teardown diagnosis and supported Cloud-runtime evidence; C3 exact artifacts/source restoration and cleanup. Codex owns review and later physical Nokia receiving. No milestone is a blanket 2.0.0/2.0.1 or release acceptance.

## Remaining gates

- Authorize and perform the exact isolated GitHub source import/publication, preserving main and preventing unintended CI. The prepared publication payload is reviewable; no branch has been created/pushed by this preparation.
- Record verified remote branch/import SHA and actual Claude environment configuration. Replace the launch prompt's NOT PUBLISHED placeholder only with that verified receipt.
- Qualify Cloud toolchain installation, dependency access, emulator/ABI capabilities, durable source/APK return route and checkpoint round trip. Neither official documentation nor another agent's cloud result establishes these facts in this environment.
- Review Claude's exact source/APK/test return, then conduct separate controlled Nokia qualification before integration. BDC remote access is optional future infrastructure, not an assumed prerequisite or a currently qualified route.

Evidence logs: `evidence/preparation/`. Full GitHub payload path/hash inventory: `GITHUB-PAYLOAD-MANIFEST.json`. Packet file checksums: `DELIVERY-MANIFEST.json`.
