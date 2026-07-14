# Tankoban Series Black Background Design

**Date:** 2026-07-14

**Owner:** [Agent 1 (Codex), comics]

**Status:** Approved design; implementation pending

## Objective

Make the comic-series and manga-series cover views use the same pitch-black background treatment as `TheatreSeries.qml` instead of their current transparent blue wallpaper treatment.

## Visual contract

Both `ComicSeries.qml` and `MangaSeries.qml` will copy Theatre's three-layer background exactly:

1. A full-page `#000000` base.
2. The shared wallpaper mirror at `opacity: 0.5`.
3. A black scrim gradient with alpha values `0.50`, `0.78`, and `0.95` at positions `0.0`, `0.42`, and `1.0`.

This preserves a faint relationship to the global wallpaper while making the series surface read as pitch black, matching Theatre.

## Scope

Only the background layers in `qml/ComicSeries.qml` and `qml/MangaSeries.qml` change. Covers, banners, typography, metadata, shelves, chapter/release rows, navigation, downloads, readers, loading states, and QML data contracts remain unchanged.

## Verification

- A focused static/QML contract test must assert that all three series views share the same base color, wallpaper opacity, and gradient stops.
- Existing comics and manga series harnesses must remain green.
- The native MSVC build must print `BUILD_OK` and exit 0.
- Hemanth performs the final eyes-on comparison.
