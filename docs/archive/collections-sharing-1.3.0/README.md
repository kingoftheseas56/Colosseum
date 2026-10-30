# Collections sharing and gifting: archive for 1.3.0

Status: **paused by Hemanth on 2026-10-30**. This branch preserves the implementation and receiving work; it is not a release-ready or deployed feature.

## Release decision

- **1.2.0:** personal, private, cross-world Your Collections.
- **1.3.0:** hosted share links, anonymous browser viewing, gifting, retained gift editions and recipient copies, after simplifying the sharing UI.

Do not merge this feature branch or migration 0011 into 1.2.0. Feature implementation remains paused; creating this online archive does not resume it.

## Preserved work

The branch contains Dots' original commits, the merge into the then-current master base, and the Windows journal and wallpaper fixes. The following source commits are reachable from this archive:

| Checkpoint | Commit |
| --- | --- |
| Master base used for integration | `02cdf699ff08da90214d7e738a4a3c1eae25fb0f` |
| Dots final delivery | `ab455a1b18e1732e3f0d76a930e092ebef1f65ca` |
| Integration merge | `2683c1fb7f796a9ff5500b201f5fd753f029ed19` |
| Final product-code checkpoint | `5a0fdac784a372103bdf8421219968fa49e33de5` |

The archive commit adds only this documentation directory. Product source remains exactly at the final code checkpoint. Original delivery ZIP SHA256: `aed42e684932c5050350a5600d4b3d721d9cb57e2caaa3a995ffc80f808209ba`; its files and bundle were verified during receiving. The original ZIP, incremental Git bundle, full local evidence and disposable profiles are also retained locally. The online branch preserves the Git source/history and the portable summaries here; it does not upload local profiles, raw runtime logs, credentials, caches or build binaries.

- [Verification and remaining work](VERIFICATION.md)
- [Proposed simpler sharing UI](UI-REVISION-PROPOSAL.md)
- [Historical guarded migration review](migration-0011-guarded-review.sql)
- [Existing API contract](../../../server/account-service/COLLECTIONS_PUBLICATIONS.md)

The implementation extends the existing native collection store, identity registry, account controller/service, and QML Collections destination. It includes explicit ordinary publication updates, immutable gift editions, optional passphrases, browser receiving, offline Keep Gift, independent Make My Own Version, owner isolation and revocation. Private edits do not automatically update hosted content. No automatic Library additions, downloads or progress changes are intended.

## Resume for 1.3.0

1. Obtain Hemanth's instruction to resume. Inspect the live branch, source and current master before changing anything.
2. Preserve or merge later master changes. At pause, window-controls commit `08faed84d3e28b10377c46e3b4984f7974d339b0` had **not** been merged into this branch. It overlaps Main.qml and CollectionsPanel.qml.
3. Simplify the rejected sharing popup using the proposal as a starting point. Its redesign was **not implemented**. Reuse Colosseum's existing Theme, Glass and control primitives.
4. Resolve the full database package's migration-context timeouts and finish receiving/runtime acceptance against the eventual combined source. Keep earlier passing narrow checks distinct from that failure.
5. Coordinate an explicitly approved production migration and deployment only after final acceptance. Recheck live hosting/database state and the actual production target; do not infer it from this archive.

Preserve existing recording/test profiles and unrelated worktrees/apps. Do not reset, clean, stash, force-push or overwrite another lane to resume. Laptop 1 Brotherhood Desktop Commander remains prohibited without Hemanth's explicit permission.

## Production boundary

This work did not apply the production migration or trigger a production deployment. The migration was rehearsed on an isolated production clone; a temporary Neon rehearsal branch remains retained, with its exact identity in local rollout records. The Neon delete tool requires explicit user confirmation before cleanup.

The current old and new server versions require different exact migration histories. The existing Render Free service has no pre-deploy hook, so coordinate build/promotion and migration; a brief readiness interruption is possible. Do not weaken schema gates. After schema 11, recovery must use schema-11-compatible code or a forward fix rather than an incompatible schema-10 binary. These are historical constraints to reverify when resuming, not authorization to deploy.
