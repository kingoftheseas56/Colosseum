# Vinyl V2 Song content design

Date: 29 October 2026.

Status: brainstorm locked and design explicitly approved by Hemanth on 29 October 2026. Implementation is authorized; provider and actual-app qualification remain separate acceptance gates.

This is a focused addendum to the [V2 consolidated plan](2026-10-29-vinyl-v2-consolidated-plan.md). It resolves the Song lyrics/annotation interactions discussed in the receiving conversation. The consolidated plan continues to govern the wider feature lane and later stages. Where the older [Song/Album snapshot](../references/vinyl-v2/2026-10-07-song-and-album-design.md) has a conflicting planner default, the locked decisions here take precedence.

## 1. Experience promise and scope

Put a song on, follow its words, and stop to read a passage's meaning without losing either the music or the reading position.

Keep the established Song composition: **lyrics on the left, artwork/title/artist in the center, annotations on the right**. The side columns occupy the space beside the artwork; opening them does not shift it. Text sits directly on the backdrop without surrounding cards or reading panels. The existing bottom player remains the only transport.

This design covers V2.0 provider/identity proof, V2.1 real timed/plain lyrics, and the reading/selection contract for V2.2 Genius annotations. Establish the real lyric document and clock first; connect Genius after that foundation is stable. Accepting the first lyrics slice does not claim Genius complete.

The Song view follows the playing song. Browsing an Album or another edition continues to respect the existing playing/browsing distinction.

### Ground truth

- The original Arc 62 `prototypes/62-vinyl-listening-room-thin-data/song-page.html` explicitly describes `lyrics | poster | notes` and records the rejection of lyrics over the poster. Hemanth reconfirmed that composition in this conversation.
- [VinylSongPage.qml](../../qml/vinyl/VinylSongPage.qml) implements that arrangement at widths of 1000 and above. Its current compact fallback combines text over the page; that fallback is not the approved responsive design.
- [VinylLyricsColumn.qml](../../qml/vinyl/VinylLyricsColumn.qml) contains lyric gestures but currently disables manual scrolling for timed lyrics and positions lines using a fixed line-height assumption.
- [VinylNotesColumn.qml](../../qml/vinyl/VinylNotesColumn.qml) distinguishes manual/all-note and automatic/selected-note presentation. Its current clipping does not establish that long manual notes are fully readable.
- [VinylLiveSession.qml](../../qml/vinyl/VinylLiveSession.qml) currently obtains lyrics and notes from record fields. Native BC1/BC3 contracts exist; their presence is not proof of a connected live content provider.
- The consolidated plan records normal-app entry/catalogue evidence and an unresolved shutdown heap-corruption defect. Neither constitutes fresh lyrics, annotations, or clean-shutdown qualification.

## 2. Primary user journey

### Open and follow

1. Start a song using the existing playback flow. Content retrieval runs independently and never delays audio starting.
2. Open Song. Keep its calm center composition and the existing session visibility preferences. Extras arriving later can enable their controls; arrival alone does not open a reading column.
3. Turn Lyrics on. Show genuinely matched timed lyrics when available, or readable plain lyrics without invented timing. Unavailable/instrumental content leaves the relevant control shaded and the composition sparse.
4. For timed lyrics, highlight the current line and follow it using its actual rendered position. Wrapping and different line heights must not make the view drift.
5. Single-click a timed line to seek. Recognize double/right-click as a distinct notes action, with no accompanying seek. Plain text has no timed seek action.

### Browse lyrics deliberately

Manual scrolling pauses lyric following and leaves the column where the listener scrolled. Playback continues. A small icon within the lyric region returns to the current line and resumes following; it does not seek the audio.

Lyric following and automatic annotations are independent. Scrolling lyrics does not itself pin an annotation or turn Auto off. Returning to the current lyric does not unpin manually opened notes.

### Read and pin annotations

Double/right-click an annotated fragment to show all its genuine, aligned notes in the right column. The selected lyric uses the existing gold treatment. Manual notes remain pinned while playback continues, until the listener opens another fragment, resumes Auto, or changes songs. Long notes and multiple notes can be scrolled independently within the right column.

Auto follows annotated passages in playback order. Prefer a genuinely verified artist contribution; otherwise choose a short, sufficiently supported explanation that fits the available reading interval. Longer explanations remain available manually. Unknown provider fields cannot stand in for votes, artist verification, or timing evidence.

