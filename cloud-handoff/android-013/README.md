> Publication update: read `PUBLICATION-AUTHORITY.md` first. Earlier preparation-status and permission statements are historical; the current user starting prompt controls the Cloud session.

# Colosseum Android: Claude Code Cloud receiving packet

Task `claude-android-cloud-013-20261020`. Prepared by Codex. **NOT PUBLISHED / NOT DISPATCHED / NO INTEGRATION.**

1. Read `HANDOFF.md` for the first milestone and acceptance/recovery gates.
2. Configure **Cloud > Colosseum** using `CLOUD-ENVIRONMENT.md` and the prepared lightweight `cloud-environment-setup.sh`. No local/Remote Control/BDC connection is required or assumed.
3. Use `PUBLISH-BASELINE.md` and `GITHUB-BASELINE-DELTA.zip` for the concrete isolated GitHub import after publication authorization. The proposed baseline branch is `codex/android-cloud-baseline-013-20261020`; it does not exist as a result of this preparation. The delta is based on exact upstream app SHA `fdd23f09e8b631f5c165e84d969895c99f520a28`, not current master.
4. Preserve `ANDROID-SOURCE-BASELINE.zip` as the exact Windows working-byte recovery backup. Run `python3 restore_source.py <new-directory>` to restore and verify it. All 4644 selected source files were actually restored and checked by Codex.
5. For the GitHub/Linux representation, run `python3 verify_source.py --manifest CLOUD-SOURCE-MANIFEST.json --app <actual-repo-root> --donor <handoff>/source-donor` before editing. Do not confuse this manifest with the raw Windows backup manifest.
6. Run `python3 restore_donor.py <new-working-donor>` for the hardened real Git donor, or use `--pristine` in a different new destination for the source pin. The shallow boundary and authentic tag are part of restoration. A plain clone of the bundle is insufficient. The final helper and actual patch-preparation verification were executed successfully.
7. After verified remote publication and Cloud setup, fill the actual import SHA into `START-PROMPT.txt` and send that prompt in the selected CLOUD session. No actual Claude session was started here.

Parent preparation evidence and boundaries: `PARENT-PREPARATION-REVIEW.md`; raw task012/other environment evidence is supplemental in the full private receiving packet. `DELIVERY-MANIFEST.json` hashes every delivered file. `GITHUB-PAYLOAD-MANIFEST.json` inventories the exact proposed source-transport paths. Neither an archive filename nor a local Git commit is proof Cloud can download/upload it: C0 must qualify the actual source checkpoint and large-APK return route.

The Android milestone covers reproducible packaging, correct fixture ground truth and actual native/Media3 motion, seeks, lifecycle and teardown. Cloud ABI/API/runtime evidence and later physical Nokia acceptance remain separate. No 2.0.0/2.0.1 integration, release or broader feature completion is claimed.
