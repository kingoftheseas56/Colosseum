# Phase 1 cloud — decisions required / investigation records

## WT-01 — library multi-selection: DECISION_REQUIRED (not implemented)

Evidence (current source, Beta after Phase 1 commits):

- Theatre library removal is no longer a plain membership removal. `TheatreWorld.LibraryPage.onRemoveRequested`
  → `theatre.libraryRemovalRequested` → `Main.requestTheatreRemoval(entry)` (qml/Main.qml) opens
  `TheatreRemovalDialog` (qml/TheatreRemovalDialog.qml), whose only removal path is
  `StremioActions.removeTheatreItem(id, type, alsoStremio)` with two explicit user choices:
  "Remove from Colosseum" and "Remove from Colosseum & Stremio" (`removeFromColosseumAndStremioAction`).
- Biblio removes membership inline (`Collection.remove("biblio", id)` in qml/BiblioWorld.qml); Tankoban's
  owner (`TankobanWorld`) performs `Collection.remove` for `TankobanLibraryTab.removeRequested`.
- Alpha's `LibrarySelectionHost.applyRemove()` calls a synchronous `removeEntry(entry)` per selected row with
  one generic confirmation ("Downloaded files and reading/watch progress stay on this device"). It predates
  the Stremio-aware Theatre owner and cannot express the per-item `alsoStremio` choice.

Decision needed from Hemanth before implementing the coherent three-library unit:

1. Theatre bulk Remove: one confirmation offering "Colosseum only" / "Colosseum & Stremio" applied to all
   selected titles, OR keep Theatre bulk actions to Mark watched/unwatched only (no bulk Remove), OR route each
   selected title through the existing per-item dialog sequentially.
2. Whether bulk "Colosseum & Stremio" is acceptable when some selected titles are not Stremio-linked
   (`StremioActions.removeTheatreItem` failure handling per item; Alpha retained failed rows selected).

Everything else in the unit is unblocked and authorized (ephemeral identity-keyed selection state, action bar,
Back/IME/modal registration in `Main.handleEscape`, Biblio/Tankoban membership-only removal, no new store).
Physical acceptance remains LOCAL_PENDING. Desktop composition would stay unchanged (selection only on touch hosts).

## Decision/investigation-only items (START.md) — not altered

BR-EPUB-DEFAULTS, BR-COMIC-DEFAULT, PF-11, PF-12, PF-13, WT-13, WT-15: not encountered as blockers by the
implemented units; no preferences/navigation/data/publication policy was changed for them. Their audit
records (tasks/*.md, audit/gap-register.csv) remain the decision inputs.

## WT-09 — conditional (direct-disable cancellation latch)

Investigated: the East Blue badge `MouseArea` (qml/OnePieceEastBlueAtlas.qml, `eastBlueBadgeTouch-*`) has
`enabled: root.touchInput`, and `touchInput` is `Qt.platform.os === "android"`. Its only production instance
(qml/OnePieceUniversePage.qml:90) does not rebind `touchInput`, and nothing else writes it. A direct
disable/re-enable of that area is therefore not a supported production path; the supplied host probe's late
preview stays a host-only finding. No latch added; no device bug claimed; touch handling not rewritten.