Use the existing Annotations icon for these transitions:

| Current state | Press Annotations |
| --- | --- |
| A fragment is manually pinned | Clear the pin, enable/resume Auto, and show the reading columns |
| Auto is following, with no manual pin | Turn Auto off; leave Lyrics visibility unchanged |
| Auto is off, with no manual pin | Enable Auto and show the reading columns |

The icon's selected treatment represents automatic following. The manually selected lyric and all-notes presentation identify a pinned passage. Do not add a second resume control to the annotations column.

### Hide and return

Turning Lyrics off hides both reading columns without discarding the active song's reading state. Turning it back on for that same song restores a manually scrolled position and any pinned notes. If lyric following was active, reopening catches up to the current line; unpinned Auto likewise follows the current passage.

Leaving and returning to the Song page while the same song remains active preserves the same state. A new playing queue occurrence clears manual scrolling and pinned notes and starts following the new song, while retaining session Lyrics/Auto preferences. This also applies to a repeated recording appearing as a new queue occurrence. Restarting an audio source within the same occurrence is not a song change.

This is session reading state, not a new per-song reading-history system. Durable application-restart restoration is not added by this slice.

## 3. States, interruptions, recovery, and edge cases

| Situation | Required experience |
| --- | --- |
| Timed lyrics qualify | Current-line highlighting, following, manual browsing, return-to-current, and mapped seeking work |
| Only plain lyrics qualify | Readable, scrollable text; no invented timestamps, current-line highlight, or line seeking |
| Instrumental, absent, ambiguous, or wrong-version result | No unrelated lyrics/timing or fabricated content; relevant controls remain shaded |
| Lyrics/notes arrive after Song opens | Availability updates without opening columns or moving artwork |
| Request completes after skip or profile change | It cannot replace the new song's content, restore an old pin, or issue a seek |
| Same occurrence changes/retries its audio source | Retain valid content and reading state; update clock alignment from actual source evidence |
| Lyric document changes | Invalidate previous fragment alignment and positional anchors; never transfer a pin by ordinal line number |
| Verified cached content is available offline | Display that document with retained provenance; a failed refresh does not erase usable content |
| Annotation request fails | Audio and valid lyrics remain usable; do not invent an explanation |
| Repeated chorus or repeated text | Keep document occurrence identity; do not attach a referent to an arbitrary matching line |
| Song changes while a delayed click is pending | Cancel the pending action; the replacement song cannot receive that seek or notes action |
| Profile ends or changes | Clear/fence transient reading state and pending work through existing profile lifecycle ownership |

When replacing a lyric document, a prior manual position or note may be restored only if its exact anchor is verified against the replacement. Otherwise discard the invalid anchor and return to a valid current-document state. Do not display another document's notes while a replacement is being qualified.

Missing extras and temporary failures do not introduce explanatory absence panels, fake notes, or filler beneath the artwork. Provider problems must still be diagnosable through existing bounded diagnostics.

## 4. Controls, feedback, accessibility, and integration

- Retain icon actions, the source symbol, artwork-only Song navigation, the existing bottom transport, and house focus/hover treatments. Do not restore historical hover descriptors, textual source/quality chips, extra transports, or the removed artist-backdrop control.
- Keep the left/center/right arrangement at supported narrow/scaled desktop sizes. Adapt artwork size, gaps, wrapping, and column geometry while retaining readable text and the artwork's central position. Neither an overlay reading mode nor lyrics/notes stacked together is approved. Qualify the fit in the actual QML view.
- The lyric return icon appears when manual browsing has suspended following. Its action changes the reading viewport and following state, not playback time.
- Keyboard users can focus and activate timed lines, open a fragment's notes through the keyboard equivalent of its context action, scroll the reading regions, return to the current line, and operate the existing icons. Plain lyrics remain non-seeking.
- Provide accessible action names and visible focus. These do not require visible hover descriptions. Use existing Theme/component ownership rather than a separate visual system.
- Preserve the gold manual-selection indication, current-line emphasis, and quieter past/upcoming lines. Do not rely on color alone for keyboard focus or control availability.
- Render real annotation content safely, with source attribution and a usable source link. Untrusted HTML must not execute or alter the surrounding UI.
- The listener can finish reading a long manual note without automatic scrolling taking it away. Auto only presents an eligible note that satisfies the established reading policy.

