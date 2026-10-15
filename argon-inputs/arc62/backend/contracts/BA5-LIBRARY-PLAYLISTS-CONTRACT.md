# Vinyl Backend BA5 - Library + Playlists Contract

Status: W4-H seal-hardened and verified, including optional non-authoritative display snapshots and durable schema v3; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl
Depends on: frozen BA0 Domain Contract
Reads BA1 Queue Core only for ownership boundaries

## Purpose

BA5 owns Vinyl personal saved-state semantics for Library and Playlists.

Library membership and playlist membership are different concepts. Saving a canonical entity does not place it in a playlist, and adding a recording to a playlist does not implicitly save it to the Library.

BA5 is profile-scoped personal state. MusicBrainz remains canonical music identity. External providers may later import or export state, but they never become the authority for Vinyl Library or Playlist identity.

## Guardrail split

The requested BA5 slice crosses the current Preflight oversized-slice threshold because it combines two independently mutable state systems plus a native build and deterministic harness.

BA5 was therefore split into independently verifiable checkpoints:

1. BA5-A Library Core - verified in the first BA5 turn.
2. BA5-B Playlist Core - implemented and verified only after Hemanth's later explicit `continue`.

BA5-A freezes saved canonical entities. BA5-B freezes PlaylistId, PlaylistItemId, ordered duplicate-safe playlist membership, optional release context, provenance, and playlist mutation operations.

The hard turn boundary kept Library and Playlist semantics from becoming one coupled long-running implementation merely because both appear under one product surface.

## Basis inspected

Arc 62 authority:
- backend/README.md
- backend/BA0-DOMAIN-CONTRACT.md
- backend/BA1-QUEUE-CORE-CONTRACT.md
- Music Assistant donor assessment
- Feishin donor assessment
- Nuclear donor assessment

Read-only Colosseum architecture references:
- native/CollectionStore.h
- native/account/ProfileContext.*
- native/account/ProfilePaths.*
- qml/LibraryApi.js
- qml/BiblioLibraryApi.js
- qml/TankobanLibraryApi.js

Current inspected Colosseum basis:
- branch: master
- commit: a61c4564b088b2e1871829d4b7021ab16f7eace0

Useful existing patterns, not adoption targets:
- CollectionStore treats manual membership as distinct from progress.
- ProfileContext/ProfilePaths isolate personal stores by active profile.
- Library projection code consumes store state without becoming its authority.

No production Colosseum file is modified by BA5.

## BA5 laws

1. Personal state is scoped to an explicit Vinyl profile scope.
2. Canonical saved identity is always a BA0 canonical MusicBrainz entity reference.
3. BA5-A supports saved Artist, Release Group, and Recording entities.
4. A saved Release Group is one Library membership. It never expands into saved recordings.
5. Provider IDs and provider item IDs are mappings/provenance only and cannot substitute for canonical identity.
6. Library membership is set-like within one profile: the same canonical entity can exist at most once.
7. Re-saving the same canonical entity is a successful deterministic no-op and preserves the original saved timestamp.
8. The same canonical entity may be saved independently in different profiles.
9. Removing an entity from one profile cannot affect another profile.
10. Queue occurrence identity is outside Library identity.
11. Playlist item identity is outside Library identity.
12. Recommendation candidate/ranking state is outside Library identity.
13. BA5 may durably retain only a bounded non-authoritative display snapshot (`title`, `artistCredit`) beside a saved canonical reference so Library and Playlist rows remain readable offline. This snapshot is fallback presentation only: it never becomes canonical identity, never repairs redirects, and never overwrites catalogue truth.

## BA5-A types

### ProfileScopeId

Shared BA0 host-owned ProfileScopeId supplied by the profile owner. BA5 no longer defines a lane-local profile identity.

Rules:
- non-empty;
- maximum 128 characters;
- no surrounding whitespace;
- no control characters;
- case is preserved;
- BA5 does not derive account identity or filesystem paths from it.

Production adoption maps the active ProfileContext profile ID into the shared BA0 ProfileScopeId without importing ProfilePaths into the BA5 domain model.

### LibraryEntry

One saved Library membership contains:
- canonical: BA0 CanonicalEntityRef;
- savedAtMs: profile-local personal-state metadata recording when membership was created.

