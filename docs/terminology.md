# Colosseum terminology

Colosseum spans Tankoban (manga/comics), Biblio (books/audiobooks), Theatre (video), and Vault (local media). This page defines terms shared across those surfaces; surface-specific vocabulary belongs here when it needs disambiguating.

## Language

**Collection**:
The persistent, world-scoped store of saved series (`Collection.items(world)`). A Collection entry records that a series was saved; it says nothing about reading progress or download state on its own.
_Avoid_: Library, Shelf (both refer to the presentation layer, not the store)

**Library (tab)**:
The consolidated tab per world that presents saved series together with their progress and download state. A Library tab is a *view* derived from Collection, Progress, and LocalDownloads; it owns no storage of its own.
_Avoid_: Collection (that's the underlying store, not the tab)

**Library row**:
A normalized, derived record joining one Collection entry with its matching Progress record(s) and download state, built fresh at render time for the Library tab. Never persisted.

**Progress**:
Persistent reading/playback position records, keyed by the series' real `seriesId` and a lane-specific `kind`.

**Reading lane**:
A progress lane is identified by its `kind`. Current Tankoban manga resume state uses the **volume lane** (`"tankoban"`). The former chapter-progress lane (`"manga"`) is legacy state removed by the Tankoban chapter migration; `"manga"` can still appear in download/media records and migration code, so do not treat every occurrence as current Progress state.
_Note_: the code's `"tankoban"` progress-kind string names the *volume lane*, not the Tankoban world/mode. Those are two different things sharing one word.

**seriesId**:
The stable identifier a source assigns to a series. Progress and comic Collection entries key by `seriesId`; manga Collection entries historically keyed by title instead (see [ADR 0001](adr/0001-manga-collection-entries-key-by-seriesid.md)).
