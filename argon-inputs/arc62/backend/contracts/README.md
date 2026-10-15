# Vinyl Backend

Status: active mock-agnostic backend foundation
Date: 2026-10-06
Owner: Arc 62 Vinyl

**2026-10-15 Artist backend prototypes (isolated; not production):** Five Preflight Spawn workers implemented executable standalone Artist catalogue, artwork, profile-scoped personal state, Videos/Live media and Qt/QML service bridge under `artist-live-prototypes-20261015/`. Each has source, focused tests and canonical worker reply. Independent receiving verified **5/5 native CTest suites pass**, with no Colosseum source changes. See `artist-live-prototypes-20261015/REVIEW-ARC62-ARTIST-BACKENDS-20261015.md` and `dispatch/BATCH-VERIFY.txt`. Real provider adapters, rights-qualified artwork, production service wiring, Argon QML adoption and full-app runtime tests remain open.

## Doctrine

Vinyl backend contracts describe product capabilities and durable state, not a particular mock or future QML composition.

HTML/QML mocks are consumers and design references. A carousel, rail, card, drawer, page, or current label may disappear without forcing a backend rewrite.

Shared backend law:

- MusicBrainz owns canonical music identity.
- Colosseum owns personal state and final policy.
- Provider IDs remain mappings/provenance.
- QML paints; native/backend code owns durable decisions.
- Parallel lanes add lane-local types instead of casually mutating shared domain types.

## Current gate

**BA0 complete locally:** `BA0-DOMAIN-CONTRACT.md`

Reference implementation and deterministic native harness:
`ba0-domain-prototype/`

**BA1 complete locally:** `BA1-QUEUE-CORE-CONTRACT.md`

Reference implementation and deterministic native harness:
`ba1-queue-core-prototype/`

**BA2-A complete locally:** `BA2-PLAYBACK-SESSION-CONTRACT.md`

Reference implementation and deterministic native harness:
`ba2-playback-session-prototype/`

**BA2-B complete locally:** `BA2B-ENGINE-ADAPTER-CONTRACT.md`

Reference implementation and deterministic fake-engine harness:
`ba2-engine-adapter-prototype/`

**BA2-C complete locally:** `BA2C-QUEUE-SESSION-COORDINATION-CONTRACT.md`

Reference implementation and deterministic queue/session harness:
`ba2c-queue-session-coordination-prototype/`

**BA2 complete locally through BA2-D:** `BA2D-MPV-ADOPTION-CONTRACT.md`

Reference tokenized player-bridge prototype and supporting raw-mpv identity evidence:
`ba2d-mpv-adoption-prototype/`
`BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md`

BA2-D-B1 raw API qualification, BA2-D-B2 playlist-entry attribution, and BA2-D-B3 exact seek-completion attribution are all verified. The final clean BA2-D qualification passes 4/4 registered tests.

Production Colosseum adoption has not happened for BA0, BA1, or BA2.

BA0 freezes the vocabulary required to fan out safely:
`ArtistMbid`, `ReleaseGroupMbid`, `ReleaseMbid`, `ReleaseTrackMbid`,
`RecordingMbid`, `WorkMbid`, `CanonicalEntityRef`, provider identity/mappings,
and `ReleaseTrackCoordinate`.

BA1 freezes occurrence identity, queue ordering, active-cursor semantics, Play Now/Play Next insertion, exact-occurrence mutation, active-removal continuity, and non-wrapping advance semantics.

BA2-A freezes the playback lifecycle state machine and the exact `QueueOccurrenceId + generation` fence for asynchronous work. BA2-B freezes the replaceable engine-neutral command/callback seam. BA2-C freezes deterministic BA1 queue / BA2 session coordination. BA2-D verifies the isolated Colosseum/mpv bridge, exact playlist-entry attribution, and exact seek-completion attribution over real libmpv events. FIFO issued-token attribution, URL identity, queue-index identity, and mutable-current-token guessing are explicitly rejected.

## Slice map

### BA - core state

- **BA0 Domain Contract** - shared canonical identity/provider vocabulary.
- **BA1 Queue Core** - complete locally; queue occurrence identity and deterministic queue mutations.
- **BA2 Playback Session Lifecycle** - complete locally through BA2-D; lifecycle, engine-neutral seam, queue/session coordination, playlist-entry attribution, and exact seek-completion attribution verified.
- **BA3 Session Persistence** - durable queue/session snapshot and restore.
- **BA4 Listen Event Ledger** - starts, pauses, resumes, seeks, skips, stops, completions, source changes, listening-time facts.
- **BA5 Library + Playlists** - saved canonical entities and ordered playlist state.

