# Vinyl backend review — 2026-10-20

## Decision

**Adopt the existing backend after bounded repairs and compatibility work.** The authored backend is substantially more complete than the first roadmap conveyed. It includes working domain algorithms, lifecycle state machines, codecs, query/bridge implementations and composed journeys. Rebuilding those capabilities would duplicate existing work.

It is not an already sealed, connected production backend. The full native/QML facade, concrete provider/device/transport adapters, several planned extensions and current-target qualification remain open. There are also source-level defects to repair before adopting their affected components.

This review supersedes the initial inventory-only assessment and informs the revised [development roadmap](../plans/2026-10-20-vinyl-development-roadmap.md).

## Scope and evidence

The reviewed root is the existing Arc 62 `backend/` artifact directory supplied by Hemanth. All backend-relative references below resolve there. App references resolve in Colosseum at implementation base `c059c6238ed8bd2b226a7606fd4a9f2cb039de73`; the roadmap commit and four pending availability edits do not change that implementation assessment.

- **32 source units:** 30 component directories, the W4 end-to-end sources and the W4 superbuild.
- **178 authored code/test/fixture/build files** enumerated and reviewed, with no uncovered file in the source coverage check.
- **246 unique reviewed files** including technical contracts, reports and retained evidence. Inventory totals 67,354 physical lines; this is not a claim that every line has a correctness proof.
- SHA-256 values supplied by the domain reviews were checked against current source during consolidation. The portable [coverage inventory](2026-10-20-vinyl-backend-coverage.json) records exact paths, hashes and review modes.
- **14 root historical build directories** were inventoried separately. Component generated trees add 149 core and 474 Artist-generated files. Generated sources, objects and binaries were not treated as authored implementation or fresh verification.
- Preflight agent instructions/definitions, prepared prompts/dispatch, agent rooms, memory/capabilities and operating material were excluded under Hemanth's restriction. Technical contracts and implementation/evidence reports were treated as task evidence only.
- **No backend builds, tests, provider calls or runtime probes were executed in this review.** Implementation bodies and test assertions were inspected; raw retained receipts were examined where relevant. Implementation/test inspection modes are recorded rather than claiming exhaustive line-by-line proof.

## What already exists

