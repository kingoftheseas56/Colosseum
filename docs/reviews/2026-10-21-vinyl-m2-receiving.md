# Vinyl M2 receiving — real catalogue and Artist integration

**Status:** Windows development-app catalogue/Artist receiving accepted on `codex/vinyl-production-flow`. Main integration and release remain separate.

**Catalogue authority:** YouTube Music is primary. MusicBrainz supplies optional identity, release-group and version enrichment; it never replaces the provider catalogue or becomes a prerequisite for local listening. Playback policy remains local when available, then qualified torrent preferred, with YouTube as fallback. M2 does not close the M3 transport gate.

## Delivered

- The ordinary app now uses a native catalogue facade over the existing local source and provider. Search artists/albums with provider-neutral labels, open real Artist pages, browse Albums/Singles/Videos/Live, and open full albums and exact provider versions.
- Reuse the authored helper's artist reads, complete release continuations and MusicBrainz grouping. Both artist release shelves and video playlists request all continuations. A videos shelf without a continuation handle remains explicitly partial; global search is explicitly limited.
- Keep provider artist IDs, release IDs, recordings and Vault file IDs distinct. Only MusicBrainz-returned identities enter the accepted `ArtistServiceBridge`; canonical playback commands remain unavailable without an authoritative concrete edition/recording mapping.
- Adapt the existing Artist bridge to the current Collection owner. Artist saves retain a stable `ytm:artist:<channel>` key during asynchronous identity lookup; optional MusicBrainz IDs enrich that record's payload. Existing queue/session/history/player owners remain authoritative.
- Artist Play/Shuffle fetches and admits the first listed release through the existing session. It does not invent a tracklist or claim an artist-wide radio sequence. Previously loaded local music continues while catalogue and canonical group pages load.
- Add public-metadata cache fallback and visible cached/error/loading states. Resolved playback URLs and request headers are excluded from this cache. Browse, queued albums, playback and Artist channels have independent request/cancel fences; profile changes invalidate pending Artist state.
- Package a private Windows runtime contract: official Python archive digest, seven hash-locked wheels, isolated import smoke and an app-relative launch path. CMake stages `provider.py` and can copy a prepared runtime via `COLOSSEUM_VINYL_RUNTIME_DIR`. Missing online dependencies preserve local use.

## Exact build

Base: `409f225fce3fd7dcca102b020de3358be56994d7` (M1).

- App: `C:\b\vinyl-production-build\colosseum.exe`
- EXE SHA256: `b4f0b8ae9f55f150085739a751631bfa80fe0db826ca3f8d9f800c38ff961afe`
- QML tree SHA256: `7aaa96c8a63d9cc30aa9c3c2fcf9fc02e997d9b43f8f9ce2943ea8abb8c4621c`
- Qualification and fixture build flags **OFF**, no Vinyl feature-enable or developer environment override. Runners use the existing Qt/mpv runtime and OpenGL renderer.
- Affected native consumers, including the shared Collection header's eight application consumers, were rebuilt and linked. Compilation was reduced to one job under memory pressure; unrelated processes/worktrees were preserved.

The source/evidence manifest pins this delta and the unchanged M1 baseline. Build artifacts, audio, profiles, runtime payload, screenshots and raw logs remain local.

## Focused verification

- **9/9 CTest targets passed:** existing personal-state, local source, tagged import, native catalogue facade, native session bridge, full native world QML, native facade, actual mpv events and feedback snapshot.
- Full world: **41 QtTest cases**; native catalogue: **8 cases**, including setup/cleanup. The latter covers optional MusicBrainz failure while primary catalogue albums remain usable.
- Python: existing helper suite **13 passed, one optional live-smoke skipped**, plus **6 new catalogue contracts passed**. Actual live provider receiving below supersedes the need to infer network behavior from mocked tests.
- Four initial contracts failed against pre-M2 helper source before passing. Additional accent and credit-alignment regressions also failed before their fixes.
- Both new PowerShell files parse; runtime lock validation and isolated runtime imports were checked. Production launch uses the app-owned interpreter, not PATH Python. The preparation host needs Python/pip only at build time.

Evidence root: `D:\Colosseum-Vinyl-M2-20261020`; final suite: `qualified-ctest.log/xml`. The run started October 20 and final receiving continued October 21.

## Actual application journeys

All runs inherit actual Main.qml, use native registrations and real profile stores, and drive the production world commands. They are not standalone previews or fixture-fed catalogue pages. Each final phase exited **0**.

### Real catalogue and cached restart — two phases

Run: `vinyl-m2-9233489a00974f2a9445deca2b83be58`.

