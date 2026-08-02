// CatalogueVisualMetrics — the approved catalogue poster geometry + timing tokens, frozen so no
// consumer can drift them at runtime. Two immutable profiles: `classic` (the current shared card,
// used by every surface not yet cleared for the polish) and `gallery` (the approved Theatre polish
// profile, spec 2026-08-02). This module carries NO world data and does NO I/O; it does not replace
// Theme, WidgetHeader, TopBar, or any shell typography token. Consumers select a profile; they do
// not keep local copies of these numbers.
.pragma library

// Classic — the existing shared-card values (132px poster, 8px radius, single-line title). Kept
// verbatim so Discover/Tankoban/Comics stay pixel-identical until each opts in behind its own gate.
var classic = Object.freeze({
    posterWidth: 132, posterRatio: 1.5, posterRadius: 8,
    cardGap: 18, shelfGap: 26, headerGap: 14,
    titlePixels: 12, titleLines: 1, titleMinHeight: 18, hoverLift: 4,
    hoverDuration: 160, imageRevealDuration: 160
})

// Gallery — Hemanth-approved Theatre polish: wider crisp posters, genuine 12px crop, a reserved
// two-line title measure, and quiet 7px/260ms hover. Every value here is a contract number checked
// by tests/poster_source_policy_harness.qml.
var gallery = Object.freeze({
    posterWidth: 148, posterRatio: 1.5, posterRadius: 12,
    cardGap: 20, shelfGap: 46, headerGap: 18,
    titlePixels: 13, titleLines: 2, titleMinHeight: 35, hoverLift: 7,
    hoverDuration: 260, imageRevealDuration: 280
})

// Profile selector for consumers that carry a `visualProfile` string.
function profile(name) { return name === "gallery" ? gallery : classic; }
