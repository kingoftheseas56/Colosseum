# Vinyl Backend BB3 - CUE / Whole-Album Sources Contract

Status: BB3 locally complete; Qt 6.11.1 native harness verified; production adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BB3 models one physical whole-album audio source as multiple virtual playable track slices without changing Vinyl's canonical identity rules.

The boundary is:

```text
one physical whole-album source
  + CUE text from an external sidecar or embedded metadata
  + optional measured backing duration
    -> validated virtual track ranges
    -> BB2 AlbumSourceManifest evidence
    -> BB2 exact-track decision
    -> exact virtual playback slice
```

BB3 owns CUE normalization and time bounds. BB2 continues to own the exact identity decision. BB3 does not discover sources, rank providers, play media, or infer MusicBrainz identity from a CUE sheet.

## Upstream authority

BA0 remains authoritative for:
- `RecordingMbid` as canonical playable-song identity;
- `ReleaseTrackMbid` as the release occurrence identity;
- `ReleaseTrackCoordinate`;
- provider identity remaining separate from canonical music identity.

BB1 remains authoritative for source-candidate provenance.

BB2 remains authoritative for:
- the requested Recording identity;
- exact Release Track / Recording / release-coordinate evidence;
- structured title/artist/album/position evidence;
- Exact, Rejected, Ambiguous, and InsufficientEvidence outcomes.

A BB3 virtual ID, CUE track number, filename, or physical source ID can never substitute for a BA0 canonical ID.

## Product laws

1. One physical source may expose many virtual track slices.
2. A virtual track is a range over a source, not a new physical media file.
3. `VirtualTrackId` is lane-local identity only.
4. CUE metadata never manufactures `RecordingMbid`, `ReleaseTrackMbid`, Release MBID, or Release Group MBID.
5. BB2 decides which virtual occurrence satisfies the requested canonical track.
6. BB3 binds a playback slice only after BB2 returns `Exact`.
7. Standard CUE timing is 75 frames per second.
8. Unknown facts remain unknown. An unknown final source duration produces an explicit open end rather than a guessed end.
9. Invalid, non-increasing, or contradictory range data fails closed.
10. External and embedded CUE inputs feed the same normalized contract; origin is provenance only.
11. BB3 is transport- and player-neutral. It has no mpv, network, torrent, QML, or provider-fetch dependency.
12. The current BB3 contract is deliberately one-physical-source. Multi-`FILE` CUE sheets fail with a typed unsupported-shape error instead of being silently flattened.

## Physical source identity

`WholeAlbumSourceId` is an opaque BB3 correlation key for the already-discovered physical source.

It is:
- non-empty;
- trimmed;
- control-character free;
- at most 1024 characters.

It is not:
- a Recording MBID;
- a Release Track MBID;
- a provider ranking key;
- a URL;
- a local filesystem ownership claim.

A later playback adapter may map the source correlation to the currently resolved transport handle. BB3 does not own that handle.

## Virtual track identity

Each normalized CUE AUDIO track receives:

```text
VirtualTrackId = cue1-<sha256(WholeAlbumSourceId)>-track-NN
```

where `NN` is the validated CUE track number.

The source hash namespaces otherwise identical CUE track numbers across different physical sources. It is a deterministic correlation key, not musical identity.

The virtual ID only identifies an occurrence inside the normalized physical-source manifest. It never changes the requested `RecordingMbid` or `ReleaseTrackMbid`.

Track numbers must be one through 99 and strictly increasing within the accepted single-source CUE.

## CUE normalization

BB3 accepts CUE text plus an optional measured backing duration.

Required shape:
- exactly one `FILE` section;
- one or more `TRACK nn AUDIO` entries;
- exactly one `INDEX 01 mm:ss:ff` for every AUDIO track.

Album and track `TITLE` / `PERFORMER` values are supporting text evidence only.

