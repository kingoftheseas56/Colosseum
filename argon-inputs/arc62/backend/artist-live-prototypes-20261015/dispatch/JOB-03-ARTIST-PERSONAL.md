TASK: arc62-spawn-artist-personal-003

REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\personal\REPLY-ARC62-SPAWN-ARTIST-PERSONAL-003.md

OWNED DIRECTORY (write code, tests, build outputs only here): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\personal

PROMPT FILE (read-only after dispatch): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\dispatch\JOB-03-ARTIST-PERSONAL.md

Your distinct owner is personal; no cross-owner edits.

Preflight Spawn Summon Lite worker task. This is implementation plus actual focused execution, NOT research or a proposal. Work on Laptop 1 using Brotherhood Desktop Commander (BDC), including local builds/tests. Never invoke Codex CLI, Claude Code CLI or any model CLI.

Current source READ ONLY: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum, branch codex/vinyl-live-listening, frozen starting SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. Preserve all dirty and untracked artist/QML edits; no source modifications, git commits, pushes, resets, deploys, provider logins or prod data. If the SHA moved, report exact evidence rather than silently switching.

Arc 62 source READ ONLY outside your owned directory: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend; also read only relevant Arc 65 and artwork source contracts as needed. Current live Vinyl native integration exists (VinylProvider, VinylTorrentProvider, VinylLiveSession/World) but Artist data is still driven by tests/fixtures, so do not claim production integration.

Implement isolated native backend proof code, tests and build/run instructions ONLY in the exact owned directory below. Create/modify no files outside that directory, including agent rooms, global memory, git indexes and other workers' owned folders. Avoid dependency downloads or system writes. Use existing installed C++17/Qt and stdlib. No ZIP/archive.

Each expected code path is an actual fully implemented compileable prototype, not pseudocode/headers-only. Show decisive PASS/FAIL test output and negative controls. If a tool or compile blocks, still write code plus focused tests and a truthful blocker; don't falsely claim execution.

Report real source SHA, exact produced files, tests run and results, unverified boundaries, contract mismatches and adopter guidance. Write the exact canonical REPLY path atomically (temporary file then rename) and verify final bytes; start reply with TASK and STATUS (DONE, PARTIAL, BLOCKED). No more than 16 KiB reply. Work one focused ~30-minute slice and stop.

OWNER: artist saved state and artist-created playlists ONLY. Do not implement artwork, catalogue, media or QML/bridge.

Read existing BA5-LIBRARY-PLAYLISTS-CONTRACT.md and ba5-library-playlists-prototype, live native CollectionStore and VinylLiveSession.qml, and the fixture-only savedNames/customPlaylists in VinylArtistData.qml. Preserve canonical ArtistMbid/RecordingMbid identity, not title strings or untrusted provider IDs.

Implement a compact QtCore or standalone C++17 persistent ArtistPersonalState prototype with stable profile partitioning, saved artist toggle/find, ordered user-created playlist names and canonical item IDs, minimal metadata, transactional/atomic read-write (QSaveFile if using Qt), schema version and migration/fail-closed corruption outcomes. Do not duplicate Colosseum profile database for production; implement an independently testable adapter/port that can later bind BA5/Collection.

Test save/unsave/restart, empty or invalid MBID rejection, dedup, playlist order/mutations, independently isolated profiles, corrupt/truncated data (never silently overwrite valid data), unknown versions, and save while catalogue unavailable with display snapshots non-authoritative.

Provide executable harness and evidence; no production state or personal user files read or modified (test within isolated owned directory/temp fixture).

Finish by writing your canonical REPLY and do not wait on the coordinator to direct further work.