The later official-video mode retains its existing contract: video replaces the artwork, hides both reading columns, and returns to the appropriate audio/session preferences. This specification does not implement that player handoff or change its alignment promises.

## 5. Technical shape and ownership

### Existing owners

Use native retrieval/presentation around BC1 and, subsequently, BC3. Bind the results into the existing Song, lyrics, notes, live-session, and player-bar components. Use current catalogue/identity, networking/cache, collection/session, and profile owners. Do not introduce another canonical identity store, playback engine, queue, or history system.

YouTube Music remains the primary catalogue. LRCLIB supplies timed/plain lyrics; Genius supplies published annotation material. Verified MusicBrainz mappings enrich identity but are not required for a valid catalogue song to exist, play, or be considered for enrichment. Resolve legacy `RecordingMbid` seams through existing ownership without manufacturing identifiers or maintaining a competing canonical identity.

### Content and request identity

Every retrieval/result carries enough existing context to distinguish:

- the provider/catalogue recording and selected version;
- relevant release/edition evidence;
- the playing queue occurrence and profile generation;
- the exact lyric document and its provenance/revision or content fingerprint;
- an annotation's exact source fragment and occurrence within that document.

Qualify matches using actual artist/title, available album/version and duration, and provider evidence. A provider search result or display-title resemblance alone does not establish version equivalence. Preserve ambiguity rather than silently accepting another edit, remix, cover, or performance.

A late result must pass the current context fences before publication. Source retries within the same occurrence may reuse qualified content, but cannot bypass document or clock checks. Transient stream URLs and headers never become durable song, document, or progress identity.

### Clock and gesture contract

Keep media, recording, and lyric time distinct. Convert playback seconds and content milliseconds explicitly. Apply each established media/recording, provider, and user offset once; invert the same mapping for lyric seeking. Unknown alignment remains unknown and cannot enable falsely synchronized seeking.

Use BC1's document/cue identity, occurrence request keys, and offset helpers instead of recreating their machinery in QML. Extend the existing typed seams as necessary for provider-backed recordings. Position the viewport from real delegate geometry rather than `lineIndex * fixedHeight`.

The gesture discriminator must prevent a recognized double-click from issuing the pending single-click seek. Cancellation covers drag/cancel, track/occurrence changes, document replacement, profile changes, and view retirement. Existing numerical delays are implementation defaults, not newly approved product constants.

### Cache, availability, and provider proof

Use the existing cache infrastructure. Retain provider record identity, match evidence, retrieval time, revision/fingerprint, expiry, and source attribution. Reuse previously qualified cached content offline; refreshing a document must not silently reuse its predecessor's annotation alignment.

Reading preferences/pins are profile-scoped transient state. Public content caching is not permission to share private session state between profiles. Credentials and resolved media resources remain outside durable personal records and logs.

Prove actual LRCLIB timed, plain, and absent/instrumental responses against actual selected catalogue versions. Prove Genius song/referent/body access, required secure configuration, attribution/display conditions, and the semantics of real vote/author/verification fields before promising automatic annotation behavior. Preserve genuine manual content where its alignment is established; do not synthesize missing metadata to make Auto select a note.

Exact tuning and the smallest compatible native API adaptation belong to implementation planning. Provider access, terms, or metadata limitations that materially alter the agreed experience must be brought back as evidenced tradeoffs rather than hidden scope changes.

## 6. Acceptance, non-goals, and decision record

### Observable acceptance

