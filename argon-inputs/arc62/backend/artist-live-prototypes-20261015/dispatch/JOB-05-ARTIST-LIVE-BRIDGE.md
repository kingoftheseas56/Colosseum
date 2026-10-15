TASK: arc62-spawn-artist-live-bridge-005

REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\bridge\REPLY-ARC62-SPAWN-ARTIST-LIVE-BRIDGE-005.md

OWNED DIRECTORY (write code, tests, build outputs only here): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\bridge

PROMPT FILE (read-only after dispatch): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\dispatch\JOB-05-ARTIST-LIVE-BRIDGE.md

Your distinct owner is bridge; no cross-owner edits.

Preflight Spawn Summon Lite worker task. This is implementation plus actual focused execution, NOT research or a proposal. Work on Laptop 1 using Brotherhood Desktop Commander (BDC), including local builds/tests. Never invoke Codex CLI, Claude Code CLI or any model CLI.

Current source READ ONLY: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum, branch codex/vinyl-live-listening, frozen starting SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. Preserve all dirty and untracked artist/QML edits; no source modifications, git commits, pushes, resets, deploys, provider logins or prod data. If the SHA moved, report exact evidence rather than silently switching.

Arc 62 source READ ONLY outside your owned directory: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend; also read only relevant Arc 65 and artwork source contracts as needed. Current live Vinyl native integration exists (VinylProvider, VinylTorrentProvider, VinylLiveSession/World) but Artist data is still driven by tests/fixtures, so do not claim production integration.

Implement isolated native backend proof code, tests and build/run instructions ONLY in the exact owned directory below. Create/modify no files outside that directory, including agent rooms, global memory, git indexes and other workers' owned folders. Avoid dependency downloads or system writes. Use existing installed C++17/Qt and stdlib. No ZIP/archive.

Each expected code path is an actual fully implemented compileable prototype, not pseudocode/headers-only. Show decisive PASS/FAIL test output and negative controls. If a tool or compile blocks, still write code plus focused tests and a truthful blocker; don't falsely claim execution.

Report real source SHA, exact produced files, tests run and results, unverified boundaries, contract mismatches and adopter guidance. Write the exact canonical REPLY path atomically (temporary file then rename) and verify final bytes; start reply with TASK and STATUS (DONE, PARTIAL, BLOCKED). No more than 16 KiB reply. Work one focused ~30-minute slice and stop.

OWNER: backend QML-facing Artist service/adoption seam ONLY. Other workers independently implement catalogue, artwork, saved state, and media, so do not edit/read their in-progress work or couple to their unfinished headers. Build an injectable adapter/interface proof that can consume those later.

Read Arc62 backend README/BH1 discussion and BG1/BA5/BF2 interface laws. In frozen LIVE branch inspect native/vinyl/VinylProvider.h, VinylTorrentProvider.h, qml/vinyl/VinylLiveSession.qml, VinylLiveWorld.qml, VinylArtistData.qml, VinylArtistCatalogue.js and qml/Main.qml. The live path has no production Artist navigation; Artist prototype is fixture driven; leave all app files untouched.

Implement a QtCore QObject QML-facing ArtistServiceBridge reference with Q_INVOKABLE requestArtist, requestDiscography(cursor,filter), requestMedia, requestArtwork, saved artist read/toggle and select exact recording/release via existing injected playback-command port. Typed request state (Loading/Ready/Empty/NotFound/Unavailable/Error), source identity, profile boundary and cancellation/generation. Do not import test fixture data in bridge or pretend provider.py supports artist endpoints.

Prevent stale replies after artist-switch, profile-switch and return navigation, preserve browsing vs active playback state, reject mismatched artistId and stale continuation, enforce selected recording/release strong identifier and clear error with retry. Bridge should not know how artwork is blurred or how tabs are drawn.

Provide focused Qt6 Core/Test harness with fake adapters that tests request generation/cancellation, profile independence, browse does not stop a playing song, exact selection maps to playback-port call, typed empty/offline, and no unsafe cross-profile state. Provide CMake build and runnable receipts.

Deliver interface and executable tests; no production integration or QML modifications.

Finish by writing your canonical REPLY and do not wait on the coordinator to direct further work.