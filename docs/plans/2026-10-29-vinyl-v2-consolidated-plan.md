# Vinyl V2 consolidated development plan

Date: 29 October 2026

Status: consolidated product decisions, source inventory and delivery gates. The V2 runtime features below remain planned work. Hemanth has separated V2 feature development from the Home and UX discussion in the originating chat.

Vinyl V2 adds real lyrics, Genius annotations, official music videos and explicit live-performance viewing to the existing Colosseum app. It reuses the current player, queue, identity mappings, Collection, Session, Progress, profile preferences and networking. Enrichment must run independently of audio acquisition and must never delay a song starting.

## Scope and lane ownership

| Area | V2 feature lane | Home and UX lane |
| --- | --- | --- |
| Lyrics and Genius | Retrieval, matching, document identity, timing, annotation alignment and functional QML binding | Review any proposed change to the established Song composition |
| Official and live videos | Candidates, classification, audiovisual resolution, player handoff, resume and functional activation | Layout and visual polish of Artist and Album video surfaces |
| Continue Watching | Exact video identity, progress, resume operation and data projection | Placement and presentation of the Home row |
| Home and onboarding | Consume existing preferences and preserve profile boundaries | Artist onboarding, five-artist requirement, personalized rows, discovery and navigation |
| Shared app files | Coordinate bounded changes to Song, Album, player/session, CMake and native wiring | Coordinate visual or navigation changes touching the same files |

The core V2 sequence is V2.0 through V2.4. Translation, credits/context/editions and signal information remain the later stages already proposed in the original roadmap. They are preserved here without making every later stage a prerequisite for accepting the first lyrics slice.

Home redesign, recommendation ranking changes, radio/autoplay, a custom search API, a private-note platform and Android qualification are separate work. The historical donor matrix contains further ideas such as sonic search, visualizers, booklets, ReplayGain and gapless playback; their presence in research does not add them to this lane.

## Decisions to preserve

### Providers and identity

- YouTube Music is the primary catalogue. MusicBrainz enriches verified identity and context; a valid catalogue song must not require a MusicBrainz match to exist or play.
- LRCLIB is the planned timed/plain lyric provider. Genius is the chosen meaning and annotation provider. A Genius lyric-scraping path is not a replacement for the lyric timing plane.
- YouTube audiovisual resources are resolved by Colosseum's existing yt-dlp transport and played through the existing mpv ownership. Do not introduce a browser player, a second queue or a parallel playback engine.
- Preserve the current Tankorent/local/YouTube source behavior while adding extras. V2 is not permission to make playback wait for enrichment or to redesign source selection.
- Retain the selected recording/version, release or edition, queue occurrence and profile throughout a journey. A related live performance has its own identity and duration.
- Keep durable provider/catalogue IDs separate from transient stream URLs and headers. Do not save resolved URLs as library or progress identity.

### Song and Album behavior

- Retain the existing calm Song composition: artwork, title and artist in the middle; lyrics on the left; annotations on the right. Opening a side column does not move the artwork.
- A single click on a timed lyric seeks. Double-click or right-click opens all annotations for that fragment without seeking. Plain lyrics do not acquire invented timestamps or seek actions.
- Automatic annotations follow annotated passages. The agreed policy favors a genuinely verified artist contribution, otherwise a short sufficiently supported explanation that fits the reading interval. Long explanations remain available manually.
- The official video replaces the artwork in a wider frame. Lyrics and annotations are hidden during video. Returning to audio restores their session preferences.
- Audio/video switching preserves the listener's position, pause state, volume and queue. The same timestamp is not proof of exact musical alignment in an extended video; apply an offset only when it is established.
- Use the one persistent bottom transport. Actions use icons; the source indicator is a symbol. The agreed artwork target opens Song. The seek bar, empty HUD surface and transport buttons retain their own actions.
- Missing extras leave shaded controls and the sparse composition. Do not fill absence with explanatory panels, fake notes, fabricated credits or descriptive subheadings.
- Keep the Album cover and first track list visible together. Retain the designed About, Credits, Videos, Editions, More by this artist and Information sections when real data supports them. Reuse the existing section-navigation component.
- Browsing another edition does not interrupt the playing edition. Queue entries and history retain edition context. Recommendation grouping uses verified album/recording equivalence without erasing edition-specific history.
- Keep individual collaborator identities and links. A collaboration display string is not a single artist identity.
- Video rails are stationary and controlled by the user. Theatre's moving showcase strip is a visual reference, not a ready-made per-video picker. Activate the exact selected candidate and show official/live claims only when supported.