| Unit | Implemented capability | Adoption boundary |
|---|---|---|
| BA0 | Distinct artist/release-group/release/release-track/recording/work MBIDs, canonical refs, profile/play IDs, provider mappings, release coordinates | No opaque local-track identity variant; map to the host's existing local identity without inventing MBIDs. |
| BA1 | Occurrence-safe queue, order/cursor, Play Now/Next, insert/move/remove/clear, seeded shuffle and remembered order | Adopt into one native session owner; preserve existing app behavior. |
| BA2 lifecycle | Pure lifecycle, exact occurrence+generation fencing, play/pause loading intent, latest seek sequence, terminal failures | Logical state is not evidence of audible output. |
| BA2 engine adapter | Resource/header/window validation, physical/track/recording time transforms, integer-ms command adapter | Concrete command rejection/failure and timeout exits need completing. |
| BA2 coordinator | Queue/session invariant, waiting input, transport replacement, transition facts, EOF repeat Off/Queue | Repeat-one, previous and manual wrap parity need adapting from the current app. |
| BA2 mpv adoption | Abstract player bridge, per-file windows, playlist-entry ledger, serialized physical seek state, real libmpv probes | Attach to the existing mpv core; the full app does not yet use this reference strand. |
| BA3 | Profile-bound session/queue codec, v2 writer/v1 reader, shuffle/origin/display/resume intent | Codec only; adapt to current Progress transactions and migrate existing app checkpoints. Pending replacement/resume mismatch needs repair. |
| BA4 | Factual listen ledger/recorder, monotonic qualified time, coalesced spans, Failure versus Stop, continuation identity, v3 codec | Integrate with existing Activity/history retention and privacy; do not create another consumption total. |
| BA5 | Saved Artist/ReleaseGroup/Recording membership, ordered playlist occurrences, display snapshots, v3 codec/v1-v2 reads | Adapt through Collection. It is not a second production personal-state database. |
| BB1 | Provider-neutral candidate identities, capabilities, provenance, known facts and deterministic candidate sets | Actual candidate discovery remains an adapter responsibility. |
| BB2 | Fail-closed exact manifest matching: canonical evidence, release position, structured text/duration and conservative filenames | Acquire and probe real manifests; retain the admitted file/occurrence through resolution. Do not rewrite this matcher. |
| BB3 | Single-FILE CUE parsing, bounds/overflow checks, virtual tracks and exact BB2 slice admission | Read real CUE/resources and apply player bounds. Multi-FILE and audible gapless are outside the implemented proof. |
| BB4 | Validated resolved local/HTTP transport, headers, freshness/expiry, expected Recording and playback windows | No fetching or fallback here; adapt the existing engine/transport. |
| BB5 | Selection policy, quality/provider/pin/exclusion logic, request fencing, coordinator replacement, finite fallback and expiry refresh | Concrete resolution adapters and several profile/refresh/failure boundary repairs remain. |
| BC1 | Plain/line/word timed documents, revision identity, offsets, lookup and occurrence fencing | No lyrics acquisition/parser/cache or live clock bridge. Fix source-aware seek calibration. |
| BC2 | Partial translation documents on the original timing grid, source-document binding and result fencing | No translation engine, language detection or service/cache implementation. |
| BC3 | Normalized anchors/referents, document/cue binding, plural and lazy annotation bodies | No content fetcher, sanitizer or update persistence. Referent identity and anchor integrity need hardening. |
| BD1 | Conservative source/requested/observed/device signal model and verdicts | Does not apply processing or control an audio device. |
| BD2 | Normalization of supplied decoder/output/device evidence | No live mpv subscription or OS endpoint adapter; requested options do not prove physical output. |
| BE derivation | Pure versioned listening interpretation, aggregates, continuation semantics and separate exposure records/log | Adapt factual app history, add durable exposure ownership and actual display events. |
| BE1 | Artist/ReleaseGroup/Recording candidates with provenance, contribution evidence, finite raw scores and normalization | No recommendation generators or source-availability admission. |
| BE2 | Profile taste, explicit feedback, hide/snooze/DNR, revised signals and persistence | Use existing profile ownership; exact-entity suppression is the current implemented scope. |
| BE3 | Deterministic calibrated ranking, history/taste/graph support, exploration, exposure fatigue, diversity and explanations | No generators/service/radio. Numeric and unknown-relationship handling need repair; product eligibility belongs upstream. |
| BF1 | Canonical relationship graph, roles, versions, evidence merging and preserved unsupported kinds | No relationship acquisition; structural normalization needs hardening. |
| BF2 | Qualified Recording-to-video candidates, evidence strength and official switch eligibility | No video search or independent provider/evidence verification. |
| BF3 | Qualified selection converted to BB5 requests and coordinated source replacement | Resolver is a pure virtual port with fake tests; no real yt-dlp wrapper exists in this component. |
| BG1 | Typed catalogue interface and fully functioning deterministic fixture client: redirects, queries, cursors and failures | The only concrete client is fake. A live corpus/provider service is not supplied by these types. |
| BG2 | Validated regional/editorial/discovery snapshots and reproducible ordering/freshness rules | No actual enrichment feed; personalization stays with BE. |
| Artist catalogue | Async identity/name/alias/redirect/revision/filter/release-group queries | Needs real provider transport and authoritative per-release occurrences. Current paging fetches the complete discography repeatedly. |
| Artist artwork | Eligibility/provenance/cache-attestation/deduplication and fallback composition | Fetching/decoding and real cache attestations remain outside it; fix invalid ID/aspect assumptions. |
| Artist personal | BA5-backed save/manual-playlist semantics with CAS bytes port | Bind the port to Collection/profile transactions rather than adopting another personal store. |
| Artist media | Canonical mappings, BF2 qualification and separate open-versus-switch intent | Needs real mapping/transport/action routes and correct unavailable envelope behavior. |
| Artist bridge/composition | QObject query/command projections, lane/context fencing, paging and adapters joining all Artist parts | Composition has compile mismatches; default source and bytes store are fake/in-memory, and play port emits a request without playing. |
| W4 composition | Queue → resolution → playback → factual history → persistence → fallback/next; redirects/offline display; lyric-document swap; recommendation derivation | Valuable integration tests already exist. Host plumbing is encoded in test harnesses, not delivered as the full BH1 service. |

