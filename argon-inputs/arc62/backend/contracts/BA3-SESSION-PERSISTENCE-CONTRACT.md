# Vinyl Backend BA3 - Session Persistence Contract

Status: W4-D schema v2 complete and locally verified; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl
Depends on: BA0 Domain Contract, BA1 Queue Core, BA2 Playback Session Lifecycle, W4-F display-snapshot interface

## Purpose

BA3 defines durable, presentation-independent snapshot and restore for Vinyl's logical queue plus the minimum session/coordinator intent that remains meaningful after process death.

The load-bearing separation is:

durable queue/session intent != live BA2 playback work

A restored snapshot may recover queue order, active QueueOccurrenceId, enqueue origin, reversible shuffle state, repeat/pending-input intent, inert resume position, and last-known display fallback. It may not resurrect an old PlaybackWorkToken, BA2 generation, seek sequence, engine handle, source credential, provider selection, or raw mpv object.

## Upstream ownership

BA3 consumes:
- BA0 Recording identity plus shared ProfileScopeId;
- BA1 QueueOccurrence, QueueOccurrenceId, queue order, EnqueueOrigin, and shuffle/original-order state;
- BA2-A lifecycle observations only to derive inert resume intent;
- coordinator-owned repeat/pending-input intent through a persistence projection;
- W4-F LastKnownDisplaySnapshot as explicitly non-authoritative presentation fallback.

BA3 serializes these facts. It does not become their behavioral owner.

## BA3 laws

1. Persistence is bound to one BA0 ProfileScopeId.
2. Queue order is stored as an ordered sequence of BA1 QueueOccurrences.
3. Duplicate Recording MBIDs remain legal and retain distinct QueueOccurrenceIds.
4. Active cursor is stored by QueueOccurrenceId, never queue index.
5. Canonical Recording identity and optional release-track context round-trip without reinterpretation.
6. EnqueueOrigin is descriptive context only and cannot replace occurrence or Recording identity.
7. Materialized shuffle persists both visible queue order and the remembered original QueueOccurrenceId order.
8. A restored shuffled queue does not reshuffle; unshuffle after restore recovers the remembered original order.
9. Durable resume intent may exist only for a BA2 Loading, Playing, or Paused lifecycle coherent with BA1 active occurrence.
10. Idle, Ended, and Failed BA2 lifecycles do not manufacture resume work.
11. A pending seek contributes only its target as an inert integer-millisecond bookmark.
12. Coordinator pending-input intent may exist only for the active queue occurrence while BA2 has no resumable lifecycle.
13. Repeat intent is persisted as a narrow durable policy value, currently off or queue.
14. LastKnownDisplaySnapshot is non-authoritative text fallback beside canonical identity and can never satisfy identity lookup or override redirect/canonical truth.
15. Restoring a snapshot creates no live BA2 generation, PlaybackWorkToken, or seek request.
16. The first future BA2 begin/seek after restore mints fresh live identity.
17. Reader version 2 accepts supported versions 1..2 and upgrades v1 in memory.
18. Unknown fields fail closed against the declared schema version.
19. Corrupt, incompatible, cross-profile, or internally inconsistent snapshots return typed errors and no partial restored state.
20. Provider credentials, source-ranking state, transport secrets, raw engine objects, UI state, and mock assumptions are outside BA3.

## Shared profile scope

BA3 no longer declares SessionProfileScopeId.

It consumes BA0 ProfileScopeId, the same host-owned opaque profile boundary used by the other durable personal-state lanes at convergence. The token remains distinct from MusicBrainz identity, provider identity, account-provider credentials, and filesystem paths.

## Schema evolution rule

Schema name remains:

`colosseum.vinyl.session-persistence`

Current writer version: 2.
Minimum supported reader version: 1.

A reader of version N accepts supported older versions up to N, validates each version against that version's exact field set, and upgrades older snapshots into the current in-memory shape.

Unknown fields do not become silently ignored compatibility extensions. Additive durable shape changes require a declared schema version and an explicit reader branch.

Unsupported future versions fail typed rather than being guessed.

## Snapshot schema v2

Root contains exactly:
- schema
- version = 2
- profileScopeId
- queue
- resume
- coordinator

queue contains exactly:
- items, ordered visible QueueOccurrence array
- activeOccurrenceId, string or null
- shuffle, object or null

Each queue item contains exactly:
- occurrenceId
- recordingMbid
- releaseContext, object or null
- enqueueOrigin, object or null
- displaySnapshot, object or null

releaseContext contains exactly:
- releaseGroupMbid
- releaseMbid
- releaseTrackMbid, string or null
- recordingMbid
- mediumPosition
- trackPosition

enqueueOrigin contains exactly:
- kind = user | album | playlist | radio | restore
- batchId
- container, object or null

A canonical container contains:
- kind = canonical
- entityKind
- mbid

A playlist container contains:
- kind = playlist
- playlistId

displaySnapshot contains exactly:
- primaryText
- artistCredit, ordered string array

The display snapshot carries no canonical replacement ID, provider identity, score, or freshness claim.

shuffle, when non-null, contains exactly:
- originalOrder, ordered QueueOccurrenceId array

The queue.items array is the current visible/materialized order. shuffle.originalOrder is the reversible pre-shuffle order.

resume is null or contains exactly:
- occurrenceId
- playWhenReady
- optional positionMs

positionMs is integer milliseconds. Schema v2 does not emit the legacy floating positionSeconds field.

