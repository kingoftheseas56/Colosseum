# Vinyl Backend BC1 - Timed Text Contract

Status: production-intent reference contract; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl BC1

## Purpose

BC1 defines the smallest provider-neutral timed-text domain required for future lyric providers and playback-time synchronization.

The canonical media identity is BA0 RecordingMbid. Timed text enriches that Recording; it never becomes playback authority, source identity, queue authority, history, or canonical music identity.

BC1 consumes BA0 without modifying it.

## Basis inspected

- backend/README.md
- backend/BA0-DOMAIN-CONTRACT.md
- donor-reviews/feishin/DONOR-ASSESSMENT.md
- donor-reviews/innertune/DONOR-ASSESSMENT.md
- Arc 62 README.md
- BA0 Qt/Core value-model prototype
- Feishin isolated timed-lyrics behavior prototype

The donor material is prior art for behavior only. BC1 is a clean-room Qt/C++ contract.

## Domain laws

1. RecordingMbid is the canonical identity of the music being enriched.
2. ProviderId and optional ProviderItemId are immutable provenance, not canonical identity.
3. TimedTextKind stays explicit: unavailable, plain, line-synced, or word-synced.
4. Plain text is genuinely untimed. BC1 never invents timestamps to make it fit a synchronized shape.
5. Unavailable is a valid result, not an exception and not an empty plain lyric disguised as success.
6. A synchronized cue has stable cue identity, a non-negative start, and an optional explicit end.
7. Missing non-final cue end derives only from the next real cue start. Missing final end stays open-ended.
8. Word timing is optional data nested beneath a parent cue. It cannot rewrite the parent cue interval.
9. Cue and word intervals use half-open semantics: start is inclusive and end is exclusive.
10. Provider offset and caller-supplied global offset use one sign convention and add to one effective offset.
11. Timed text observes playback position. It does not own playback state or issue seeks itself.
12. A playback discontinuity is handled by a fresh deterministic lookup at the new position.
13. Asynchronous results must prove they still belong to the requested Recording and exact BA1 QueueOccurrenceId before becoming current.
14. TimedTextDocument carries a revision-scoped TimedTextDocumentId derived from provider, provider item when present, and normalized document content; a provider or content revision cannot silently reuse cue identity from another document.
15. Source-aware synchronization uses W4-A recording time: recordingMs = trackRelativePlaybackMs + recordingTimeOffsetMs. Unknown recordingTimeOffsetMs stays unknown and disables synchronized highlight/seek rather than guessing.
16. Translation belongs to BC2. Annotation and meaning belong to BC3.

## Value model

### TimedTextDocument

A document contains:

- RecordingMbid
- TimedTextDocumentId
- TimedTextKind
- TimedTextProvenance
- providerOffsetMs
- either plainText or synchronized cues, according to kind

The four valid shapes are:

- unavailable: no text and no cues;
- plain: non-empty plainText and no cues;
- line-synced: one or more validated cues without word timing;
- word-synced: one or more validated cues, with at least one nested word interval somewhere in the document.

The model deliberately does not contain layout, animation, playback-engine, network, cache, history, translation, or annotation state.

### TimedTextDocumentId

TimedTextDocumentId identifies one exact lyric/timed-text document revision. The reference prototype deterministically hashes versioned identity material containing RecordingMbid, ProviderId, optional ProviderItemId, TimedTextKind, providerOffsetMs, and the normalized plain/cue/word payload. Identical evidence reproduces the same ID; provider, item, timing, or content changes produce a different ID.

Cue IDs remain unique only inside this document identity. A matching cue string in another TimedTextDocumentId is not the same referent.

### TimedTextProvenance

Provenance contains:

- BA0 ProviderId;
- optional BA0 ProviderItemId identifying the provider-owned lyric/source object.

ProviderId keeps BA0 normalization. ProviderItemId remains opaque and case-preserving. Neither may substitute for RecordingMbid.

### Cue identity

TimedTextCueId is an opaque, bounded identifier unique inside one document.

Provider adapters may preserve a real source cue ID or deterministically derive one from source-local structure such as the provider object plus ordinal. Cue identity must not require a fabricated timestamp.

### Cue timing

A TimedTextCue contains:

- TimedTextCueId;
- text;
- startMs;
- optional endMs;
- zero or more TimedTextWord values.

Rules:

- startMs is non-negative;
- cue starts are strictly increasing in supplied order;
- BC1 does not silently sort malformed input;
- explicit endMs must be greater than startMs;
- explicit endMs may equal the next cue start;
- explicit endMs may not extend past the next cue start;
- if a non-final cue has no explicit end, its effective end is the next real cue start;
- if the final cue has no explicit end, it has no synthetic end.

An explicit end before the next cue creates a legitimate gap. Current-cue lookup returns no cue during that gap.

### Word timing

TimedTextWord contains text, startMs, and endMs on the same lyric clock as its parent cue.

Rules:

- word text is non-empty;
- startMs is at or after parent cue start;
- endMs is greater than word start;
- if the parent has an effective end, word end cannot exceed it;
- word intervals are monotonic and non-overlapping;
- adjacent word intervals are valid;
- a line-synced document rejects nested words;
- a word-synced document requires at least one nested word interval, but individual cues may still lack word detail.

BC1 preserves parent line timing even when richer word timing exists.