### Later corrections override older mock behavior

| Earlier material | Current interpretation |
| --- | --- |
| The early three-pane Listening Room with permanent source/context panels | Superseded by the separate calm Song and Album design. Do not restore the rejected room. |
| The October 7 text allows the whole bottom bar to open Song | Later instruction narrows navigation to the agreed artwork target. Scrubbing must never navigate. |
| The October 7 text proposes hover names and a textual quality chip | Later instructions require icon actions, no hover descriptors, and a source symbol. |
| Older mock track rows contain additional details or strong selection boxes | Preserve the subsequent simplified rows and house hover/keyboard-focus behavior. Do not reintroduce those mock treatments while wiring content. |
| Old source states use textual source descriptions | Preserve the current indicator and buffering behavior; do not reintroduce a source-picker label. |
| An old artist reference exposes a backdrop picker | Hemanth removed that entire option and its user-facing wording. Automatic artist imagery is separate from that removed control. |
| The October 7 Song/Album scope says no artist photographs | Applies to those screens, not a ban on the separately designed Artist hero and its automatic artist photographs. |
| Historical HTML says a mock wins on proportions | Current user corrections and the actual QML direction take precedence. HTML and preview fixtures are historical references, not acceptance evidence. |
| MusicBrainz-only catalogue language in early backend reviews | Superseded by YouTube Music as primary catalogue. Adapt legacy typed seams through existing identity owners. Never fabricate a MusicBrainz identifier. |
| Live performances are deferred in the October 7 design | Their already agreed explicit-choice journey is brought into V2.4 by the October 26 roadmap. Simultaneous video and lyrics remains deferred. |
| The old V2.0 list includes implementing five-artist onboarding | Shared search and onboarding are dependencies owned with the Home/UX lane under the new split; do not silently absorb that feature branch. |

### Defaults that are not new product rulings

The archived design distinguishes Hemanth's decisions, accepted proposals and planner defaults using H, C and P markers. Preserve that distinction. Existing code uses a 250 ms single-click delay and a note picker with 10 upvotes, 210 words per minute and 500 ms reading lead. These numbers are implementation defaults, not a claim about Genius metadata or a newly locked tuning decision.

The receiving conversation has locked the Song reading interactions and reconfirmed the original left-lyrics / centered-artwork / right-annotations layout. See the approved [Song content design](2026-10-29-vinyl-v2-song-content-design.md) for manual scrolling, pinned notes, Lyrics off/on restoration, Auto resumption and song-change behavior. Hemanth approved the written design and authorized implementation on 29 October 2026.

The three-region arrangement also governs supported narrow windows; artwork, spacing and column geometry adapt without introducing the rejected overlay or combined stacked-text layouts. Current Song QML hides the side columns below width 1000 and uses a compact combined fallback, so responsive fit remains implementation/qualification work. Cross-edition like/stat display remains outside this first content slice. Other old P markers are not automatically new product rulings.

## Source and runtime baseline

Remote refs were refreshed on 29 October 2026:

- `origin/master`: `f6255f943ec78fef050115655402601b9a8677ed`.
- `origin/codex/vinyl-shared-search`: `6f3242f158a2bdca00c64559f91e398de9a73abc`.
- The feature branch still has three commits outside master: `5a70e2d2b` shared search, `c04dd604f` five-artist onboarding, and `6f3242f15` the V2 roadmap. This consolidation imports the roadmap only.

