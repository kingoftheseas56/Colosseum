# Colosseum: current UI browser mock

A separate, offline browser recreation built from the existing website HTML and reconciled against inspected Colosseum QML. The live website and production application are unchanged.

## Open it

1. Download and extract the complete folder. Keep `assets`, `demos`, `shared` and `manifests` beside the HTML files.
2. Open **review.html** to browse all registered screens and inspection states, or **index.html** to use the app-style navigation.
3. Use the mouse or Tab/Enter; Escape closes supported overlays or navigates back. Native browser Back/Forward is supported. Longer pages scroll normally.

No build, server, sign-in or Internet connection is needed for the mock. A normal static HTTP server is optional. Do not enter real passwords, recovery keys, tokens or personal information. Account and provider controls use demonstration data.

## Source basis

- App: [Colosseum 2916fe85812643c2e0ae45b0aec132cb6eb4e507](https://github.com/kingoftheseas56/Colosseum/tree/2916fe85812643c2e0ae45b0aec132cb6eb4e507). Master was rechecked on 2026-10-08 during final review.
- Existing HTML: [Brotherhood db990f8f438a53cf7da1eec9ff7f4d57955c7cc4](https://github.com/kingoftheseas56/Brotherhood/tree/db990f8f438a53cf7da1eec9ff7f4d57955c7cc4/agents/colosseum-site).
- Separate Vinyl prototype: [bf3becf16c4288e9fce591ac45e9c179d846ae21](https://github.com/kingoftheseas56/Colosseum/tree/bf3becf16c4288e9fce591ac45e9c179d846ae21). It is available through the review page and is not presented as a world integrated into current master.
- Delivery branch: `dots/current-colosseum-ui-mock` in Brotherhood.

Per-route source files, inspection states and limitations are in `manifests/*.json` and the review page. The source's default title pages and opt-in rebuilt variants are represented separately. Real bundled extension marks, Feria app marks and supplied artwork are retained locally. Original notices and asset provenance remain under `assets`.

## What the controls do

Navigation, overlays, filters, chapter/volume selection, staged playback controls, queue/session management and fixture state changes work in the browser. This is a UI mock, not a browser port of the C++ backends. Authentication, synchronization, downloads, updates, DRM, playback and filesystem actions are simulations and do not contact providers or mutate real files/accounts.

Manga reader pages are original, explicitly labelled sample artwork. Book-reader text uses public-domain excerpts rather than the copyrighted book shown on the catalogue cover. Catalogues and metadata are finite fixtures. Some native 3D universes are represented by labelled 2D maps; exact Qt shader rendering, native spatial navigation and media algorithms are not reproduced.

## Verification

Screenshots are captured and visually inspected, alongside interaction checks. Linux browser results qualify this HTML mock only: Debian 13.6 x64, Chromium Headless Shell 151.0.7922.34 and patched Firefox 153.0 with Playwright 1.62.1. Windows behavior and compiled QML/native engines were not tested for this delivery.

See `qa/README.md` for portable route/screenshot verification and `DELIVERY.md` for final results, exact publication identity and evidence selection. Browser-specific native form controls may render differently on Windows.