## Repairs before adopting affected components

These are static findings and retained qualification gaps, not claims that all corresponding failures reproduce in the current Colosseum app. Existing app paths do not yet consume all these reference components.

### R1. Restore exact-target Artist composition compilation

`artist-service-composed-20261015/ArtistReadAdapter.cpp:106–109` treats `Recording::lengthMs` as optional; `bg1-catalogue-client-prototype/VinylCatalogueTypes.h:165` defines a plain `int`. `FakeArtistSource.cpp:151,163` default-constructs `SourceBatch`, whose required `ArtistMbid` has no default constructor (`artist-live-prototypes-20261015/media/ArtistMediaQuery.h:39–44`).

The known three-line-class repairs must be recovered/applied to the exact adopted copy and compiled on the target Qt/MSVC configuration. The earlier approved repair outside this directory is not present in this retained source. Composed README/report evidence explicitly says target Windows execution was not run; temporary header-shim checks do not close it.

### R2. Bind BB5 restored source memory to the expected host profile

`SourceResolutionSession.h:304–305` accepts text without an expected profile. `SourceResolutionSession.cpp:581–597` constructs memory from the embedded scope. No typed profile mismatch exists. Add checked expected-profile restore and session binding; test a syntactically valid document from another profile, not just two independent in-memory objects.

### R3. Reconcile pending source replacement and resumable persistence

BA2C permits pending replacement while the old transport still plays (`QueueSessionCoordinator.cpp:528–551`). BA3 rejects pending input whenever playback is resumable (`VinylSessionPersistence.cpp:822–832`) and rejects pending+resume on restore. Capture must represent both the continuing transport bookmark and pending replacement intent, or apply a deliberate equivalent host policy without losing state.

### R4. Complete exact seek failure and time-bound handling

`PlaybackEngineAdapter.cpp:307–322` creates pending state for any nonnegative seek; `ColosseumMpvBridge.cpp:111–126` silently returns for an unrepresentable/out-of-window target. Serialized seek state advances only through the expected SEEK/PLAYBACK_RESTART pair (`MpvSidecarReference.cpp:117–169`). Add defined rejection, command error, timeout, cancellation and EOF/error exits.

The retained real composed CUE assertion at `real_cue_journey.cpp:1086–1088` failed in the final independent receipt. Its cause still needs a fresh focused investigation; do not widen its ±300ms assertion merely to obtain green output.

### R5. Fence actual transport cancellation, refresh and failure identity

The two Artist catalogue query objects each start local ticket numbering at one; their shared provider bridge forwards those numbers unchanged (`ArtistCatalogueQuery.cpp:389–394`, `ArtistReadAdapter.cpp:178–198`). Namespace/remap real transport tickets. The fake records cancels but still delivers callbacks, masking cancellation-map collisions. Media counters can diverge after invalid/cancelled requests.

BB5 repeated `refreshIfExpired()` can supersede an already pending refresh (`SourceResolutionSession.cpp:1148–1185`); coalesce the pending request. `playbackFailed()` receives occurrence identity only (`:1037–1054`); forward only BA2-accepted current failures or strengthen the request token so late failures from old transports cannot affect replacements.

### R6. Repair numeric, persistence and rejected-mutation boundaries

