# Vinyl V2 roadmap and delivery plan

**Date:** 26 October 2026. **Status:** proposed delivery order grounded in the existing product decisions and live source. This document does not mark these features implemented or authorize starting every milestone.

**Goal:** Finish Vinyl's listening experience: real lyrics, Genius annotations, official music videos, live performances, translations, and the accompanying album/context features inside Colosseum.

**Architecture:** Reuse the existing catalogue identities, profile preferences, Collection, Session, Progress/history, network/cache infrastructure, mpv player and audio ownership. Adapt the linked BC/BF backend contracts through native presentation services into the existing Song, Album, Artist and player QML. Fetch enrichment independently of audio resolution so extras never hold up playback.

**Tech stack:** current C++/Qt/QML application; existing Python/yt-dlp transport for YouTube extraction; LRCLIB for lyric text/timing; Genius for published annotations; current catalogue and qualified MusicBrainz/context enrichment.

## 1. Product decisions carried forward

Sources read for this roadmap:

- Locked Song/Album brainstorm: `C:/Users/Suprabha/Desktop/Brotherhood/docs/superpowers/specs/2026-10-07-vinyl-song-page-and-album-page-design.md`.
- Provider split and donor research: `C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/62-vinyl/experience/DESKTOP-LISTENING-ROOM.md` and `research/FEATURE-DONOR-MATRIX.md` in that arc.
- Video/time-offset rulings: `C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/62-vinyl/claude-brainstorm-review-post-wave3/CLAUDE-REVIEW-RESPONSE.md`.
- Artist video rail direction: `C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/62-vinyl/experience/ARTIST-V4-COLLECTION-SHELVES-2026-10-14.md`.
- Existing umbrella roadmap: [Vinyl development roadmap](2026-10-20-vinyl-development-roadmap.md), especially M6 and its backend coverage map.
- Hemanth's later instructions in this conversation override older mock behavior.

The resulting scope is:

1. YouTube Music remains the primary catalogue. MusicBrainz enriches identities and context where a verified mapping exists.
2. **LRCLIB supplies timed/plain lyrics. Genius supplies the meaning/annotation plane.** Annotations here mean the planned Genius feature; a private-note editor or community publishing system is not part of this request.
3. Song starts with its large artwork/title/artist in the middle. Lyrics appear left; annotations right. Opening either does not shift the artwork. Retain the existing QML composition.
4. Single-clicking a timed line seeks; double/right-clicking opens all its annotations without seeking. Auto-annotation follows the song, prefers a verified artist contribution when genuinely available, and otherwise picks a short, sufficiently supported explanation that fits the reading interval.
5. The music video replaces the artwork. Lyrics and annotations are hidden during video. Audio/video switching preserves progress; extended videos do not establish exact musical alignment automatically.
6. Use the existing bottom player and its audio ownership. Controls are icons, without hover descriptors. The source indicator remains a symbol. Only the agreed artwork target opens Song; the seek bar and transport retain their own actions.
7. Missing extras leave shaded controls and the calm Song composition. No explanatory absence panels, invented metadata or filler descriptions.
8. Artist/album video rails remain stationary until operated. Reuse the agreed Theatre/Feria QML behavior and landscape presentation. Display official/live claims only with supporting evidence.
9. Initial Home asks for at least five artists and uses those choices for personalization. Preserve this prerequisite while adding the V2 extras.
10. The brainstorm already defines related live performances: a YouTube search ranked by Vinyl, an explicit candidate list/drop-down, deliberately loose sync, and a separate Continue Watching journey within Vinyl. V2 brings that previously deferred feature forward.

## 2. What exists today

This is source inspection of `codex/vinyl-shared-search` at `c04dd604f`, not a fresh runtime qualification.

| Area | Reusable implementation | Remaining work |
| --- | --- | --- |
| Lyrics | BC1 `TimedTextCore`; `VinylLyricsColumn.qml`; live session clock/cue helpers; click/double-click handling | Live lyric retrieval, version matching, cache/orchestration and reliable production clock projection |
| Genius annotations | BC3 `AnnotationMeaningPlane`; `VinylNotesColumn.qml`; `VinylNotePicker.js` | Genius transport, referent alignment, lazy bodies, real vote/author metadata and actual app binding |
| Translation | BC2 `TimedTextTranslation` with document/cue/language identity | Qualified translation source and runtime presentation |
| Music/live video discovery | Artist video feed in `provider.py`; existing Artist tabs and rails; BF2 relationship/eligibility contracts | Reliable classification and recording relationships; playable cards; complete paging and availability |
| Video playback | BF3 `VideoPlaybackAdapter`, existing source resolution and mpv infrastructure | Actual video extraction/rendering, position-preserving handoff and audio return |
| Credits/context | BF1 context graph, existing credit popup and album sections | Populate real roles, relationships, edition metadata and navigation |
| Signal information | BD1/BD2 source/decoder/output observation contracts | Map actual runtime facts into the existing presentation without unsupported quality claims |

