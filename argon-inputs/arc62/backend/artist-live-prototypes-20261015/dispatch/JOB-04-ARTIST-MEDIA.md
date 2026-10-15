TASK: arc62-spawn-artist-media-004

REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\media\REPLY-ARC62-SPAWN-ARTIST-MEDIA-004.md

OWNED DIRECTORY (write code, tests, build outputs only here): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\media

PROMPT FILE (read-only after dispatch): C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\dispatch\JOB-04-ARTIST-MEDIA.md

Your distinct owner is media; no cross-owner edits.

Preflight Spawn Summon Lite worker task. This is implementation plus actual focused execution, NOT research or a proposal. Work on Laptop 1 using Brotherhood Desktop Commander (BDC), including local builds/tests. Never invoke Codex CLI, Claude Code CLI or any model CLI.

Current source READ ONLY: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum, branch codex/vinyl-live-listening, frozen starting SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. Preserve all dirty and untracked artist/QML edits; no source modifications, git commits, pushes, resets, deploys, provider logins or prod data. If the SHA moved, report exact evidence rather than silently switching.

Arc 62 source READ ONLY outside your owned directory: C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend; also read only relevant Arc 65 and artwork source contracts as needed. Current live Vinyl native integration exists (VinylProvider, VinylTorrentProvider, VinylLiveSession/World) but Artist data is still driven by tests/fixtures, so do not claim production integration.

Implement isolated native backend proof code, tests and build/run instructions ONLY in the exact owned directory below. Create/modify no files outside that directory, including agent rooms, global memory, git indexes and other workers' owned folders. Avoid dependency downloads or system writes. Use existing installed C++17/Qt and stdlib. No ZIP/archive.

Each expected code path is an actual fully implemented compileable prototype, not pseudocode/headers-only. Show decisive PASS/FAIL test output and negative controls. If a tool or compile blocks, still write code plus focused tests and a truthful blocker; don't falsely claim execution.

Report real source SHA, exact produced files, tests run and results, unverified boundaries, contract mismatches and adopter guidance. Write the exact canonical REPLY path atomically (temporary file then rename) and verify final bytes; start reply with TASK and STATUS (DONE, PARTIAL, BLOCKED). No more than 16 KiB reply. Work one focused ~30-minute slice and stop.

OWNER: artist-specific Videos and Live media metadata and playback-intent qualification ONLY. Do not implement catalogue, artwork, personal state or QML/bridge.

Read BF2-RELATED-VIDEO-CONTRACT.md, BF3-VIDEO-PLAYBACK-ADAPTER-CONTRACT.md and their existing prototype classes, current fixture ArtistMedia.js and source scripts/vinyl/provider.py. Differentiate canonical RecordingMbid/ArtistMbid from opaque ProviderId+ProviderItemId.

Implement injectable ArtistMediaQuery service with source-backed artist-specific video and live-performance result records, exact qualified artist/recording mapping, dedicated type OfficialMusicVideo/LivePerformance/AlternatePerformance/etc, stable dedup, deterministic ordering and typed Empty/Unavailable/Invalid, separate video thumbnail provenance, and safe actionable open/resolve intent (do NOT claim native video player works).

Never manufacture live performances or 'official' status based on fuzzy title similarity. Only qualified official mapped video may request direct audio-video switch; live performances remain separate explicit artist media item and open action.

Executable tests: unknown/no videos is empty, specific videos and live separate, unrelated artist/recording rejected, duplicate provider IDs, spoofed officiality/fuzzy results rejected, cancellation/stale results, exact action selection and failure/expiry boundaries. Prefer use BF2/BF3 types or clearly documented compatible reference port.

Do not call network, scrape providers, download media, or claim live mode integration.

Finish by writing your canonical REPLY and do not wait on the coordinator to direct further work.