Other legal metadata such as REM, ISRC, FLAGS, PREGAP, POSTGAP, or INDEX 00 does not define canonical identity and does not replace INDEX 01 range semantics in this slice.

### Time conversion

CUE `mm:ss:ff` is interpreted as:
- minutes >= 0;
- seconds 0..59;
- frames 0..74;
- 75 frames per second.

Conversion uses integer arithmetic rounded to the nearest millisecond.

The frame-sensitive fixture:

`00:10:74 -> 10987 ms`

This intentionally corrects the Tauon donor implementation observed using `ff / 100`.

### Start validation

Normalized INDEX 01 starts must be strictly increasing.

Therefore BB3 rejects:
- backwards starts;
- duplicate/equal starts;
- zero-length adjacent ranges;
- range overlap implied by non-increasing starts.

When backing duration is known, every start must be strictly before physical file end.

## End-boundary model

Every virtual track has one explicit end state.

### Adjacent track

For every non-final track:

```text
endMs = next track INDEX 01 start
endKind = NextTrackStart
```

This is deterministic and independent of provider order.

### Final track with known source duration

When physical duration is measured:

```text
endMs = backingDurationMs
endKind = BackingDuration
```

The duration must be positive and the final track must begin before it.

### Final track with unknown source duration

When the physical duration has not been measured:

```text
endMs = absent
endKind = OpenEnded
```

BB3 does not invent an album duration. Later playback may naturally reach source end.

## BB2 bridge

`CueWholeAlbum::toBb2Manifest()` exposes one `CandidateTrackEntry` per virtual CUE occurrence.

It may supply only evidence actually present or explicitly provided:
- lane-local manifest entry ID;
- CUE track number as track-position evidence;
- optional externally supplied medium position;
- track title;
- track performer or album performer;
- album title;
- measured virtual duration when an end is known;
- optional BB1 `SourceCandidateKey` provenance.

It deliberately does not populate:
- Recording MBID;
- Release Track MBID;
- Release MBID;
- Release Group MBID.

The whole-album physical filename is not passed as per-track filename evidence because it describes the backing source, not one virtual track.

BB2 then runs the existing evidence ladder unchanged.

## Exact slice binding

The public `resolveExactSlice()` seam constructs the BB2 manifest and invokes `ExactTrackResolver` with the same request in one operation. This avoids a stale-result join where a BB2 result from one request or physical source could be accidentally rebound to another.

BB3 may produce `ExactVirtualTrackSlice` only when:
- BB2 outcome is `Exact`;
- BB2 supplies one selected manifest entry;
- that entry belongs to the normalized CUE album.

The bound slice carries:
- `RecordingMbid` from the BB2 request;
- optional `ReleaseTrackMbid` from the BB2 request coordinate;
- separate `WholeAlbumSourceId`;
- separate `VirtualTrackId`;
- CUE track number;
- start bound;
- optional end bound plus its explicit end kind;
- BB2 exact evidence strength for diagnostics.

BB3 never reverse-derives canonical identity from the selected virtual entry.

## Typed failures

BB3 exposes typed boundary failures including:
- invalid or empty source identity;
- empty CUE;
- missing or malformed FILE;
- multiple FILE sections;
- TRACK before FILE;
- malformed/non-audio TRACK;
- invalid or non-increasing track number;
- no audio tracks;
- INDEX before TRACK;
- malformed INDEX;
- duplicate or missing INDEX 01;
- seconds outside 0..59;
- frames outside 0..74;
- timestamp overflow;
- non-increasing virtual starts;
- invalid backing duration;
- start at or beyond backing duration;
- invalid medium position;
- BB2 manifest bridge failure;
- non-exact BB2 result at bind time;
- BB2 selected entry absent from this CUE album.

Errors have stable snake_case names for deterministic diagnostics.

## Donor translation

Tauon contributes the useful architecture:
- one physical file may represent many logical tracks;
- INDEX 01 starts define virtual boundaries;
- the next start ends the previous track;
- physical file end closes the final track;
- external and embedded CUE inputs can share one normalization model.