Only canonical kinds Artist, ReleaseGroup, and Recording are accepted in BA5-A.

Release, ReleaseTrack, and Work remain valid BA0 identities but are not BA5-A Library membership kinds. Attempting to save them fails with a typed unsupported-kind error rather than silently changing meaning.

LibraryEntry does not carry provider identity, queue occurrence identity, source candidate state, playback state, progress, or recommendation scores.

### VinylLibrary

One VinylLibrary instance is bound to exactly one ProfileScopeId.

It owns:
- deterministic LibraryEntry membership;
- canonical membership lookup;
- add/remove mutation semantics.

Its in-memory vector order is deterministic insertion order for the prototype, but BA5-A does not define product sorting or presentation ordering. Playlist durable order is a separate BA5-B law.

## BA5-A operations

### save

Input: one validated LibraryEntry.

Behavior:
- unsupported entities fail before insertion;
- new canonical entity inserts exactly one membership;
- existing canonical entity returns success with changed=false;
- duplicate save does not rewrite savedAtMs.

### remove

Input: one BA0 CanonicalEntityRef.

Behavior:
- existing membership removes exactly that canonical entity;
- unknown membership returns a typed EntryNotFound failure;
- no other profile can be affected because profile scope is owned by the VinylLibrary instance.

### contains / find

Read-only canonical lookup. Provider IDs are not accepted as lookup identity.

## BA5-A typed failures

- None
- UnsupportedCanonicalEntityKind
- InvalidSavedAt
- EntryNotFound

Stable prototype names use snake_case.

## BA5-A invariants

After every successful mutation:

1. the library is bound to one immutable ProfileScopeId;
2. every entry is Artist, Release Group, or Recording canonical identity;
3. canonical entity references are unique within that profile;
4. savedAtMs remains positive;
5. provider identity is absent;
6. queue identity is absent;
7. playback and source state are absent;
8. recommendation state is absent.

## Ownership boundaries

### Library vs Playlist

Library answers whether a canonical entity is saved by the active profile.

Playlist answers which recording occurrences belong to one ordered playlist.

Neither membership implies the other.

### Library / Playlist vs Queue

BA1 owns queue order and QueueOccurrenceId.

BA5 never turns a queue occurrence into durable Library identity. BA5-B must define PlaylistItemId as a separate strong type. Sending a playlist item to BA1 must create or receive a QueueOccurrenceId through a queue operation rather than reusing PlaylistItemId.

### BA5 vs Recommendations

BE owns recommendation candidates, ranking, feedback, fatigue, and autoplay policy.

A future generated playlist may carry generation provenance, but that provenance cannot mutate Recording MBID identity and cannot make BA5 a recommendation ranker.

### BA5 vs Providers

External services may later propose import/export operations. Imported records must resolve to Vinyl canonical MBID identity before becoming BA5 state. Provider IDs remain mapping/provenance data and never replace canonical Library or Playlist identity.

## BA5-B Playlist Core

### PlaylistId

Opaque playlist identity owned by BA5.

Rules:
- non-empty;
- maximum 128 characters;
- no surrounding whitespace;
- no control characters;
- case-sensitive and preserved exactly;
- not a MusicBrainz identifier;
- not a provider identifier.

### PlaylistItemId

Opaque identity for one ordered item occurrence inside a playlist.

It is deliberately distinct from both:
- BA0 `RecordingMbid`;
- BA1 `QueueOccurrenceId`.

The same Recording MBID may therefore appear multiple times in one playlist under different PlaylistItemIds. Duplicate recordings are legal. Duplicate PlaylistItemIds inside one playlist are not.

Queueing a playlist item must create or receive a queue occurrence through BA1. Reusing the PlaylistItemId as QueueOccurrenceId would collapse two ownership domains and is forbidden.

### PlaylistItem

One playlist item contains:

```text
PlaylistItemId
RecordingMbid
addedAtMs
ReleaseTrackCoordinate?
```

The Recording MBID is canonical playable-song identity.

addedAtMs is durable user-owned metadata and must be positive and no earlier than the playlist createdAtMs. Optional release context is descriptive edition/track context only. When present, its Recording MBID must match the playlist item's Recording MBID or construction fails with `RecordingContextMismatch`.

No provider/source object is stored in PlaylistItem.

### Playlist

