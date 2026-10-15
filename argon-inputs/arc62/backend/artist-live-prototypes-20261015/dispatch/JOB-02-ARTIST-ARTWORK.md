TASK: arc62-spawn-artist-artwork-002

REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\artwork\REPLY-ARC62-SPAWN-ARTIST-ARTWORK-002.md

OWNED DIRECTORY (write code, tests, build outputs only here): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\artwork

PROMPT FILE (read-only after dispatch): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\dispatch\JOB-02-ARTIST-ARTWORK.md

Your distinct owner is artwork; no cross-owner edits.

Preflight Spawn Summon Lite worker task. This is implementation plus actual focused execution, NOT research or a proposal. Work on Laptop 1 using Brotherhood Desktop Commander (BDC), including local builds/tests. Never invoke Codex CLI, Claude Code CLI or any model CLI.

Current source READ ONLY: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum, branch codex/vinyl-live-listening, frozen starting SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. Preserve all dirty and untracked artist/QML edits; no source modifications, git commits, pushes, resets, deploys, provider logins or prod data. If the SHA moved, report exact evidence rather than silently switching.

Arc 62 source READ ONLY outside your owned directory: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend; also read only relevant Arc 65 and artwork source contracts as needed. Current live Vinyl native integration exists (VinylProvider, VinylTorrentProvider, VinylLiveSession/World) but Artist data is still driven by tests/fixtures, so do not claim production integration.

Implement isolated native backend proof code, tests and build/run instructions ONLY in the exact owned directory below. Create/modify no files outside that directory, including agent rooms, global memory, git indexes and other workers' owned folders. Avoid dependency downloads or system writes. Use existing installed C++17/Qt and stdlib. No ZIP/archive.

Each expected code path is an actual fully implemented compileable prototype, not pseudocode/headers-only. Show decisive PASS/FAIL test output and negative controls. If a tool or compile blocks, still write code plus focused tests and a truthful blocker; don't falsely claim execution.

Report real source SHA, exact produced files, tests run and results, unverified boundaries, contract mismatches and adopter guidance. Write the exact canonical REPLY path atomically (temporary file then rename) and verify final bytes; start reply with TASK and STATUS (DONE, PARTIAL, BLOCKED). No more than 16 KiB reply. Work one focused ~30-minute slice and stop.

OWNER: artist-specific hero/background artwork resolver ONLY. Do not implement catalogue, library, video, QML or bridge.

Read Arc62 research/ARTIST-ARTWORK-SOURCE-STRATEGY-2026-10-14.md (latest addenda including MF DOOM and The D.O.C.), Arc62 catalogue/ARTWORK-STRATEGY.md and existing VinylBackdrop.qml/read-only artist UI. Source candidates are distinct artist-specific photographs from TheAudioDB, Commons, Fanart etc, qualified through MusicBrainz MBID. Provider access is NOT commercial reuse rights.

Build injectable provider-transport-independent ArtistArtworkResolver in C++ (or Qt C++ if convenient) with normalized candidate/provenance metadata, dimension/aspect validation, dedup (source URL and stable media fingerprint where provided), explicit verified rights state vs unknown (unknown is not release approved), placeholder rejection, safe fallback to artist-specific composition, and robust selected list.

Return adaptive slideshow policy: 0 eligible -> no image/artist-specific composition, 1 -> static, 2 -> crossfade, >=3 -> rotating; no mandatory 4, no clones/tinted duplicates; optional local user override; deterministic order; saved cache metadata and safe failure fallback. Return only data/state for QML hero+full-page blur; DO NOT implement QML/GPU filtering.

Provide executable deterministic tests for 0/1/2/3+ candidates, mismatched MBID rejection, same-image dedupe, extreme aspect/too-small rejection, rights-unknown never treated as release-cleared, offline cache fallback and manual override. Do not fetch/redistribute copyrighted artwork in generated files.

Document native adoption against bg1 ArtistMbid and future Arc65 image cache; no provider credentials or live API calls in test.

Finish by writing your canonical REPLY and do not wait on the coordinator to direct further work.