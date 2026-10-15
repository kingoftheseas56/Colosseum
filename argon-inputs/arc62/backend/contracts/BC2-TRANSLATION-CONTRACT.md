# Vinyl Backend BC2 - Timed Text Translation Contract

Status: production-intent reference contract; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl BC2

## Purpose

BC2 defines translation as an enrichment layer over BC1 timed text.

BC1 remains the sole timing authority. BC2 never creates, estimates, corrects, sorts, or substitutes a lyric clock. For synchronized lyrics, translated text attaches to the existing BC1 cue identity. For plain lyrics, translation remains plain and untimed.

BC2 consumes BA0 and BC1 without modifying either contract.

## Basis inspected

- `backend/README.md`
- `backend/BA0-DOMAIN-CONTRACT.md`
- `backend/BC1-TIMED-TEXT-CONTRACT.md`
- `donor-reviews/innertune/DONOR-ASSESSMENT.md`
- `donor-reviews/feishin/DONOR-ASSESSMENT.md`
- `backend/ba0-domain-prototype/`
- `backend/bc1-timed-text-prototype/`

InnerTune contributes the behavioral rule that synchronized translation preserves the source cue grid. Feishin and BC1 contribute the stronger cue identity and stale-generation model. BC2 is a clean-room Qt/C++ contract.

## Domain laws

1. `RecordingMbid` remains canonical playable-song identity.
2. Original BC1 timed text remains authoritative and independently available.
3. Translation is optional enrichment. Absence is valid.
4. Translation provider identity is provenance, never canonical identity.
5. A synchronized translation references a BC1 `TimedTextCueId` inside one exact BC1 `TimedTextDocumentId`; it does not own `startMs`, `endMs`, offsets, word timing, or any independent clock.
6. Translation text that happens to contain timestamp-looking characters is still only text.
7. Partial synchronized translation is valid.
8. Missing and explicitly untranslated cues are valid and never fabricate replacement text.
9. Plain lyric translation remains untimed.
10. Translation never replaces the original lyric text.
11. Translation completion must still belong to the exact source request, target language, and translation request generation before it may become current.
12. Translation service choice, transport, caching, UI, playback, annotation, and meaning remain outside BC2.

## Target language

`TranslationLanguage` is a bounded BCP47-shaped language token used to identify the requested translation language.

The prototype accepts:

- non-empty text;
- no surrounding whitespace;
- a maximum total length of 63 characters;
- hyphen-separated non-empty ASCII alphanumeric subtags;
- a maximum of eight characters per subtag.

It normalizes the token to lowercase for deterministic request identity.

BC2 does not perform language detection and does not claim full BCP 47 semantic validation. A future provider adapter may supply a more deeply validated tag before entering this boundary.

## Translation provenance

`TranslationProvenance` contains:

- BA0 `ProviderId`;
- optional BA0 `ProviderItemId` identifying the provider-owned translation result, model result, cache object, or equivalent provenance token.

Provider IDs keep BA0 normalization. Provider item IDs remain opaque and case-preserving.

Translation provenance is independent from BC1 lyric provenance. The lyric provider can differ from the translation provider without either becoming canonical music identity.

## Translation document

`TimedTextTranslationDocument` is keyed to one BA0 `RecordingMbid` and one exact BC1 `TimedTextDocumentId` revision.

It also records:

- the BC1 source `TimedTextDocumentId`;
- the BC1 source `TimedTextKind`;
- target language;
- translation provenance;
- either optional plain translated text or synchronized cue-translation entries.

The document intentionally does not own timing fields.

### Synchronized shape

For a BC1 line-synced or word-synced source, each `CueTranslation` contains:

- the existing BC1 `TimedTextCueId`;
- a translation state;
- translated text only when the state is `Translated`.

A cue translation never contains a timestamp.

Every referenced cue ID must exist in the supplied BC1 source document. A translation document may contain each cue ID at most once. The translation also retains that source document's TimedTextDocumentId, so an equal cue ID in another provider/content revision is not interchangeable.

Entry order is not timing order and carries no clock meaning. BC1 cue order remains authoritative.

### Cue states

BC2 distinguishes three projected states:

- `Missing`: no translation entry exists for this BC1 cue;
- `Untranslated`: the translation result explicitly records that no replacement translation is available for this cue;
- `Translated`: non-whitespace translated text exists.

Both `Missing` and `Untranslated` produce original-only content. They remain distinguishable so later provider/cache policy can reason about absence without inventing text.

### Partial translation

A synchronized translation may contain zero, some, or all BC1 cue IDs.

There is no requirement that adjacent cues translate together, and missing coverage never shifts one cue's translated text onto another cue.

## Timing authority and projection

BC2 stores no synchronized timing.

When a consumer needs an original-plus-translation row, `projectTranslationCue()` combines:

- the BC1 cue ID;
- BC1 original text;
- BC2 translation state/text;
- BC1 cue start;
- BC1 effective cue end.