### BB - sources and exact resolution

- **BB1 Source Candidate Contract** - provider-neutral candidate discovery/provenance.
- **BB2 Album Manifest + Exact Track Resolver** - fail-closed mapping from requested release/recording context to playable source.
- **BB3 CUE / Whole-Album Sources** - virtual tracks over one physical source.
- **BB4 mpv Playback Adapter** - selected/resolved source into Colosseum's mpv path.

### BC - timed text and meaning

- **BC1 Timed Text Core** - plain/line/word timing, offsets, seek, resync, provenance.
- **BC2 Translation Layer** - original plus translation on the same timing grid.
- **BC3 Annotation / Meaning Plane** - provider-neutral referents/annotations and lazy bodies.

### BD - audio observability

- **BD1 Signal Path Model** - source facts -> requested processing -> conversion -> selected output -> observed result.
- **BD2 mpv / Device Observability** - populate Signal Path from real runtime evidence.

### BE - recommendations

- **BE1 Recommendation Candidate Contract** - provider-neutral MBID-keyed candidates, scores, evidence, provenance.
- **BE2 Feedback + Taste State** - local positive/negative signals, hides, snoozes, do-not-recommend state.
- **BE3 Deterministic Vinyl Ranker** - catalogue graph + population priors + local taste + diversity/fatigue + explanations.
- **BE4 Radio / Autoplay Sessions** - candidate sequences feeding the normal queue contract.

### BF - context and video

- **BF1 Context Graph** - credits, works, versions, covers and canonical relationships.
- **BF2 Related Video Mapping** - Recording MBID to qualified video candidates and evidence.
- **BF3 Video Playback Adapter** - selected video candidate -> yt-dlp -> mpv, without replacing Recording identity.

### BG - catalogue client and discovery planes

- **BG1 Catalogue Client Contract** - Artist/Release Group/Release/Recording browse, search and pagination against Arc 65's eventual serving corpus.
- **BG2 Discovery Enrichment Contract** - popularity, new releases, editorial collections, regional discovery and other noncanonical browse signals.

### BH - presentation boundary

- **BH1 Native Vinyl Service Facade** - stable C++ query/command/event surface consumed by QML after underlying contracts settle.

BH1 deliberately comes late. It must not fossilize temporary UI assumptions while the domain is still moving.

## Parallelization

BA0 is the fan-out gate. **Wave 1 is complete locally:**

```text
BA1 Queue Core
BB1 Source Candidate Contract
BC1 Timed Text Core
BD1 Signal Path Model
BG1 Catalogue Client Contract
```

**Wave 2 is complete locally:**

```text
BA2 Playback Session
BA5 Library / Playlists
BB2 Exact Resolver
BC2 Translation
BC3 Annotation
BD2 mpv Observability
BE1 Recommendation Candidate Contract
BF1 Context Graph
BF2 Related Video Mapping
BG2 Discovery Enrichment
```

**Wave 3 is complete locally and verified:**

```text
BA3 Session Persistence
BA4 Listen Event Ledger
BB3 CUE / Whole-Album Sources
BB4 mpv Playback Adapter
BE2 Feedback + Taste State
BF3 Video Playback Adapter
```

The post-Wave-3 Claude architecture/product review is complete and reconciled.

Claude's central finding is accepted: isolated lanes are strong, but the cross-lane Play journey and several durable/orchestration seams were missing. The old BE3-first four-slice Wave 4 is superseded.

Wave 4 authority:

- `WAVE4-CLAUDE-RECONCILIATION.md`
- `WAVE4-AGENT-ROOM.md`

Wave 4 now has nine slices:

```text
1 W4-A Playback Window + Recording-Time Model
2 W4-B Coordinator Repair + Transition Facts
3 W4-C Source Resolution Session
4 W4-D Queue + Session Schema v2
5 W4-E Durable Personal State
6 W4-F Catalogue + Timed-Text Identity
7 W4-G Recommendation Inputs + BE3
8 W4-H Superbuild + End-to-End Play Journey
9 W4-I Reference Backend Freeze
```

Initial parallel launch group: 1, 4, 5, 6.

The real seal is W4-H's composed Play journey plus W4-I's evidence-backed freeze audit. BE4 Radio / Autoplay and the full BH1 QML-facing facade remain deferred.

## Shared-type change rule

A downstream lane may request a BA0 change only when:

1. it has a concrete requirement;
2. an additive lane-local type cannot express it cleanly;
3. compatibility with existing BA0 consumers is reviewed;
4. the BA0 harness is extended and remains green.

This is the coordination barrier that keeps parallel backend work from collapsing into one mutable shared header.
