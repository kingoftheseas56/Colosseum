# W4-D Queue + Session Schema v2 Handoff

AGENT: Agent-4
TASK: T-4 / W4-D
STATE: DONE, ready for Lead verification and downstream consumption
DATE: 2026-10-06

## COMPLETED

- Added shared BA0 ProfileScopeId for host-owned profile scope.
- Added shared BA0 PlayInstanceId for durable Play-journey identity while preserving BA2 PlaybackWorkToken as the live async fence.
- Added optional descriptive BA1 EnqueueOrigin with origin kind, EnqueueBatchId, and optional canonical/playlist container context.
- Enqueue origin does not participate in QueueOccurrenceId identity or queue ordering.
- Added materialized deterministic BA1 shuffle with injected seed, active/prior-history preservation, remembered original QueueOccurrenceId order, and unshuffle.
- Added persisted shuffle restore seam so BA3 can round-trip both visible shuffled order and original order.
- Advanced BA3 writer to schema v2 with MinimumSupportedVersion = 1.
- v2 reader accepts supported v1 snapshots, validates v1 against the exact v1 field set, and upgrades into the v2 in-memory shape.
- v2 persists EnqueueOrigin, shuffle/original order, integer-ms resume, repeat/pending-input intent, and W4-F LastKnownDisplaySnapshot.
- LastKnownDisplaySnapshot remains explicitly non-authoritative presentation fallback.
- v2 persists no BA2 generation, PlaybackWorkToken, seek sequence, or live seek request identity.
- Added direct pending-input round-trip and shuffled-queue persistence/unshuffle regression coverage.
- Posted the W4-D public-interface checkpoint in WAVE4-AGENT-ROOM.md for W4-E/W4-G; its BA3 coordinator projection is also ready for W4-B mapping.

## VERIFIED

Toolchain:
- Qt 6.11.1 MSVC 2022 x64
- MSVC 19.44.35223
- Windows SDK 10.0.26100.0
- Visual Studio 17 2022 generator

Fresh/local W4-D builds:
- w4d-build/ba0: compile/link PASS, CTest 1/1 PASS
- w4d-build/ba1: compile/link PASS, CTest 1/1 PASS
- w4d-build/ba3: compile/link PASS, CTest 1/1 PASS

Final combined rerun:
- BA0: 1/1 PASS
- BA1: 1/1 PASS
- BA3: 1/1 PASS
- total: 3/3 harnesses green

Static verification:
- SessionProfileScopeId: zero occurrences in BA3 prototype
- BA3 persistence implementation emits no JSON field named generation, workToken, or seekSequence
- SchemaVersion = 2
- MinimumSupportedVersion = 1
- v1 exact-field validation and v2 exact-field validation are separate reader branches

Verification notes:
- The first BA0 CTest launch lacked Qt DLLs on PATH and exited 0xc0000135; the already-built harness passed once the Qt 6.11.1 bin directory was supplied.
- A new shuffle persistence test initially asserted that one fixed seed must visibly change order. That assertion was corrected because a valid random permutation may equal the original order; the proof now checks persisted shuffle state, visible-order round trip, and exact unshuffle restoration.

## CHANGED

Canonical owned surfaces:
- BA0-DOMAIN-CONTRACT.md
- ba0-domain-prototype/VinylDomainTypes.h
- ba0-domain-prototype/VinylDomainTypes.cpp
- ba0-domain-prototype/test_vinyl_domain.cpp
- BA1-QUEUE-CORE-CONTRACT.md
- ba1-queue-core-prototype/VinylQueue.h
- ba1-queue-core-prototype/VinylQueue.cpp
- ba1-queue-core-prototype/test_vinyl_queue.cpp
- BA3-SESSION-PERSISTENCE-CONTRACT.md
- ba3-session-persistence-prototype/CMakeLists.txt
- ba3-session-persistence-prototype/VinylSessionPersistence.h
- ba3-session-persistence-prototype/VinylSessionPersistence.cpp
- ba3-session-persistence-prototype/test_vinyl_session_persistence.cpp
- WAVE4-AGENT-ROOM.md
- W4-D-HANDOFF.md

Generated verification build trees live under backend/w4d-build/ and are evidence, not durable source authority.

## PUBLIC INTERFACES

### BA0 ProfileScopeId

`ProfileScopeId::parse(QString)` -> `DomainResult<ProfileScopeId>`

Rules:
- non-empty
- <= 128 characters
- trimmed
- no control characters
- case-preserving
- host/profile scope only

### BA0 PlayInstanceId

`PlayInstanceId::parse(QString)` -> `DomainResult<PlayInstanceId>`

Same opaque-token validation as ProfileScopeId, but a distinct strong type.

It is durable Play-journey identity, not canonical song identity and not BA2 live-work identity.

### BA1 EnqueueOrigin

`QueueOccurrence::create(..., optional<EnqueueOrigin>)`

Origin:
- kind: user | album | playlist | radio | restore
- EnqueueBatchId
- optional EnqueueContainerRef
- container is canonical CanonicalEntityRef or opaque playlist ID

### BA1 shuffle

- `shuffle(quint64 seed)`
- `unshuffle()`
- `restoreShuffleOriginalOrder(QVector<QueueOccurrenceId>)`
- `shuffleOriginalOrder()`

The host supplies fresh production entropy; tests can inject a fixed seed.

### BA3 schema v2

`VinylSessionPersistence::SchemaVersion == 2`
`VinylSessionPersistence::MinimumSupportedVersion == 1`

v2 writer emits:
- queue items with enqueueOrigin + optional displaySnapshot
- activeOccurrenceId
- optional shuffle originalOrder
- resume with optional integer positionMs
- coordinator repeatMode + optional pendingInput

Reader accepts v1 and v2, fails closed on unknown fields within the declared version, and upgrades v1 in memory.

## IMPORTANT EVIDENCE

Representative SHA-256:
- BA0-DOMAIN-CONTRACT.md: f1a45ca8e7c24a7e381e4e50611231c2e9c3b53b341e8676ab32309363623266
- VinylDomainTypes.h: a39e0339ae154e0cf6c0de5b8c3c2611ac5350e93c530242751fd3476d8e7491
- VinylQueue.h: bca748416418bba179745c0a713b4374fe7cbd9f81ea0f964858036ccbde774f
- BA3-SESSION-PERSISTENCE-CONTRACT.md: 5a09a0309acf3134ff8d28e4ff5b7d3a598fad6a0eb7c383aeeb9c2a2d43eb79
- VinylSessionPersistence.h: 159beee664089d0ada19b7c5e4d0b95d93ef77139466454371a254240d6056c9
- VinylSessionPersistence.cpp: 32d15e184dcdc7709cb1825e86cd9c40f6ad2a4e0289dfd60c3623c431351e76
- test_vinyl_session_persistence.cpp: 97802d9cbb18758db8a5d2b9b00ae025b8a86fd47d8657fce25bb60ba72721fc

## UNRESOLVED

No W4-D implementation blocker remains.

Cross-slice integration still owned elsewhere:
- W4-B must map its final coordinator repeat/pending-input public types to BA3's durable coordinator projection.
- W4-E must consume BA0 ProfileScopeId and PlayInstanceId without redefining them.
- W4-G consumes EnqueueOrigin after W4-E/W4-F are compatible.
- W4-H remains the composed-system proof and may still expose integration repairs.

Production Colosseum was not modified.

## NEXT

Lead should independently inspect this handoff and the three green harnesses, then mark T-4 done/unblock dependent work when compatible with T-5/T-6.