The ordinary local master checkout was reconciled at `e727e7f08c0ae285647f9d0eb0fea0dba2065373`. Its committed native/QML tree matches the remote baseline. It was built with Vinyl enabled, qualification mode off and synthetic fixtures off; its missing Python catalogue runtime was staged. Vinyl opened and an actual Nas artist search returned real YouTube Music results. This proves entry and catalogue search, not V2 lyrics, Genius, video or fresh playback acceptance.

The same app reproduced a shutdown heap-corruption failure, `0xC0000374`, on graceful Quit. Its cause remains unresolved. Carry it as an observed baseline defect and a final qualification gap; do not attribute it to a new V2 change without evidence. The 1.1.10 release packaging gate is separate from ordinary master/development availability.

### Reusable code and remaining wiring

All backend paths below are under `native/vinyl/backend/`. BC1–BC3, BD1–BD2 and BF1–BF3 are already included in `colosseum_vinyl_backend`, which is linked through `colosseum_vinyl_app`. A linked contract is not a live content provider.

| Feature | Existing implementation | Remaining work |
| --- | --- | --- |
| Lyrics | `bc1-timed-text-prototype/TimedTextCore.h`; `VinylLyricsColumn.qml`; live session cue helpers | Provider retrieval, match qualification, orchestration/cache and production clock projection |
| Document and clock safety | BC1 already has `TimedTextDocumentId`, occurrence-based requests and recording-offset helpers; BC2 references the source document | Use these contracts; close the runtime identity/time seams without recreating the core |
| Genius | `bc3-annotation-prototype/AnnotationMeaningPlane.h`; `VinylNotesColumn.qml`; `VinylNotePicker.js` | Song/referent/body transport, exact text alignment and real score/author/verification semantics |
| Translation | `bc2-translation-prototype/TimedTextTranslation.h` | A qualified translation source, provenance and runtime presentation |
| Related video | `bf2-related-video-prototype/RelatedVideoContract.h`; existing artist media feed | Relationship evidence, classification, availability, paging and working activation |
| Video playback | `bf3-video-playback-adapter-prototype/VideoPlaybackAdapter.h`; existing source/session/mpv machinery | Audiovisual transport, real frame rendering, handoff and coherent audio return |
| Credits and context | `bf1-context-graph-prototype/VinylContextGraph.h`; existing Credits popup and Album sections | Real credits, role ordering, editions and relationship projection |
| Signal information | `bd1-signal-path-prototype/` and `bd2-mpv-observability-prototype/` | Actual runtime observations wired into the existing presentation |

Live source checks still show:

- `qml/vinyl/VinylLiveSession.qml` reads lyrics/notes from record fields, declares `hasVideo: false`, and has an empty `toggleVideo()`.
- `qml/vinyl/VinylLiveArtistData.qml` projects provider video cards with `playable: false`.
- `qml/vinyl/VinylSongPage.qml` contains a video artwork placeholder rather than a real rendering surface.
- `scripts/vinyl/provider.py` resolves with `format: bestaudio`.
- `Annotation` does not expose the real votes and artist-verification fields the QML automatic picker expects.
- Several BC/BF interfaces still require `RecordingMbid` while live catalogue identities need not have a verified MusicBrainz mapping.

## Implementation rules