`native/vinyl/backend/CMakeLists.txt` includes BC1–BC3, BD1–BD2 and BF1–BF3 in `colosseum_vinyl_backend`; that library is linked through the app. Runtime provider and UI integration still need to be completed.

Concrete current gaps:

- `VinylLiveSession.qml` obtains lyrics/notes from record fields. It has `hasVideo: false`, an empty video list and a no-op `toggleVideo()`.
- `VinylLiveArtistData.qml` marks provider video cards `playable: false`.
- The Song video frame is currently an artwork placeholder.
- The current yt-dlp resolution path requests `bestaudio`.
- The artist feed currently separates live videos using the word `live` in the title; that cannot prove a performance relationship or official status.
- BC3 does not currently expose the vote/artist-verification fields required by the agreed automatic note picker.
- Several older BC/BF contracts require a `RecordingMbid`. The live catalogue can have a valid YouTube Music identity without one. Adapt those seams to existing app/provider identities; never manufacture an MBID or make enrichment depend on a MusicBrainz match.

## 3. Delivery sequence

Each milestone is an integrated app slice with its own acceptance gate. When a milestone begins, turn it into a bounded implementation plan with exact edits and checks. Use Luna xhigh for small adapter/presentation tasks; retain identity, timing, playback handoff and receiving review with the primary implementer.

### V2.0 — Close the baseline and prove the provider path

**Outcome:** an agreed, buildable starting point and real source payloads for the features we intend to connect.

- [ ] Confirm the current main/feature-branch relationship before landing work. Shared search and the five-artist opener must not be assumed merged.
- [ ] Compile and qualify the new five-artist policy: 0–4 cannot complete, five or more can, duplicates do not count, old selections survive, preferences persist. Update existing qualification fixtures that still assume Skip/two artists.
- [ ] Carry a song context using existing recording/provider IDs, edition, queue occurrence, profile generation and a separate recording-relative clock. Add optional verified MusicBrainz/Genius/video mappings through the existing identity owner.
- [ ] Prove LRCLIB matching and Genius song/referent/body access on actual tracks. Record authentication/configuration requirements, returned fields, attribution, throttling and cache behavior. Keep app credentials out of source and avoid a separate compulsory listener account.
- [ ] Prove a real official-video candidate and a real live-performance candidate, with evidence for their relationship. Record absent-data cases too.

**Gate:** real payloads can be associated with the right song/version. Provider availability is demonstrated rather than inferred from existing contracts. The current missing Seagate `D:` build is an execution blocker to re-check, not a reason to change the architecture.

### V2.1 — Real lyrics in the existing Song page

**Outcome:** synced lyrics where available, readable plain lyrics otherwise.

**Reuse/owners:** BC1; existing networking/cache owner; `VinylLiveSession.qml`, `VinylBrowseSongData.qml`, `VinylSongPage.qml`, `VinylLyricsColumn.qml`.

- [ ] Add a native lyric retrieval/presentation adapter around BC1. Keep network work out of frame-by-frame QML and out of audio source resolution.
- [ ] Query LRCLIB with the actual title/artists and available album/duration; validate the returned version and duration. Prefer valid timed text, then plain text. Preserve instrumental/unavailable states.
- [ ] Bind playback seconds to BC1 milliseconds explicitly. Apply provider, user and media-to-recording offsets exactly once; retain unknown mappings as unknown.
- [ ] Connect line highlighting, variable-height line scrolling and click-to-seek. Keep the existing delayed single-click/double-click distinction. Plain lyrics never offer fabricated timing or seeking.
- [ ] Fetch/cache lazily; cancel stale work on track/profile changes. Restore lyrics visibility and relevant scroll/offset state for the session. Accept genuine word timing as a later enhancement within this slice only when supplied by the provider.

**Gate:** actual synced and plain tracks work while playing, paused, seeking, skipping, revisiting Song and using a valid cached result offline. A different edit/remix never receives another version's timing silently.

### V2.2 — Genius annotations and automatic meaning

**Outcome:** real Genius explanations attached to the correct lyric fragments.

**Reuse/owners:** BC3; `VinylNotesColumn.qml`, `VinylNotePicker.js`, lyric gestures and the existing Song/player controls.

