# Preflight Architect: Biblio Library Advisory Handoff

**Repository:** `kingoftheseas56/Colosseum`  
**Baseline:** `master` at `ae29768dcb0cb1a07c1be524aec10ca5fee9f60a`  
**Status:** advisory only; do not implement until the decision gates are resolved.

## Recommendation

Add a retained third Biblio tab:

```text
Discover | Explore | Library
```

One Library row should represent one saved book work keyed by Biblio `pairKey`.

Authorities:

- Collection entry: membership, Details, Remove.
- Exact matched `kind = "book"` Progress record: reading Resume.
- Books, BookTorrents, and Audiobooks: local availability.
- Derived Library row: projection only; do not persist it.

Keep the existing Continue Reading and Your Collection shelves.

## Confirmed findings

- `BiblioWorld.qml` already retains Discover and Explore.
- Theatre Library is a useful structural reference but contains video-only domain logic.
- Biblio Collection uses `pairKey`.
- Existing global book Progress may use provider ID or local path, so exact-ID joins are insufficient.
- Existing Continue routing handles `kind = "book"`.
- Existing Collection routing opens Biblio Details.
- Local availability is split across normal books, BookTorrents, and Audiobooks.
- Shared Biblio LocalDownloads does not cover every local form.
- The inspected architecture does not establish an honest standalone audio-only Resume route.
- The Library should use shared fixed-metric artwork and fallback components.

## Decision gates

1. **Card identity:** one card per Collection `pairKey`; downloads decorate the row.
2. **Future Progress identity:** prefer `pairKey` for future global book Progress; preserve provider IDs and paths in metadata; keep Reader2 file identity unchanged.
3. **Legacy matching:** canonical pairKey first, then resume metadata, derived title+author pairKey, provider ID, normalized title+author, and title-only only when author is absent. Ambiguous matches must not Resume.
4. **Downloaded meaning:** any local form, with separate Ebook and Audio badges.
5. **Availability ownership:** derive from each Biblio backend; add only the smallest missing torrent inventory seam.
6. **Primary action:**
   - valid reading Progress path → Resume;
   - no valid Resume + local ebook → Read;
   - audio-only or no local form → Details.
7. **Remove:** remove only Collection membership; do not delete files or Progress.
8. **Page architecture:** Biblio-specific pure derivation module and retained page, using shared lower-level visual primitives.
9. **Visual contract:** fixed metrics, ordered cover candidates, bounded decode size, stable placeholder, author at rest, progress and format badges.
10. **Browse controls:** title/author search; All, In Progress, Not Started, Downloaded filters; Last Read, Added, A-Z, Author sorts.

## Evidence required before design freeze

Produce:

- identity table for Collection pairKey vs Progress ID/resume metadata;
- availability matrix for normal ebooks, torrent ebooks, and audiobooks;
- routing map for Resume, Read, Details, and Remove;
- visual component-fit matrix;
- performance baseline for snapshots, file checks, retained construction, and artwork.

## Product questions

- Does Downloaded include audio-only? Recommendation: yes, with Audio badge.
- Is Details acceptable for audio-only v1? Recommendation: yes.
- Should an unstarted local ebook open immediately? Recommendation: yes.
- Should Remove leave Continue Reading intact? Recommendation: yes.
- Should downloaded but unsaved works appear automatically? Recommendation: no.
- Are ledger counts wanted? Recommendation: optional.

## Stop conditions

Return for review if:

- one Progress record can resume several works;
- audio-only requires a new global playback architecture;
- file validation causes GUI stalls;
- pairKey is not stable enough;
- shared visual reuse requires media-specific branches affecting other worlds.

## Required next output

A **Biblio Library Design Decision Brief**, not code or an implementation plan, containing approved behavior, row identity, legacy matching, action matrix, availability ownership, component boundaries, failure handling, open questions, and acceptance criteria.

## First action

Inspect representative shapes from:

```text
Collection.items("biblio")
Progress.recent("book", 0)
Books.downloadedBooks()
BookTorrents persisted download index
Audiobooks.downloadedAudiobooks()
```

Build the identity and availability tables before freezing a design.
