TASK: arc62-spawn-artist-catalogue-001

REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\catalogue\REPLY-ARC62-SPAWN-ARTIST-CATALOGUE-001.md

OWNED DIRECTORY (write code, tests, build outputs only here): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\catalogue

PROMPT FILE (read-only after dispatch): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\dispatch\JOB-01-ARTIST-CATALOGUE.md

Your distinct owner is catalogue; no cross-owner edits.

Preflight Spawn Summon Lite worker task. This is implementation plus actual focused execution, NOT research or a proposal. Work on Laptop 1 using Brotherhood Desktop Commander (BDC), including local builds/tests. Never invoke Codex CLI, Claude Code CLI or any model CLI.

Current source READ ONLY: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum, branch codex/vinyl-live-listening, frozen starting SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. Preserve all dirty and untracked artist/QML edits; no source modifications, git commits, pushes, resets, deploys, provider logins or prod data. If the SHA moved, report exact evidence rather than silently switching.

Arc 62 source READ ONLY outside your owned directory: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend; also read only relevant Arc 65 and artwork source contracts as needed. Current live Vinyl native integration exists (VinylProvider, VinylTorrentProvider, VinylLiveSession/World) but Artist data is still driven by tests/fixtures, so do not claim production integration.

Implement isolated native backend proof code, tests and build/run instructions ONLY in the exact owned directory below. Create/modify no files outside that directory, including agent rooms, global memory, git indexes and other workers' owned folders. Avoid dependency downloads or system writes. Use existing installed C++17/Qt and stdlib. No ZIP/archive.

Each expected code path is an actual fully implemented compileable prototype, not pseudocode/headers-only. Show decisive PASS/FAIL test output and negative controls. If a tool or compile blocks, still write code plus focused tests and a truthful blocker; don't falsely claim execution.

Report real source SHA, exact produced files, tests run and results, unverified boundaries, contract mismatches and adopter guidance. Write the exact canonical REPLY path atomically (temporary file then rename) and verify final bytes; start reply with TASK and STATUS (DONE, PARTIAL, BLOCKED). No more than 16 KiB reply. Work one focused ~30-minute slice and stop.

OWNER: artist catalogue query/identity/pagination ONLY. Do not implement artwork, personal library, media playback, QML or bridge.

Read BG1-CATALOGUE-CLIENT-CONTRACT.md + existing bg1-catalogue-client-prototype classes and BA0 identity definitions; source scripts/vinyl/provider.py and VinylArtistData.qml are read-only contrasts. MusicBrainz ArtistMbid is canonical, source/browse IDs mapping/provenance. Artist discography returns release GROUPS, not duplicate individual editions.

Implement an injectable provider-neutral ArtistCatalogueQuery with typed Artist Found/NotFound/Redirected/Unavailable, exact ID/alias match (name collision fail closed), deterministic paginated release-group collection (albums, singles/EPs/etc), explicit continuation/cancellation/version identity, source-qualified recording/artist credits and typed sort/filter.

Live YouTube Music helper currently supports album-only search; this prototype should make the missing artist endpoint/adapter contract explicit and executable against a fake provider, NOT introduce network calls/credential flows or pretend a live server exists.

Test collision of two artists with same displayed name but distinct MBIDs, name lookup vs ID lookup, page boundaries/duplicate groups, different release statuses, cancelled/stale callbacks, missing/unavailable/offline, and recording credits. Provide CMake or commandline build harness that runs these assertions.

Prefer source-level reusable C++17/Qt types matching BG1; can implement a narrow adapter rather than reimplementing all BG1. Document how main Vinyl native provider would call this after separate adoption.

Finish by writing your canonical REPLY and do not wait on the coordinator to direct further work.