coordinator contains exactly:
- repeatMode = off | queue
- pendingInput, object or null

pendingInput contains exactly:
- occurrenceId
- playWhenReady

No v2 field exists for:
- BA2 generation
- PlaybackWorkToken
- seek sequence / pending PlaybackSeekRequest identity
- duration
- terminal failure envelope
- provider/source selection
- transport headers or credentials
- engine/mpv handles
- PlayInstanceId

PlayInstanceId is a shared BA0 durable Play-journey identity for coordinator/history use, not a BA3 queue/session resurrection token.

## Supported v1 upgrade

Schema v1 remains supported as the previous frozen shape:
- root: schema, version, profileScopeId, queue, resume
- queue: items, activeOccurrenceId
- item: occurrenceId, recordingMbid, releaseContext
- resume position uses optional positionSeconds

The v2 reader upgrades v1 in memory as follows:
- v1 ProfileScopeId text is parsed through BA0 ProfileScopeId;
- ordered queue and active QueueOccurrenceId are preserved exactly;
- duplicate Recording occurrences remain distinct;
- positionSeconds is validated and converted to integer positionMs;
- EnqueueOrigin remains unknown/absent;
- display snapshots remain absent;
- shuffle is inactive with no invented original order;
- repeat defaults to off;
- pending input defaults to absent.

The reader does not manufacture facts that did not exist in v1.

## Capture policy

BA3 always captures BA1 queue order and active occurrence identity.

For BA2 Loading, Playing, or Paused:
1. BA2 must have a bound occurrence.
2. BA1 must have an active occurrence.
3. Both must name the same QueueOccurrenceId and agree on Recording/release context.
4. playWhenReady is copied as durable intent.
5. Resume position is latest pending seek target when present, otherwise latest observed position.
6. The position is converted to validated integer milliseconds.

For BA2 Idle, Ended, or Failed, resume is null.

Coordinator pending input may be captured only when:
- BA1 has an active occurrence;
- pendingInput names that exact occurrence;
- BA2 is not in a resumable Loading/Playing/Paused lifecycle.

Display snapshots supplied for capture must each name an existing queue occurrence and must be unique by QueueOccurrenceId.

## Restore policy

Restore is transactional:
1. parse complete JSON;
2. validate schema name and supported integer version;
3. select the exact allowed field set for that declared version;
4. validate persisted ProfileScopeId and require it to equal the requested profile;
5. parse every queue occurrence through BA0/BA1 boundaries;
6. append items in persisted visible order;
7. restore active state by QueueOccurrenceId;
8. for v2, restore/validate shuffle original-order permutation;
9. restore optional resume intent against the exact active occurrence;
10. restore coordinator repeat/pending-input intent and validate pending ownership;
11. restore display snapshots only as non-authoritative metadata;
12. return completed state only after every check succeeds.

The restored result contains VinylQueue, optional SessionResumeIntent, SessionCoordinatorIntent, and display snapshots. It does not contain PlaybackSession.

## Typed errors

BA3 defines:
- none
- invalid_profile_scope_id
- empty_snapshot
- malformed_snapshot
- unsupported_schema
- unsupported_version
- profile_mismatch
- invalid_queue_state
- invalid_resume_state
- invalid_coordinator_state

Malformed canonical IDs, duplicate occurrence IDs, invalid release context, invalid shuffle permutations, missing active occurrence, mismatched resume/pending occurrence, version-inappropriate fields, invalid JSON types, and injected live-work/provider fields fail closed.

## Prototype

Reference implementation:
`backend/ba3-session-persistence-prototype/`

Files:
- VinylSessionPersistence.h
- VinylSessionPersistence.cpp
- test_vinyl_session_persistence.cpp
- CMakeLists.txt

The prototype compiles BA0, BA1, and BA2-A sources directly, consumes the W4-F display-snapshot header, and uses Qt Core only.

## W4-D acceptance proof

The native harness proves:
1. duplicate Recording occurrences round-trip under distinct QueueOccurrenceIds;
2. visible queue order and active occurrence identity round-trip;
3. release context and EnqueueOrigin round-trip;
4. last-known display snapshot round-trips as non-authoritative metadata;
5. schema v2 emits integer positionMs and no positionSeconds;
6. v2 reader restores repeat intent;
7. pending-input occurrence plus playWhenReady round-trip from an idle BA2 lifecycle;
8. materialized shuffle visible order plus original order round-trip;
9. unshuffle after restore returns the exact pre-shuffle order;
10. supported v1 fixture upgrades in memory without inventing origin/display/shuffle/coordinator facts;
11. unknown v1 fields fail closed against v1 and unknown v2 fields fail closed against v2;
12. duplicate occurrence IDs, missing active IDs, mismatched resume IDs, and cross-profile restore fail typed;
13. live BA2 generation/work-token/seek-sequence fields are absent and injected equivalents are rejected;
14. post-restore BA2 work begins with fresh generation/seek identity.

## Verification

Verified locally on 2026-10-06 with Qt 6.11.1, MSVC 19.44.35223, Windows SDK 10.0.26100.0:
- CMake configure: PASS
- compile/link through vinyl_session_persistence_harness: PASS
- CTest: 1/1 PASS
- added W4-D pending-input and persisted-shuffle regression cases: PASS

The W4-D BA0 and BA1 harnesses also rebuilt and passed independently.

Production Colosseum adoption remains separate and must not be inferred from these reference prototypes.
