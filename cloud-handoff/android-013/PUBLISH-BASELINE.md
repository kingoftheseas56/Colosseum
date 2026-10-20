> Publication update: read `PUBLICATION-AUTHORITY.md` first. Earlier preparation-status and permission statements are historical; the current user starting prompt controls the Cloud session.

# Concrete GitHub source preparation — not yet authorized for publication

Target repository: https://github.com/kingoftheseas56/Colosseum

Proposed NEW source-transport branch: `codex/android-cloud-baseline-013-20261020`.
Proposed NEW Claude implementation branch: `claude/android-stabilization-013-20261020` (or an actual Cloud-created isolated branch recorded before work).
Upstream app starting SHA: `fdd23f09e8b631f5c165e84d969895c99f520a28`.

No GitHub write, branch creation/push, commit, CI, environment change, Cloud dispatch or integration has been performed in preparing this handoff. Source ZIPs are local recovery/transfer artifacts. The requested handoff is prepared; the source transport remains a separate concrete gate.

## After Hemanth authorizes this exact isolated source publication

1. Use a NEW disposable staging clone/worktree, not either authoritative dirty Laptop2 repository or Hemanth's original checkout. Verify remote identity and starting commit. Ensure the new branch does not already exist remotely; never force-push or reuse an unrelated branch.
2. Restore the preserved full source archive into a separate fresh directory and verify all 4644 file hashes. Separately verify `GITHUB-BASELINE-DELTA.zip` against `DELIVERY-MANIFEST.json`.
3. Create the source-transport branch from the exact upstream app SHA. Safely extract the delta ZIP into that fresh app checkout. It changes only the preserved eight tracked app files, adds the twenty preserved untracked authored files, and adds `cloud-handoff/android-013/`. The payload's full exact paths are in `GITHUB-PAYLOAD-MANIFEST.json`; inspect actual counts there rather than assuming this description remained current.
4. Verify the WHOLE app working tree against `CLOUD-SOURCE-MANIFEST.json`, not the Windows backup manifest. This Cloud manifest preserves exact upstream Git blobs for unchanged app files and exact captured bytes for dirty/new files. The 3383 differing unchanged Windows checkout files were checked to differ only in line endings; every transformation is enumerated, never hidden. Do not overlay the Windows copy across the whole fresh checkout: that would create thousands of irrelevant line-ending changes. Preserve imported dirty/new bytes with per-command `git -c core.autocrlf=false add ...`; do not change global Git settings. Avoid excluded machine/private configuration. Verify raw bytes before staging and inspect staged blob correspondence afterward.
5. Materialize donor source at `cloud-handoff/android-013/source-donor`; verify all 149 files. Preserve pristine pin bundle, manifest and working donor snapshot as distinct artifacts.
6. Inspect every staged path and diff; exclude `.mcp.json`, private continuity/config, keys, profiles, generated build/cache/APKs. Inspect existing workflow triggers and use an explicit `[skip ci]` commit message for this transport-only branch. If any enabled event would still run CI, stop publication and resolve that exact gate first rather than disabling repository-wide workflows. No GitHub Actions dispatch is authorized.
7. Create the isolated import commit only in that staging clone, record its SHA and source tree identity. Verify a fresh checkout of that commit against the CLOUD manifest; new files/patches and signed fixtures must survive without normalization drift. Push only this NEW branch after the stated authorization; verify the remote ref equals the local import SHA. Record `GITHUB-DELIVERY-RECEIPT.json` (repository, branch, upstream/import SHAs, manifest/archive hashes and UTC time). Main must remain unchanged.
8. Configure Cloud > Colosseum using `CLOUD-ENVIRONMENT.md`. Attach the repository and select the published source branch. Authorize only the separate named worker branch for Cloud checkpoints if Hemanth approves that return route; never infer main push authority from repository access.
9. Give Claude `START-PROMPT.txt` with the actual import SHA filled in. C0 must verify that actual source, cloud environment and checkpoint-return route before implementation. No laptop, SSH, BDC or physical Nokia connection is part of this setup.

## What Codex will review after Claude returns

Review the new delta against this imported candidate, not against remote master or an old pristine donor; verify exact APK/runtime/engine/source hashes and restore the returned checkpoint. Separate Cloud build, Cloud emulator and fresh physical Nokia results. Accept/return corrections at the actual achieved scope. No integration, release or main merge follows automatically.