- BA2/BA3/BA4/BA5/BE2 use rounded double limits before signed integer conversion in several readers/seconds conversions. Check exact range before casting, including version integers. BE2 also loses large exact timestamps through double conversion.
- BA5 narrows positive qint64 release coordinates to int without checking INT_MAX (`VinylPersonalStatePersistence.cpp:131–143`). Its restore accepts duplicate canonical library rows and silently retains the first (`:574–599`; `VinylLibrary.cpp:70–76`). Reject malformed persisted duplicates while keeping interactive save idempotent.
- Advertised bounded display snapshots lack enforced string/container budgets at relevant BA5 boundaries; establish host limits across affected codecs.
- BE2 rejected hidden/DNR writes can create otherwise empty entity state because `ensureState` runs before revision validation (`FeedbackTasteState.cpp:235,296`). Failed mutations must leave state unchanged.

### R7. Harden text and relationship identities

- BC3 referent hashes use unescaped delimiter concatenation (`AnnotationMeaningPlane.cpp:191–218`). Distinct valid cue/anchor structures can produce identical pre-hash material. Use versioned unambiguous encoding; hashing the same ambiguous material is not collision protection.
- BC3 accepts a supplied text anchor without proving it belongs to the selected cue (`:263–286`); enforce that relation before presentation.
- BF1 delimiter-based evidence/semantic keys can discard distinct assertions (`VinylContextGraph.cpp:15–53`). Equivalent duplicated edges retain the first unsorted credit/role payload (`:132–147`), so normalize stored payload as well as the comparison key.
- BC1 applies a clamp before subtracting the source offset (`TimedTextCore.cpp:421,455–459`). For cue=1000ms, lyric offset=+2000ms and recording offset=-10000ms, the inverse playback target is 9000ms while the implementation returns 10000ms. Reconcile the contract and implementation; clamp at the final playback boundary.

### R8. Harden recommendation input interpretation

`DeterministicRanker.cpp:397–403` converts calibrated floating scores to qint64 before applying the cap; valid finite extreme inputs can exceed the conversion domain. Validate/clamp first and bound intermediate integer arithmetic. `:193–206` scores every graph edge, including preserved `Unsupported` relationships with no useful evidence, as affinity. Admit only explicitly understood, evidenced relations.

Before connecting live generators, define contribution deduplication, eligibility filtering and exact-entity versus artist-wide suppression. Existing source does not justify broader “never recommend this artist” behavior for all their tracks. Passive completion and deliberate completion are retained separately by derivation but currently share a rewarded recent-completion aggregate in BE3; preserve or deliberately revise that policy rather than claiming it is already a learned taste model.

### R9. Repair Artist presentation/data seams without accepting stale defect counts

The current bridge blocks already loaded selections during continuation loading (`ArtistServiceBridge.cpp:152,357`) and Empty continuation bypasses revision validation (`:246–273`). Media Unavailable-with-rows is classified Invalid (`ArtistMediaQuery.cpp:86–95`). Grouped recordings lack their complete edition occurrence relation, and the fake permits permissive group-level edition membership when mappings are absent. Supply actual per-release tracklists.

The historical 23-defect report is useful evidence, not a current verified count. Deterministic artwork sorting weakens its arrival-order allegation; `refreshSavedArtist()` already supplies recovery; an exact Qt qualification pin is not inherently a product defect. Do not carry those allegations forward unchanged.

## Evidence reconciliation

The retained W4-H/Claude receipts report earlier **37/37** runs. A later independent W4-I receipt, `w4-baseline-seal-evidence/ctest.log`, records **36/37**, failing the real CUE logical seek-time assertion. `W4-BASELINE-SEAL-REVIEW.md` and `FINAL-VERIFICATION.md` therefore state **REPAIR REQUIRED**, and no final frozen baseline manifest was issued.

Current source still has the BB5 expected-profile omission identified by that audit. This review has not rerun the real CUE case, so it reports the last retained failure and its unresolved qualification gate rather than inventing a fresh failure or pass.

The real CUE executable uses real libmpv with `ao=null`/`vo=null` (`real_cue_journey.cpp:201–204`). It tests engine events/time/queue behavior, not audible device output, real torrent discovery or external fallback services. CMake registers that test only when the fixture exists (`w4-superbuild/CMakeLists.txt:235–246`); receiving must assert required registration and execution, not merely accept a smaller green set.