1. Use the existing identity mapping owner. Match recording/version using available artist, title, album, duration and provider evidence; keep ambiguity explicit. Repeated choruses and alternate edits must not collapse by display string.
2. Bind annotations and translations to the exact lyric document and fragment occurrence. A corrected lyric document invalidates prior cue alignment. An ordinal cue number is not a durable cross-provider anchor.
3. Separate media time, recording time and lyric time. Convert seconds/milliseconds explicitly; apply source, provider and user offsets once, and invert the mapping for lyric seeking. Unknown alignment stays unknown.
4. Fence asynchronous work by the appropriate recording/document, queue occurrence, profile generation and requested mode. A late response cannot replace another track's data or alter a new profile. Retain valid lyric content across a source restart of the same occurrence.
5. Route source replacement through the existing session/coordinator. Preserve one audio owner, one queue advance at EOF and one coherent listening history across a handoff.
6. Use existing network/cache and profile owners. Retain provenance, revisions, expiry and meaningful cached/offline states. Keep transient media resources and secrets out of durable personal records and logs.
7. Treat provider access and content display as a qualification task. Record required configuration, returned fields, attribution, cache behavior, failures and availability. Use existing secure configuration ownership where needed; core local music must remain usable without a compulsory provider account.
8. Render annotation content safely. Preserve source attribution and links. Neither generated prose nor unsanitized donor HTML substitutes for genuine Genius material.

## Delivery stages

| Stage | Work | Acceptance gate |
| --- | --- | --- |
| V2.0 Provider and baseline proof | Refresh branch/runtime facts; establish song context; obtain actual LRCLIB, Genius, official-video and live-performance samples; resolve configuration and matching gaps | Real payloads are associated with the correct song/version. Pending access or missing metadata is recorded honestly. |
| V2.1 Real lyrics | Adapt BC1 through a native presentation service into the existing Song page; timed/plain/instrumental states; follow/seek; cancellation and cache | One real timed song and one plain/no-timing case work during play, pause, seek, skip, revisit and cached offline use. Wrong versions never receive timing silently. |
| V2.2 Genius annotations | Resolve the Genius song; referents and lazy bodies; exact-document alignment; real metadata; manual and automatic reading | Manual gestures do not seek. Repeated fragments, changed documents, missing votes/verification and late responses cannot produce false alignment. |
| V2.3 Official music videos | Qualify the recording relationship; resolve real audiovisual resources, headers and expiry; render with existing mpv; switch and return | Real frames and audio work. Repeated/paused switches, seeks, failure and skip-during-resolution preserve the queue and avoid double audio or a reset to zero. |
| V2.4 Live performances | Song-level YouTube search with Vinyl ranking and explicit choice; working Artist/Album activation; exact progress and return state; Continue Watching contract | Resume the exact performance after restart, retain the original listening context, and handle removed media. Home row presentation is coordinated with the Home lane. |
| V2.5 Translation | Qualify the translation source; key language/cues to exact original document; preserve gaps and provenance | Language or lyric revision changes never mix documents or shift timing. Transliteration and word-level effects require their own supported data. |
| V2.6 Context and editions | Real credits/roles, Album sections, verified editions and factual sample/work/cover/remix relationships | Individual collaborators and editions navigate correctly while playback and edition-specific history stay intact. |
| V2.7 Signal information | Project real source, decoder and device observations through BD1/BD2 | Distinguish encoding from output. Do not infer bit-perfect/lossless output from a container name or requested settings. |
| V2.8 Integrated qualification | Combine the accepted slices in the normal app and target main build | Actual Song/Album/Artist journeys, persistence, failure/offline cases, responsive access and shutdown meet the recorded gates. Report source, build, tests, runtime, merge and release separately. |

Begin with V2.0 and one V2.1 lyric journey. Video candidate qualification can overlap after identity and clock boundaries are established. Genius depends on exact lyric documents; translation follows stable lyrics; live viewing follows working video/session behavior. Do not start all stages at once.

### Qualification cases

Retain the original cross-market references: Eminem / Without Me; a qualified Nas track; Mounamela Noyee / Sagara Sangamam; and another non-Latin-script song. Select exact provider versions when qualifying rather than assuming availability.

Cover timed/plain lyrics, instrumental/no extras, extended official video, a genuinely separate live performance, missing annotations and removed media. Exercise rapid track changes, edition/version mismatch, double-click timing, profile changes, restart, slow providers, expiry and cached offline use. Check 1280 by 720, 1600 by 900, larger desktops and narrow/scaled windows. Provider requests and rendering must not stall audio playback.