- [ ] Resolve and cache the correct Genius song identity separately from its search/display title; reject ambiguous recording/version matches.
- [ ] Retrieve referents and annotation summaries, with lazy body loading. Add real source score, author and artist-verification semantics to the contract where needed. Unknown fields remain unknown.
- [ ] Anchor referents to the exact lyric document/text fragment and occurrence, including repeated choruses. A provider revision or document replacement invalidates old cue alignment. Unaligned material must not be pinned to an arbitrary line number.
- [ ] Double/right-click opens all explanations for that line. Auto mode follows annotated lines and applies the agreed selection/reading-time policy. Long explanations remain available manually.
- [ ] Preserve attribution and a source link. Render provider content safely. No generated explanations standing in for missing Genius material.

**Gate:** a real annotated track supports manual and automatic reading without unwanted seeks. Repeated fragments, changed lyric documents, missing score/verification fields and late responses cannot attach the wrong explanation.

### V2.3 — Official music videos and audio/video switching

**Outcome:** the Video action plays real frames in the Song artwork position using the existing player.

**Reuse/owners:** BF2/BF3; `VinylCatalogueSource`, existing yt-dlp transport, `VinylBackendSession`, mpv/audio ownership, `VinylSongPage.qml`, `VinylPlayerBar.qml`.

- [ ] Associate qualified official music videos with the recording. Treat related clips separately from candidates eligible for the primary Audio/Video switch.
- [ ] Extend the existing extraction/transport contract for actual audiovisual resources, including separate audio/video resources if required, headers and expiry. An audio-only URL cannot satisfy video playback.
- [ ] Connect the current mpv video's rendering surface to the Song frame. Reuse the one active playback owner and existing controls.
- [ ] Guard each asynchronous handoff by occurrence, generation and requested mode. Preserve pause state, volume, queue and progress. Apply a known video intro offset; preserve timestamp continuity without claiming exact musical alignment when it is unknown.
- [ ] Switching back restores the appropriate audio source and coherent progress. Failed or stale video resolution leaves a usable audio session. Ending one occurrence advances the queue once.
- [ ] Hide lyrics/annotations while video is displayed and restore their prior session preferences on return. Respect the latest artwork-only Song navigation rule.

**Gate:** actual video frames/audio, repeated switches, paused switches, seek, failure, expiry and skip-during-resolution all work in Colosseum without double audio, queue replacement or a restart at zero.

### V2.4 — Live performances and complete video browsing

**Outcome:** Artist Videos/Live and Album Videos become useful playback destinations.

- [ ] Replace title-only live classification with qualified relationships/evidence. Distinguish official video, live performance, acoustic/session clip, cover and unknown related material.
- [ ] Add song-level YouTube live-performance search and Vinyl's own deterministic ranking using matched artists/song, performance intent, channel/relationship evidence, relevant duration and availability. Offer the ranked candidates for explicit choice; ambiguity never becomes an automatic studio-to-live switch.
- [ ] Populate the existing landscape rails/tabs and continuations with real candidates and working activation. Keep ordering stationary until the user navigates or changes it.
- [ ] Treat a live performance as a different performance/version with its own duration and identity. Do not reuse the studio recording's lyric timing, Genius cue mapping or progress blindly.
- [ ] Use existing Session/player parking and return behavior for explicit related-performance viewing. Preserve the original listening queue and return state; record what was actually watched. A related performance is not a seamless alternate source for the studio recording.
- [ ] Persist video/performance progress through the existing Progress/Session owners and add the planned **Continue Watching** destination in Vinyl Home. Resume the exact video and timestamp independently of the audio listening checkpoint; keep unavailable entries recoverable without rewriting their identity.
- [ ] Handle unavailable/removed clips and distinguish recorded live performances from ongoing livestreams. Livestream support is a separate scope decision.

**Gate:** Song → ranked live candidates → exact performance → Continue Watching after restart, Artist → Videos/Live → exact clip → Back, and Album → Videos → Song work with genuine media, stable identities and intact listening context.

### V2.5 — Translation and multilingual presentation

**Outcome:** optional translated lyrics aligned with the original document.

**Reuse/owners:** BC2 and the existing lyric presentation. The translation engine/provider has not been qualified by the existence of BC2.

- [ ] Qualify the runtime translation source and language coverage before integrating it. Distinguish provider-authored and machine-generated translations when relevant.
- [ ] Retain original lyrics and cue timing; key translation by exact source document, language and generation. Preserve gaps rather than inventing missing lines.
- [ ] Make language selection work without disturbing playback or changing the original document. Exercise Telugu/Tamil and Latin-script content, wrapping and narrow layouts.
- [ ] Cache with provenance and invalidate when the source document changes. Romanization/transliteration is an optional later extension, not a promised capability of BC2.

**Gate:** changing language and lyric revisions never mixes lines, shifts timestamps or reuses another document's translation.

### V2.6 — Credits, context, album sections and editions

