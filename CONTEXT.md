# Colosseum

Colosseum is Hemanth's unified media app across three modes — Tankoban (manga/comics), Biblio (books), and Theatre (video). This file defines terms shared across those modes; mode-specific vocabulary belongs here too once it needs disambiguating.

## Language

**Collection**:
The persistent, world-scoped store of saved series (`Collection.items(world)`). A Collection entry records that a series was saved — it says nothing about reading progress or download state on its own.
_Avoid_: Library, Shelf (both refer to the presentation layer, not the store)

**Library (tab)**:
The consolidated tab per world (Theatre has one; Tankoban is gaining one) that presents saved series together with their progress and download state. A Library tab is a *view* derived from Collection, Progress, and LocalDownloads — it owns no storage of its own.
_Avoid_: Collection (that's the underlying store, not the tab)

**Library row**:
A normalized, derived record joining one Collection entry with its matching Progress record(s) and download state, built fresh at render time for the Library tab. Never persisted.

**Progress**:
Persistent reading/playback position records, keyed by the series' real `seriesId` and a lane-specific `kind`.

**Reading lane**:
One of the independently-tracked ways a manga series can be read: the **chapter lane** (Progress kind `"manga"`) or the **volume lane** (Progress kind `"tankoban"`). A single series can have progress in both lanes at once.
_Note_: the code's `"tankoban"` progress-kind string names the *volume lane*, not the Tankoban world/mode — those are two different things sharing one word. Keep them apart in speech even though storage reuses the string.

**seriesId**:
The stable identifier a source (WeebCentral, GetComics, GCD, LOCG) assigns to a series. Progress and comic Collection entries key by `seriesId`; manga Collection entries historically keyed by title instead (see [ADR 0001](docs/adr/0001-manga-collection-entries-key-by-seriesid.md)).