1. In the actual Colosseum Song view, opening either reading column leaves artwork centered. Lyrics remain left and annotations right, with no surrounding reading panels. Check 1280x720, 1600x900, larger desktops, and supported narrow/scaled windows.
2. A genuinely matched timed track works during play, pause, seek, skip, and revisiting Song. A genuine plain case remains readable without fake timing. An instrumental/absent case remains sparse. Record exact provider/version identity.
3. Manually scrolling timed lyrics holds the reading position through subsequent playback updates. The return icon recenters the current line and resumes following without seeking audio.
4. A single click seeks to the correctly mapped line. Double/right-click opens all aligned notes without a seek, including near the platform's gesture timing boundary. A pending gesture cannot affect a replacement song/document/profile.
5. Manual notes remain pinned across later annotated passages. The Annotations icon resumes Auto in one activation; the next activation while Auto is active turns Auto off. Long manual content remains readable.
6. Lyrics off/on and leaving/revisiting Song restore the same active song/document's manual reading position and pin. A following view catches up instead. New queue occurrences clear manual state and preserve session visibility/Auto preferences.
7. Skips, rapid changes, profile changes, source retries, slow/failing providers, cached offline use, repeated fragments, wrong versions, and lyric revisions cannot display or seek using stale identity/alignment.
8. Genuine Genius material retains attribution and links, renders safely, and uses only qualified metadata for Auto. Missing metadata or absent eligible notes never results in fabricated scoring, verification, or an explanation.
9. Keyboard and pointer journeys provide equivalent access to their applicable actions. There is one playback owner/transport; enrichment does not stall playback or replace the queue.
10. Report source revision, focused automated checks, actual normal-app evidence, and remaining gaps separately. A preview or contract test is not runtime acceptance. Carry the recorded baseline shutdown failure separately until its status is verified; do not claim clean integrated shutdown while it remains unresolved.

The lyrics milestone may be delivered before the Genius acceptance cases pass, with the latter explicitly outstanding. The annotations milestone requires its own genuine provider and normal-app evidence. Neither milestone implies merge, release, or deployment.

### Approaches and discarded alternatives

The selected approach extends the existing Song composition and backend owners. It keeps simultaneous lyric/meaning context and the established listening/navigation model.

- Overlay/switching reading sheets were rejected: they obscure or replace the composition Hemanth reconfirmed.
- Combining lyrics and notes below the artwork was rejected: it removes the two distinct side regions.
- Automatic snap-back after manual scrolling was rejected in favor of an explicit return icon.
- Automatically replacing a manually opened note at the next annotated line was rejected in favor of explicit Auto resumption.
- A second annotations resume control was rejected in favor of the existing Annotations icon.
- Per-song restoration of manual reading positions after changing songs was rejected in favor of clearing manual state for the new occurrence.

### Decision ledger

**Locked**

- Preserve lyrics left, artwork centered, annotations right, including the arrangement in supported narrow windows.
- Manual lyric scrolling suspends following until the small current-line return icon is activated.
- Manual notes remain pinned until explicitly replaced or Auto resumes; playback does not take them away.
- Use the existing Annotations icon to resume Auto from a pin and to turn Auto off when following.
- Hide both columns with Lyrics off; retain manual scroll/pin state for that same active song/document.
- On a new playing occurrence, clear manual state and follow the new song while retaining session Lyrics/Auto preferences.
- Preserve timed single-click seeking, double/right-click notes without seeking, and genuinely untimed plain lyrics.

**Constraints**

- Preserve the consolidated plan's provider split, native/QML ownership, replaceable providers, local core use, profile boundaries, document/version/clock safety, and optional verified MusicBrainz enrichment.
- Preserve one player/queue/transport, the source symbol, icon actions, artwork-only Song navigation, attribution, and sparse missing-extra behavior.
- The historical Arc mock is a layout reference. Later control/source corrections and the written decisions govern behavior; historical fixture content is not genuine provider evidence.
- Geometry, timing thresholds, cache mechanics, and native API details are implementation choices within these contracts. No additional user-facing mode or persistent reading-history feature is assumed.

**Deferred**

- Official video and live-performance implementation remain V2.3/V2.4 under the consolidated plan, including their separate identity/progress and explicit live choice.
- Translation, richer context/editions and signal information retain their later stages.
- Home/onboarding/search presentation, cross-edition like/stat display, recommendation changes, radio/autoplay, private-note publishing, and Android qualification are outside this slice.

**Open**

No unresolved product decisions for this slice. Live provider access, metadata semantics, matching, cache behavior, clock projection, responsive fit, and the recorded baseline defect remain engineering/qualification work, not claims of proven implementation.

### Spec self-review

Reviewed against the locked conversation decisions, the original Song layout reference, the consolidated correction ledger, and the relevant live QML/session source. The design records the accepted controls, state transitions, failure boundaries, rejected alternatives, scope split, and observable acceptance gates. Hemanth explicitly approved this design and authorized implementation with GLM High through ZCode, Luna Max for simpler slices, and the primary agent for complex integration.