One Playlist is bound to:
- one shared BA0 ProfileScopeId;
- one PlaylistId;
- one durable user-authored name;
- one positive createdAtMs timestamp;
- one PlaylistProvenanceKind;
- one explicit ordered sequence of PlaylistItems.

The sequence itself is authoritative playlist order. Production persistence/import/export must preserve PlaylistItemId order exactly rather than regenerating order from Recording MBIDs, titles, timestamps, or provider order.

The same PlaylistId may exist in another profile because BA5 state is profile-scoped.

### Playlist provenance

BA5 freezes two descriptive provenance kinds:
- `Manual`;
- `Generated`.

This is justified because future recommendation/radio lanes may materialize an ordered playlist while ordinary user-created playlists remain manual.

Provenance is metadata about how the playlist came to exist. It does not:
- change Recording identity;
- change PlaylistItem identity;
- authorize recommendation ranking inside BA5;
- make a provider authoritative.

### Playlist operations

#### rename

Rename changes only the durable user-authored playlist name. Blank, untrimmed, control-containing, or overlong names fail typed validation. Renaming to the current name is an idempotent no-op.

#### append

Append one PlaylistItem to the tail.

Duplicate Recording MBIDs are allowed. Duplicate PlaylistItemIds fail.

#### insert

Insert one PlaylistItem at an explicit index in `0..size`.

#### remove

Remove exactly one PlaylistItem by PlaylistItemId.

Removing one duplicate Recording occurrence must leave every other PlaylistItemId untouched.

#### move

Move exactly one PlaylistItem by PlaylistItemId to a destination index in the final playlist order.

Moving one duplicate Recording occurrence moves only that playlist item. Moving to its current final index is a successful no-op.

### BA5-B typed failures

- `None`
- `InvalidPlaylistId`
- `InvalidPlaylistItemId`
- `InvalidPlaylistName`
- `InvalidCreatedAt`
- `InvalidAddedAt`
- `DuplicatePlaylistItemId`
- `PlaylistItemNotFound`
- `IndexOutOfRange`
- `RecordingContextMismatch`

Stable prototype names use snake_case.

### BA5-B invariants

After every successful mutation:

1. PlaylistItemIds are unique inside one playlist.
2. Recording MBID duplication is legal.
3. Every PlaylistItem carries one canonical Recording MBID.
4. Playlist name and createdAtMs are durable user-owned metadata.
5. Every PlaylistItem addedAtMs is positive and does not predate its playlist.
6. Optional release context agrees with that Recording MBID.
7. Order is explicit and deterministic by PlaylistItem sequence.
8. Playlist provenance never changes canonical identity.
9. QueueOccurrenceId is not stored or reused as PlaylistItemId.
10. Provider/source state is absent.
11. Library membership is not implied by playlist membership.

## Explicit non-goals

BA5 does not implement:
- QML or any current mock section;
- playback;
- queue persistence;
- recommendation ranking;
- cloud sync;
- provider login/import/export runtime;
- database placement/migration into production Colosseum;
- cloud transport or cross-device merge policy.

W4-E established the BA5 on-disk reference format; W4-H extends it to schema v3 with optional non-authoritative display snapshots. Production file/database placement remains adoption work. BA3 remains queue/session persistence and is not repurposed as Library authority.

## Durable personal-state format

W4-E established `VinylPersonalStatePersistence` as the versioned BA5 reference format; the W4-H seal repair extends that format without changing BA5 identity ownership.

- schema: `colosseum.vinyl.personal-state`;
- writer version: 3;
- reader accepts supported versions 1 through 3; v1/v2 rows restore with no invented display snapshot and upgrade on the next write;
- malformed JSON, profile mismatch, invalid state, unknown fields, typed display-snapshot corruption, truncated files, and unsupported future versions return typed failures;
- Library entries serialize canonically by canonical entity key;
- playlists serialize canonically by PlaylistId while PlaylistItem sequence remains authoritative semantic order;
- Library entries and Playlist items may each carry an optional `PersonalDisplaySnapshot { title, artistCredit }`;
- playlist name, createdAtMs, PlaylistItemId, RecordingMbid, addedAtMs, release context, provenance, and optional display snapshot round-trip;
- repeated capture of equivalent state produces identical compact JSON bytes.

