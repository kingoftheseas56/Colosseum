# Tankoban Sources Page Design

**Date:** 2026-07-15
**Owner:** [Agent 1 (Claude), comics]
**Status:** Approved by Hemanth ("please build it, sources the way they are look really bad")

## Goal

Replace Tankoban Mode's inline per-volume source cards with a **full-screen source picker** visually equivalent to the comics `ComicTorrentSourcesPage` (which mirrors Theatre's Torrentio picker). Same black-and-gold house language, fed by the **existing** `TankobanVolumes` sources — no native/service change.

## Locked decisions (leaner v1)

1. Full-screen overlay, not inline expansion. Opened when the user taps **Choose source** on an undownloaded volume; closes back to the volume library.
2. NO manual query/search box (manga volumes are found automatically by series+volume).
3. NO weak-match confirmation modal and NO multi-file archive sub-picker in v1 — the Nyaa list is already coverage-filtered; a pick that can't isolate the volume simply reports why (the service's existing `failed` reason). Both are easy follow-ups.
4. The WeebCentral "Build from chapters" fallback is the pinned LAST row, styled quieter; disabled with its `reason` when the chapter map is incomplete (already computed by the service).
5. Confidence tag from uploader trust: tier 1 → STRONG, tier 2 → POSSIBLE, else WEAK. Evidence chips carry the specifics.

## Components

### New: `qml/MangaTankobanSourcesPage.qml`
A self-contained overlay `Item` mirroring `ComicTorrentSourcesPage.qml`:
- `property var service` (falls back to the `TankobanVolumes` context property, same seam as the library), `property Item backdrop`, `property var context`, `open`/`loading` state, `signal closed()`.
- `show(context)` — context `{ volumeId, seriesTitle, volumeNumber, volumeTitle, cover, coverage }`; resets state, `open=true`, calls `service.searchSources(volumeId)`.
- `hide()` — `open=false`, `closed()`.
- Structure: black base + `ShaderEffectSource` backdrop at 0.5; volume key-art hero (300px) washing down; `BackAction`; title block (gold eyebrow `SOURCES · TANKOBAN VOLUME`, Fraunces title = `Vol. N` (or volume title), identity line `series · Vol. N · coverage`); a `Glass` result table.
- Table: head (`N sources` + `Nyaa · WeebCentral`), empty/loading text (`Searching Nyaa releases…` / `No releases matched this volume yet.` / partial-failure line), a `ListView` of the sources from `sourcesReady(volumeId, results)` (order VERBATIM — Nyaa rows then WeebCentral last), `HouseScrollBar` + `ScrollGlide`.
- Row delegate branches on `modelData.kind`:
  - **nyaa**: source badge (uploader initial), confidence label (STRONG/POSSIBLE/WEAK from `tier`), `releaseTitle`, evidence chips (`DIGITAL` if `digital`, `SINGLE VOLUME` if `standalone` else `VOLS lo–hi`, `TRUSTED` if `tier<99`), `sizeBytes`→human + `seeders`, a 56px gold ↓ button → `service.downloadNyaa(volumeId, infoHash)` then `hide()`.
  - **weebcentral**: `label` ("Build from chapters"), quieter styling, `enabled`/`reason` (disabled shows the reason, no action), → `service.compileWeebCentral(volumeId)` then `hide()`.
- `Connections { target: service; ignoreUnknownSignals: true }` — `onSourcesReady(vid,results)` (guarded by context volumeId) fills the table; `onFailed(vid,reason)` shows the failure; a chosen source closes the page (the library row then reflects the in-flight state).

### Modified: `qml/MangaTankobanLibrary.qml`
- Remove the inline chooser `Item` + the `MangaTankobanSourceCard` usage, and the now-unneeded `openVolumes`/`expandedByVolume`/`dispatchSource`/`onSourcesReady` plumbing.
- `chooseSource(vid)` now emits `signal sourcesRequested(var context)` with `{ volumeId, number, title, cover }` instead of toggling an inline panel. Volume rows, progress, and `primaryAction` (ready→`openVolumeRequested`, in-flight→`cancel`, else→`sourcesRequested`) stay.

### Modified: `qml/MangaSeries.qml`
- Instantiate `MangaTankobanSourcesPage` (as `ComicSeriesPage` hosts `ComicTorrentSourcesPage`), pass `backdrop`. On `library.onSourcesRequested(context)` → merge in `seriesId`/`seriesTitle` and call `page.show(context)`.

### Removed: `qml/MangaTankobanSourceCard.qml`
The inline card component is superseded by the page's own row delegate.

## Verification
- `tests/test_manga_tankoban_mode.ps1` — contract greps updated for the page (`SOURCES`, `service.searchSources`/`TankobanVolumes.searchSources`, row `uploader`+`seeders`, `Build from chapters`, `downloadNyaa`, `compileWeebCentral`) and the library now emitting `sourcesRequested`.
- `tests/manga_tankoban_page_harness.qml` — offscreen: the page opens for a volume, renders the Nyaa rows in service order with the WeebCentral card LAST, and a Nyaa pick dispatches `downloadNyaa(volumeId, infoHash)`; require `MANGA_TANKOBAN_PAGE_OK`.
- Final rendered look is Hemanth's eyes-on. `font.pixelSize` INT throughout.

## Out of scope
Manual query, weak-confirm modal, archive sub-picker, any native/service change, backend search-by-query.