Five isolated Artist component suites have retained green logs. The composed service has no equivalent target Windows receipt. BF3's old verification report describes symbols removed by the current BB5-composed design, so it does not establish execution of current source. Retained contract claims and inspected assertions remain valuable, with these limits recorded.

## What is genuinely absent or deferred here

1. **Full BH1 native/QML facade.** The Artist QObject bridge is real but partial. There is no complete mode service binding the queue/source/history/personal/text/recommendation lanes to the app.
2. **Live adapters:** concrete BG1/IArtistSource catalogue mapping, real candidate/manifest acquisition, production text/annotation/translation services, graph/discovery feeds, live mpv/device observation and the concrete BF3 resolver. Current `provider.py` already implements artist operations and yt-dlp resolution, so reuse it where suitable; it does not automatically implement these typed native ports.
3. **Request-scoped streaming service compatibility.** Current app build excludes VinylTorrentProvider because StreamServer lacks its manifest/file/cancel contract. BB1–BB5 implement policy and domain behavior, not that missing service.
4. **Ordinary local metadata ingestion.** Current app loose-file imports synthesize filename/Unknown artist releases. The external reference backend assumes canonical data supplied by adapters; it is not a tag scanner.
5. **Production persistence/adoption:** most codecs return bytes/inert state rather than owning filesystem transactions. Exposure persistence, schema migration from the current app, retention and profile lifecycle need explicit host adapters.
6. **BE4 radio/autoplay sequences.** Explicitly deferred; no authored implementation was found. Feed any future sequence through the one queue/session owner.
7. **Other named extensions:** gapless/crossfade, multi-FILE CUE, MBID-less reference-domain support, additional output command application beyond existing host controls, edition-save semantics, optional sync/scrobbling/ListenBrainz, fingerprints, persistent audio/video preference and learned ranking. These are explicit backlog/decisions, not capabilities proved by model classes.
8. **Covered screen data gaps:** edition label/catalogue number, annotation votes and artist-verified status are absent from the corresponding reference types. Add fields only with actual source semantics/provenance; do not paint fixture values as product data.

## Production adoption decisions

- **Identity:** local Vault IDs remain valid without network or an invented MBID. Introduce a clear host boundary retaining the local identity and optional verified canonical mapping. Do not cast provider strings into BA0 types.
- **One session:** promote BA1/BA2 coordination into one native session path; retain current repeat-one, previous/manual wrap and queue occurrence behavior. Do not run a QML queue and reference queue as competing controllers.
- **One player:** attach raw event/request identity to the existing mpv core. Preserve existing controls; do not instantiate the standalone real-test player as a second production engine.
- **One persistence/history authority:** adapt BA3 to current Progress checkpoints; BA5 to Collection; BA4 to Activity/history; BE2 to profile preferences. Preserve current privacy activation/retention/reset rules and do not double-count qualified consumption.
- **One portable build graph:** component CMake files repeatedly compile sibling dependencies and use qualification-specific paths. Promote each shared implementation once into the app graph. Do not add every standalone CMake project unchanged.
- **Real-data UI:** bind the recovered Artist reference and existing Album/Song/Home/Discover/Library/player/queue pages to these native projections. Refinement and verification happen in Colosseum as each adapter lands.

## Revised first development sequence

1. Pin this inventory and repair the adoption blockers in isolated promoted copies; recover exact-target green reference checks with required real test registration.
2. Establish the host identity/store/player compatibility contract and the missing BH1 facade; preserve the app's already working local behavior.
3. Route one complete app listening journey through the adopted backend, including source replacement, checkpoint, profile change and shutdown.
4. Connect existing catalogue/provider and torrent service adapters; then bind/refine the covered pages using real data.
5. Adopt text/observability/context/recommendation features and implement the explicitly missing service/product layers through the same owners.
6. Perform final full-app/installed-build qualification. Source review, component tests, real engine tests, audible output, integration and release remain distinct gates.