The start and effective end are read from the original BC1 document at projection time.

Therefore:

- translated text cannot introduce a timestamp;
- translation entry order cannot retime lyrics;
- missing translation cannot create a synthetic interval;
- BC1 offset and word-timing semantics remain untouched;
- projection first requires the current BC1 TimedTextDocumentId to equal the translation's sourceDocumentId;
- a provider/document correction that changes TimedTextDocumentId cannot inherit translations by cue-name coincidence.

BC2 does not translate individual BC1 word timing in this slice. A word-synced source may receive line-level translation attached to its parent cue ID while BC1 continues to own every word interval.

## Original-text independence

BC2 never copies translated text into the BC1 original document.

A projected row can expose original and translated text side by side, but the source values remain separate:

`BC1 original cue -> original text + timing`
`BC2 cue reference -> optional translated text`

This permits original-only, translated-only, or bilingual presentation later without destructive replacement.

## Plain lyrics

A BC1 `Plain` document may receive:

- one optional translated plain-text body;
- target language;
- translation provenance.

It receives no cue list, timestamps, offset, or synthetic line timing.

A missing plain translation is valid.

BC2 rejects timed-cue projection for a plain translation because there is no BC1 cue grid to project.

## Stale asynchronous result guard

BC2 composes with BC1's existing stale-work key rather than inventing another queue identity.

`TranslationRequestKey` is:

```text
TimedTextRequestKey
  = RecordingMbid + QueueOccurrenceId
+
source TimedTextDocumentId
+
targetLanguage
+
translationRequestGeneration
```

A translation result may become current only when:

1. the completed request key equals the caller's current translation request key;
2. the translation document `RecordingMbid` equals the requested BC1 Recording;
3. the translation document sourceDocumentId equals the requested TimedTextDocumentId;
4. the translation document target language equals the requested target language.

The caller increments `translationRequestGeneration` whenever work from an older translation request must be fenced off, including replacement of an in-flight translation/provider attempt for the same Recording and target language.

BC1 QueueOccurrenceId remains responsible for distinguishing playback occurrences while intentionally surviving same-occurrence transport restarts. BC2's request generation only scopes translation work for the same occurrence/document/language.

## Validation failures

BC2 exposes lane-local typed failures for:

- `InvalidTargetLanguage`;
- `InvalidDocumentShape`;
- `SourceKindMismatch`;
- `SourceDocumentMismatch`;
- `RecordingMismatch`;
- `InvalidTranslationText`;
- `DuplicateCueId`;
- `UnknownCueId`.

Stable snake_case names are exposed by the prototype.

BA0 identity parsing failures remain BA0 failures. BC1 cue/timing validation failures remain BC1 failures. Provider/network failures remain future provider-lane concerns.

## Explicit non-goals

BC2 does not:

- call any translation service or network endpoint;
- choose a translation provider;
- perform language detection;
- define a translation cache or persistence schema;
- implement annotations, referents, meanings, or explanations;
- implement QML or presentation;
- own playback, queue state, or seek execution;
- alter BC1 cue timing, offsets, word timing, or cue identity;
- introduce independent synchronized timestamps;
- replace original lyrics;
- change canonical Recording identity.

## Prototype

Reference implementation:

`backend/bc2-translation-prototype/`

Files:

- `TimedTextTranslation.h`
- `TimedTextTranslation.cpp`
- `test_translation.cpp`
- `CMakeLists.txt`

The prototype uses Qt Core plus the existing frozen BA0 and BC1 value types. It has no network, QML, playback, persistence, annotation, or translation-service dependency.

## Acceptance contract

BC2 is accepted when the native harness proves:

1. translated synchronized lyrics preserve original BC1 cue identity and timing;
2. partial translation is valid;
3. missing translation does not fabricate text;
4. translated text cannot introduce new timestamps or alter the BC1 clock;
5. translation provider provenance survives normalization without replacing Recording identity;
6. stale translation results cannot attach to another Recording, QueueOccurrenceId, TimedTextDocumentId, target language, or translation request generation;
7. a lyric provider/document swap that reuses the same cue IDs fails with SourceDocumentMismatch rather than migrating translations;
8. original lyrics remain available independently of translated text;
9. plain lyrics remain untimed after translation.

The harness also covers explicit untranslated state, invalid target language, unknown cue references, duplicate cue references, blank translated text, stable error/state names, and source-kind mismatch.

Production Colosseum adoption is a later state and must not be inferred from this prototype.

## BA0 and BC1 compatibility verdict

No BA0 or BC1 change is required.

BC2 expresses every required concept with additive BC2-owned types while consuming:

- BA0 `RecordingMbid`, `ProviderId`, and `ProviderItemId`;
- BC1 `TimedTextDocument`, `TimedTextKind`, `TimedTextCueId`, cue timing projection, and `TimedTextRequestKey`.

BC1 remains the timing authority throughout.