BB3 explicitly rejects copying Tauon's GPL implementation and corrects its observed frame-divisor defect.

Tauon's same-file gapless transition machinery is not adopted here. Whether adjacent exact Vinyl tracks can advance without reopening mpv belongs to later playback/runtime qualification.

## Explicit non-goals

BB3 does not implement:
- BB1 source discovery or candidate ranking;
- a second exact-track matcher;
- provider fetching or authentication;
- torrent piece selection;
- URL/path/header resolution;
- mpv loading or end-bound enforcement;
- audible gapless verification;
- ReplayGain policy;
- fingerprinting;
- multi-FILE CUE normalization;
- canonical mapping from CUE metadata alone;
- queue/session ownership;
- persistence;
- QML/UI.

## Prototype

Reference implementation:

`backend/bb3-cue-whole-album-prototype/`

Files:
- `CueWholeAlbum.h`
- `CueWholeAlbum.cpp`
- `test_cue_whole_album.cpp`
- `CMakeLists.txt`

The prototype uses Qt Core plus the frozen BA0, BB1, and BB2 reference source. It does not mutate those upstream contracts.

## Native verification

Final verification was performed on the hardened source after the stale-result and virtual-ID collision review.

Configuration evidence:
- CMake requires `Qt6 6.11.1 EXACT` with `Core` only;
- generator: Visual Studio 17 2022, x64;
- compiler observed at configure time: MSVC 19.44.35223.0;
- `Qt6Core_DIR` in the generated cache: `C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6Core`.

Verified build:

```text
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build-vs --config Release --parallel 1
```

Result: exit 0. BA0, BB1, BB2, BB3, and the BB3 harness all compiled and linked.

Verified test:

```text
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-vs -C Release --output-on-failure -V
```

Result:
- `VINYL_BB3_CUE_WHOLE_ALBUM_OK checks=54`;
- 1/1 test passed;
- 0 tests failed.

Adversarial source audit:
- no QNetwork, QUrl, mpv, QtQml/QML, curl, or socket dependency appears in BB3 source/CMake;
- the BB2 bridge leaves Recording/Release Track MBIDs absent rather than manufacturing them;
- source-namespaced virtual IDs prevent identical CUE track numbers from colliding across physical sources;
- `resolveExactSlice()` invokes BB2 itself, so callers cannot inject a stale `ExactTrackResolution` from another request.

A compile-time raw-string delimiter defect in three regular expressions was found on the first native build and corrected before the verified build above. A later eight-way parallel rebuild was externally terminated without a compiler diagnostic during concurrent Wave 3 load; the serial rerun completed successfully and is the acceptance evidence.

## Acceptance matrix

The native harness must prove:
1. one physical source exposes multiple virtual tracks;
2. virtual/source identity is type-distinct from Recording and Release Track identity;
3. 75-fps conversion is deterministic and frame-sensitive;
4. adjacent ends equal the next INDEX 01 start;
5. the final known end equals measured backing duration;
6. the final unknown end remains explicit `OpenEnded`;
7. invalid frame timing fails closed;
8. backwards/equal starts fail closed;
9. multiple physical FILE sections fail closed;
10. missing INDEX 01 fails closed;
11. a start at/beyond known physical end fails closed;
12. the BB2 manifest bridge does not manufacture canonical IDs;
13. BB2, not BB3, selects the exact virtual occurrence;
14. the exact bound slice preserves the requested Recording/Release Track identities separately;
15. non-exact BB2 outcomes cannot be bound for playback;
16. two physical sources with identical CUE track numbers receive distinct virtual IDs;
17. the public exact-slice seam cannot accept an externally supplied stale BB2 result;
18. Qt 6.11.1 configures, compiles, and executes the harness without network, mpv, or UI dependencies.

Production Colosseum adoption is outside BB3 and must not be inferred from this prototype.