## Previous material and provenance

The two preserved source documents retain their original wording. The correction ledger and current lane split above govern their interpretation. Research remains evidence, not Codex operating instructions.

| Material | Location | Contribution |
| --- | --- | --- |
| Original V2 roadmap | [October 26 roadmap](2026-10-26-vinyl-v2-roadmap.md), recovered unchanged from `6f3242f15`; Git blob `b32a3167b033268ac3c398e8421f429dcee14f75` | Full V2.0–V2.8 sequence and observable gates |
| Original Song and Album design | [October 7 design snapshot](../references/vinyl-v2/2026-10-07-song-and-album-design.md); copied from `Brotherhood/docs/superpowers/specs/2026-10-07-vinyl-song-page-and-album-page-design.md` | Locked behavior, H/C/P distinctions, Album sections, edition semantics and rejected alternatives |
| Umbrella development plan | [October 20 roadmap](2026-10-20-vinyl-development-roadmap.md), especially M6, M7R and the coverage map | Backend adoption and separation of listening extras from recommendations/radio |
| Desktop Listening Room | `Preflight-Architect/arcs/62-vinyl/experience/DESKTOP-LISTENING-ROOM.md` | Lyric/meaning provider split and cross-market fixtures; its earlier room layout is superseded |
| Backend review and subsequent rulings | `Preflight-Architect/arcs/62-vinyl/claude-brainstorm-review-post-wave3/CLAUDE-REVIEW-RESPONSE.md` | Document identity, media/recording offsets, occurrence-safe handoff and position preservation; older findings are checked against current code |
| Artist video direction | `Preflight-Architect/arcs/62-vinyl/experience/ARTIST-V4-COLLECTION-SHELVES-2026-10-14.md` | Stationary Theatre/Feria-inspired landscape selection and exact per-video activation; later artist layout corrections still apply |
| Feature research | `Preflight-Architect/arcs/62-vinyl/research/FEATURE-DONOR-MATRIX.md` | Feature-by-feature donor map and distinction between reusable ideas and reference apps |
| Detailed donor decisions | `Preflight-Architect/arcs/62-vinyl/donor-reviews/{music-assistant,muffon,feishin,tauon}/DONOR-MAP.md` | Provider, lyric, annotation, timing and licensing findings summarized below |
| Current implementation | Native/QML/provider paths listed above at the refreshed master baseline | Actual reusable code and runtime wiring gaps |
| Later user corrections and lane split | Originating Vinyl conversation through 29 October 2026 | Icon-only actions, no hover descriptors, artwork-only Song navigation, simplified rows, five-artist onboarding and the two-lane boundary |

### Donor findings to reuse

- Music Assistant's `providers/lrclib/__init__.py` supplies a useful title/artist/album/duration lookup and synced/plain fallback precedent. Its Genius lyrics wrapper does not prove a complete annotation implementation.
- muffon's `genius/track/info/lyrics.rb` and `genius/annotation/info.rb` illustrate referent extraction and lazy annotation retrieval. Its raw `v-html` rendering was rejected. DOM scraping, access and attribution remain independent qualification questions.
- Feishin provides useful ideas for normalized timed lines, source/offset ownership, discontinuity resynchronization and manual-scroll follow behavior. Its donor review identifies a click-to-seek offset inversion omission; do not reproduce it. Existing BC1 is the implementation starting point.
- Tauon's LRC and keyboard-seek behavior is a reference. Its Genius lyric HTML scraping and replacement playback engine were rejected as Vinyl implementation directions.
- Prior donor assessments call for independent implementation or license-qualified reuse. Revalidate any proposed direct code reuse against the exact donor revision; service/content permissions are separate from source-code licensing.

Provider documentation and live responses must be checked when V2.0 starts. No current Genius access, translation service or audiovisual playback qualification is established by this consolidation.