The display snapshot is deliberately non-authoritative fallback presentation. The format stores no provider/source authority, never turns title or artist credit into canonical music identity, and never permits the snapshot to overwrite a BG1 catalogue result or redirect target.

## Prototype

Reference prototype:

`backend/ba5-library-playlists-prototype/`

Files:
- `VinylLibrary.h/.cpp`
- `VinylPlaylist.h/.cpp`
- `test_vinyl_library.cpp`
- `test_vinyl_playlist.cpp`
- `VinylPersonalStatePersistence.h/.cpp`
- `test_vinyl_personal_state_persistence.cpp`
- `CMakeLists.txt`

The production BA5 core links only BA0-derived canonical types and Qt Core. The playlist harness includes the BA1 Queue Core header solely to compile-time-prove PlaylistItemId and QueueOccurrenceId remain different strong types; BA5 playlist implementation itself has no BA1 runtime dependency.

## Acceptance

BA5 is accepted when the native Qt/C++ harnesses prove:

### BA5-A Library

1. Artist, Release Group, and Recording can be saved independently.
2. Saving a Release Group does not save any Recording.
3. Re-saving the same canonical entity is an idempotent no-op.
4. Duplicate save preserves the original savedAtMs.
5. Unsupported canonical entity kinds fail with a typed error.
6. Removing an unknown membership fails with a typed error.
7. The same canonical entity can be saved independently in two profiles.
8. Removing from one profile leaves the other profile untouched.
9. ProviderItemRef is not convertible to canonical Library identity.

### BA5-B Playlist

10. The same Recording MBID may occur twice under distinct PlaylistItemIds.
11. Duplicate PlaylistItemIds fail with a typed error.
12. Removing one duplicate leaves the other exact playlist item untouched.
13. Reordering one duplicate moves only that PlaylistItemId.
14. Insert and move bounds fail with typed errors.
15. Optional release context remains attached and Recording/context mismatch fails closed.
16. PlaylistItemId is compile-time distinct and non-convertible from both RecordingMbid and QueueOccurrenceId.
17. ProviderItemRef cannot replace RecordingMbid.
18. Manual/generated provenance remains explicit without altering canonical Recording identity.
19. Ordered item sequence remains deterministic through append, insert, remove, and move.
20. Playlist name, createdAtMs, and item addedAtMs are durable and round-trip.
21. Versioned BA5 persistence rejects corruption/unknown fields, restores supported v1, and rewrites the current schema.
22. Equivalent state with different non-semantic insertion order produces identical canonical bytes.
23. All three harnesses execute against Qt 6.11.1.
24. Prototype source has no QML/mock dependency.
25. BA5 core source has no mpv/playback, recommendation, provider-runtime, VinylQueue, or QueueOccurrenceId dependency.

## Verification

BA5-A was verified locally on 2026-10-06 against the exact Qt 6.11.1 MSVC kit at `C:/Qt/6.11.1/msvc2022_64`.

BA5-B was then implemented only after Hemanth's later explicit `continue` and verified against the same kit.

Final combined verification:
- CMake 3.30.5 configured with Visual Studio 2022 x64 and Qt 6.11.1.
- Release build completed for `vinyl_domain`, `vinyl_library`, `vinyl_playlist`, `vinyl_personal_state_persistence`, and all harnesses.
- CTest passed 3/3 tests: `vinyl_library_harness`, `vinyl_playlist_harness`, and `vinyl_personal_state_persistence_harness`.
- The core harnesses assert `qVersion() == 6.11.1`; the persistence harness executes against the same Qt runtime.
- The playlist harness contains compile-time negative controls separating PlaylistItemId from RecordingMbid and BA1 QueueOccurrenceId.
- Source leakage scan: `BA5_QML_MOCK_LEAK_MATCHES=0`.
- Core-domain leakage scan: `BA5_CORE_FORBIDDEN_DEP_MATCHES=0` for mpv/playback, recommendation, provider runtime, VinylQueue, and QueueOccurrenceId in `VinylLibrary.*` / `VinylPlaylist.*`.
- Production Colosseum was read-only throughout.
- W4-E consumes the frozen W4-D shared BA0 ProfileScopeId and does not modify the W4-D write-set.

Production Colosseum adoption remains a separate future state and must not be inferred from this prototype.