- Real search returned **12 artists and 24 albums**; Eminem's known channel identity selected the Artist route.
- Raw provider continuations supplied 21 albums and 26 singles. Authored version grouping yielded **15 album groups and 25 singles**, retaining exact provider choices; **105 videos and 4 live rows** were projected from the 109-item video list.
- All **317 MusicBrainz release groups** traversed their bridge pages. These enrich the provider results; they are not inserted as an alternate primary catalogue.
- Nas FLAC playback continued during Artist loading, at 24.104 seconds when continuity was asserted.
- Saved the artist before identity completed. After enrichment and after restart there was exactly one saved artist under its original provider key, with the MusicBrainz ID as external metadata.
- Opened **STANS (The Official Soundtrack)** with 12 actual tracks, then selected the other exact provider version `MPREb_6MMDcE8iF3T`. Artist Play admitted its real 12-track release into the existing queue; receiving suspended before remote audio resolution. This is queue admission proof, not remote audio decoding proof.
- Exercised all four Artist tabs, Lifafa's smaller catalogue (two albums/one single), unknown-artist handling and cancellation of a replaced Artist request.
- Restarted the same disposable profile with the helper executable intentionally unavailable. Search, Artist tabs, canonical group pages, saved state and the cached album tracklist remained usable; the UI explicitly reported cached/offline data. Queue restored paused with no player.
- Captures for all tabs and both live/cached album views are retained; final selected Albums and Lifafa captures were visually inspected.

### Local listening without the helper — three phases

Run: `vinyl-m1-25adf758180842d2b44d3e7b02fbea70`.

With the helper intentionally unavailable throughout: Hemanth's ten Illmatic CD1 FLAC files imported as one album, partial reimport preserved identities, Library/Album/Song/Artist/Back worked, FLAC playback/output and qualified history persisted, cold restore/resume succeeded, and a separate profile remained empty. Record/restore/isolated phases all exited normally.

### Existing playback regression — three phases

Run: `vinyl-m0f-fba19f93f0674419b5da6895fa94d21a`.

Collection membership, same-occurrence replacement, duplicate/move/shuffle queue behavior, repeat-one at real EOF, serialized seeks, qualified history bounds, paused restore/resume, privacy disable and clear/empty restart passed with PCM/WASAPI output. Record/restore/empty phases all exited normally.

## Receiving fixes and decisions

Hemanth's selected-state follow-up: compared `WorldTabBar`, `TheatreTabBar`, `KeyboardAction` and `FocusRing`. Artist tabs now use the existing gold selected-pill treatment with dark text, room for counts and the shared outside focus ring. Selected Vinyl buttons use the shared primary-button separation gap. Selection remains accessible and independent of keyboard focus. The world suite covers the selected-state transition. One repeated native popup test exposed keyboard input before Window activation; waiting for actual activation before sending keys restored the full 9/9 suite (the failure receipt is retained).

The first receiving repeat after that visual change (`vinyl-m2-5ecc60e82e7842468f7f961f667e4728`) returned real YouTube Music search results but failed its enrichment assertion because MusicBrainz timed out. That failed receipt is retained. Optional-service failure is covered separately by the native primary-catalogue regression; the live enrichment assertion was not weakened.

Fresh read-only Astra review found three Important issues, all repaired with reproducing regressions:

1. Async identity changed the artist save key. Keep provider identity stable and persist MusicBrainz as enrichment; QML and native Collection-port checks passed after failing on the original code.
2. Missing credit IDs shifted the artist-name/ID arrays. Preserve aligned empty placeholders; the Python regression passed after failing.
3. Related shelves/category routes discarded known channel IDs. Carry structured provider identity through both routes; both QML cases passed after failing.

Actual offline receiving exposed a fourth issue: Windows `FailedToStart` could finish inside `request()` before the facade recorded its fence. The native regression reproduced zero completions; deferring launch fixed it, and actual cached restart subsequently passed. Prior failed/interrupted receipts are retained and superseded by the named final runs.

**Rulings:** use current Collection/session ports instead of the donor factory's independent personal bytes/player ownership; reject unmapped canonical playback rather than guessing a recording. Stable provider save keys may leave separate provider channels as separate entries until explicit identity reconciliation is implemented.

## Boundaries

- Search is a limited set of matches, not an asserted complete global search. Artist release/video continuation receiving is separate and was exercised.
- MusicBrainz title-based version grouping records its mapping method; it is not proof of an exact canonical recording or concrete release occurrence. Unavailable/ambiguous identity leaves provider browsing usable.
- Video/live rows are catalogue metadata (live classification follows supplied titles), not BF2 relationship attestation or in-app video playback qualification. Top-song/artist-wide sequence refinements remain later UI/library work; no fabricated popular-song list was introduced.
- Remote audio decoding, torrent-first resolution and YouTube fallback qualification remain M3; related in-app video playback remains M6. The M2 Artist Play receiving proves real album/queue admission only.
- Windows picker clicking remains the separate unverified M1 check. M2 used production commands programmatically. Output observations do not establish human ear audibility.
- This is a Windows source/development-app result with a portable helper payload. Installer wiring, clean-machine distribution, other platforms, final visual polish, main merge and release are not established by these runs.

Runtime references: [official Python 3.14.8 release](https://www.python.org/downloads/release/python-3148/), [ytmusicapi browsing API](https://ytmusicapi.readthedocs.io/en/stable/reference/browsing.html). The checked archive/wheel hashes are pinned in the preparation script and lock rather than following a moving latest release.