**Outcome:** populate the already designed music context rather than leave empty decorative sections.

**Reuse/owners:** BF1, catalogue identity/edition mappings, `VinylCreditsPopup.qml`, `VinylAlbumSections.qml`, artist links and current navigation.

- [ ] Fetch genuine recording/release/artist credits and role order; make individual collaborators navigate to their own pages.
- [ ] Populate About, Credits, Videos, Editions, More by this artist and Information using the locked Album design and real available fields. Do not invent label/catalogue numbers, biography or release facts.
- [ ] Group verified editions while preserving their distinct track lists, covers and history. Browsing an edition does not replace playing audio; recommendation aggregation follows verified recording/album grouping.
- [ ] Supply proven work/cover/sample/interpolation/remix relationships where available, retaining their direction and source evidence. Unknown relationships stay absent.
- [ ] Keep the credits popup bounded and scrollable. Maintain the agreed first-screen cover/track-list composition and existing section navigation.

**Gate:** real multi-edition albums and collaborator/context navigation work with playback continuing and no false merges or fabricated fields.

### V2.7 — Honest source and playback information

**Outcome:** useful technical information backed by runtime observations.

**Reuse/owners:** BD1/BD2 and actual mpv/device facts.

- [ ] Distinguish source encoding from decoded/output sample rate, channels and processing. Populate only values actually observed.
- [ ] Keep the source symbol as an indicator. Put expanded information behind the existing contextual interaction, without turning the HUD into a source-picker label or adding tooltip descriptors.
- [ ] Do not infer lossless output or bit-perfect playback from a FLAC container, requested processing settings or an audio-source label.

**Gate:** source and output facts remain correct across local, torrent, YouTube audio and video handoffs; unavailable measurements produce no invented claims.

### V2.8 — Whole-app qualification and delivery

- [ ] Use Eminem / “Without Me” for the existing Western Song/video reference; a Nas track for lyric/annotation and recording-version cases; “Mounamela Noyee” / Sagara Sangamam plus another non-Latin-script track for soundtrack/artwork/language behavior. Confirm live provider availability when selecting the exact test tracks.
- [ ] Include a plain-lyrics track, instrumental/no-extras track, extended official video, separate live performance, missing annotations and removed video.
- [ ] Exercise rapid track changes, source failure, double-click timing, revision mismatch, profile switch, restart, cached/offline operation and slow providers. Existing audio playback must remain independent of enrichment latency.
- [ ] Inspect actual app renders at 1280×720, 1600×900, large desktop and narrow/scaled windows. Verify artwork/HUD clearance, keyboard access and all visible controls.
- [ ] Land accepted changes into current main, build from main and repeat the integrated listening journey. Report authored, built, runtime-qualified, merged and released states separately.

**Gate:** the whole Song/Album/Artist experience works in the normal app on the shipped build, not only in a QML preview or an adapter harness.

## 4. Build order and parallel work

**Primary order:** V2.0 → V2.1 → V2.2 → V2.3 → V2.4 → V2.5 → V2.6 → V2.7 → V2.8.

- Video candidate qualification can progress alongside lyrics/annotations once V2.0 establishes identity and clock boundaries. Handoff integration stays sequential with one owner.
- Translation waits for stable lyric documents. Automatic Genius mapping waits for exact text/document identity. Live performance viewing waits for video playback and explicit version/session behavior.
- Small QML rows, field projections and bounded adapter tasks suit Luna xhigh. Source identity, Genius alignment, clock conversion and player handoff need primary receiving review.
- Radio/autoplay remains the separate M7R sequencing project from the umbrella roadmap. Do not silently add it, a private-note platform, a new search API, social accounts or Android qualification to this V2 listening scope.
- Lyrics beside a playing video remains deferred from the locked design. First deliver the agreed video mode and qualified clock mapping; any simultaneous video/lyrics layout is a subsequent explicit design decision.

**Recommended first implementation slice:** V2.0 provider/identity proof, followed immediately by one real track's LRCLIB lyrics in the actual Song page. That creates the document/clock foundation for Genius, which follows next. No new visual design exercise is needed to begin.

## 5. Provider references and unresolved gates

- [Current LRCLIB API documentation](https://lrclib.net/docs): timed/plain lyrics, track matching, duration and response/failure semantics. Checked during roadmap preparation; no live song requests were made.
- [Genius developer documentation](https://docs.genius.com/): intended qualification reference. The documentation page could not be retrieved during this planning pass. Supported live annotation fields, access configuration and display requirements remain a V2.0 proof item; the Genius product choice is already established by the brainstorm.
- The backend contracts and existing QML are reusable inputs. No new live lyrics, Genius, translation or audiovisual playback qualification was performed while writing this roadmap.