## Offsets and synchronization

BC1 separates track-relative playback time from recording time using W4-A's frozen convention:

recordingMs = trackRelativePlaybackMs + recordingTimeOffsetMs

lyricClockMs = recordingMs + effectiveOffsetMs

effectiveOffsetMs = providerOffsetMs + globalOffsetMs

recordingTimeOffsetMs belongs to resolved transport timing metadata, not to the lyric document and not to canonical identity. A source with a 10 s intro before Recording time zero carries -10000 ms. Unknown recordingTimeOffsetMs remains unknown: source-aware current-cue lookup and click-to-seek return no synchronized result.

A positive provider/global offset advances the lyric clock relative to recording time. A negative offset delays it. providerOffsetMs is immutable source-normalization data on the document. globalOffsetMs is supplied by the synchronization caller for current-session or user calibration and is not baked into provider provenance.

Integer operations saturate at qint64 limits rather than overflowing.

## Current-cue lookup

Synchronized documents are validated in strictly increasing start order. Current lookup therefore uses binary search over cue starts.

For a known recording position:

1. calculate lyricClockMs from recording time plus the effective provider/global offset;
2. find the last cue whose start is less than or equal to lyricClockMs;
3. reject it if lyricClockMs is at or beyond its effective end;
4. return no cue before the first cue or inside an explicit gap.

Plain and unavailable documents never yield a current timed cue.

This lookup is stateless. A seek, restart, queue jump, or other playback discontinuity calls the same lookup at the new recording position and immediately gets the correct cue. The source-aware helper first applies recordingTimeOffsetMs. No linear catch-up or animation history is part of the backend contract.

## Click-to-seek target

A cue does not perform a seek. BC1 only calculates the playback target that a playback owner may choose to use.

At the recording-time layer the inverse is:

recordingSeekTargetMs = max(0, cueStartMs - effectiveOffsetMs)

For a source with known recordingTimeOffsetMs:

trackRelativeSeekTargetMs = max(0, recordingSeekTargetMs - recordingTimeOffsetMs)

The same offsets used for highlighting are therefore inverted for click-to-seek. An invalid cue index or unknown recordingTimeOffsetMs has no source-synchronized seek target.

## Stale asynchronous result guard

BC1 does not own the queue. It defines a small caller-bound guard:

TimedTextRequestKey = RecordingMbid + QueueOccurrenceId

QueueOccurrenceId is the BA1 occurrence identity. BA2 PlaybackWorkToken generation is intentionally not part of the timed-text request key, so a same-occurrence audio/video/source restart does not discard a still-valid lyric fetch.

A completed document may become current only when:

- the request key still equals the caller's current key; and
- the document RecordingMbid equals the request RecordingMbid.

This rejects both a response for an old QueueOccurrenceId of the same Recording and a response for a different Recording, while preserving ownership across a same-occurrence source restart.

## Validation failures

BC1 exposes lane-local failures for:

- invalid cue identity;
- invalid document shape;
- invalid timing;
- non-monotonic cue timing;
- overlapping cue or word timing;
- invalid word timing;
- duplicate cue identity.

Provider/network failures remain provider-lane concerns. BA0 identity parsing errors remain BA0 errors.

Normalization must fail malformed timing rather than sorting it into plausible-looking data.

## Explicit non-goals

BC1 does not:

- fetch LRCLIB or any live provider;
- choose a preferred lyric provider;
- translate text or carry translation fields;
- merge translation into original text;
- implement annotations, referents, or meaning;
- own playback, queue state, or seek execution;
- write history or listen events;
- define a cache schema;
- define presentation or animation behavior;
- define any current design-reference layout.

## Prototype

Reference implementation:

backend/bc1-timed-text-prototype/

Files:

- TimedTextCore.h
- TimedTextCore.cpp
- test_timed_text.cpp
- CMakeLists.txt

It uses Qt Core plus the frozen BA0 value types. It does not edit BA0.

## Acceptance contract

BC1 is accepted when the native harness proves:

1. plain lyrics remain explicitly untimed;
2. line-synced current-cue lookup is deterministic at boundaries;
3. word-synced data preserves parent cue timing;
4. positive and negative provider/global offsets behave deterministically;
5. click-to-seek subtracts effective offset and clamps at zero;
6. a playback discontinuity immediately resolves the correct cue;
7. backwards timing fails validation rather than being sorted;
8. a stale Recording/occurrence request cannot silently become current text;
9. provider provenance survives normalization without replacing Recording identity;
10. unavailable lyrics are a valid result and never masquerade as text;
11. TimedTextDocumentId is deterministic for identical evidence and changes on provider/content revision;
12. same-occurrence source restart preserves the TimedTextRequestKey while a different QueueOccurrenceId is stale;
13. known recordingTimeOffsetMs aligns playback to recording time and click-to-seek inverts it;
14. unknown recordingTimeOffsetMs yields no synchronized cue or source-aware seek target.

The harness also exercises overlapping-timing rejection and stable enum/error names.

Production Colosseum adoption is a later state and must not be inferred from this prototype.

## BA0 compatibility verdict

No BA0 reconsideration is required.

BC1 can express every required concept with additive lane-local types while consuming BA0 RecordingMbid, ProviderId, and ProviderItemId unchanged.
