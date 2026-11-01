# 02 — Comic collected-edition torrent source picker

**Status:** Local HTML/CSS/JS fitting study, ready for Hemanth's review. **Not** a validated Android/Qt implementation or torrent acquisition. Publication remains held.

## Design authority

- Approved desktop at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41: reference/desktop/qml/ComicTorrentSourcesPage.qml and ComicTorrentArchivePicker.qml. These own presentation, exact labels and state flow.
- Necessary transitive references: reference/desktop/qml/Theme.qml, BackAction.qml, Glass.qml, ComicSeriesPage.qml (full-screen overlay host) and Main.qml (font context). Original main face is Segoe UI, editorial title Fraunces.
- Alpha recovery reference/alpha/qml/ComicTorrentSourcesPage.qml at 77844eef0bd3b7fee2c1f884be4a76db6d63321d provides the original phone wrapping/stacking precedent. It does not supersede desktop.
- Governing classification: context/ANDROID-PORT-CLASSIFICATION.md item 2. Presets: context/VIEWPORTS.json. Rejected flat-card Android drift was not used as a design target.
- Project art: reference/desktop/assets/universes/dcau/portals/gotham.jpg (Gotham portal art). No exact local cover for the sample Batman: Year One collected edition exists in the supplied assets; the artwork substitution is disclosed **outside** the phone frame.

## QML-to-HTML control map

| Approved source | HTML/CSS equivalent | Preservation / fitting |
|---|---|---|
| Full-screen ComicTorrentSourcesPage, hosted by ComicSeriesPage | .source-sheet inside #viewport | Black overlay, in-place Back, no generic card stack, new sheet or new navigation. Back locally hides overlay. |
| Banner key art and top-to-black gradient | .hero-art | Hero wash, fade and dark continuation; shorter hero on phones. CSS approximates QML rendering. |
| BackAction plain variant | .back-action with drawn SVG chevron | Same vector geometry and literal Back label; 44px minimum interactive height. |
| Gold eyebrow, Fraunces title and edition identity line | .title-block, .eyebrow, h2, .edition-line | Exact eyebrow and identity hierarchy. Portrait title/ISBN/range wrap; short landscape moves same block beside Back. |
| queryBar TextInput and Search control | #searchForm, .query-input, .search-button | Exact placeholder, border/glass style, gold Search, Enter and click. Portrait stacks; landscape keeps original 44px Search height. |
| Glass panel and list | .glass-panel, .rows-scroll | Original rounded translucent bounded table and independently scrolling results. |
| tableHead | .table-head, .result-count, .provider-list | Result count and exact Pirate Bay · ExtraTorrents · Torrents-CSV text retained; provider line wraps on portrait. |
| Source row and srcBadge | .source-row, .source-tile | Whole-row selection, Fraunces first-letter tile (54px desktop, 36px phone), original row hierarchy. |
| Source confidence | .source-ident and .confidence | Exact STRONG MATCH (#f0c44a), POSSIBLE MATCH (#8ea3c0), WEAK MATCH (#c98a8a). |
| Release title | .release-title | Same information and face; wraps on phone and short landscape rather than suppressing data. |
| Coverage, issues, trusted uploader | .evidence, .evidence-chip | Exact FORMAT RANGE, ISSUES and TRUSTED · uploader with original gold/neutral border and fill treatment. |
| Size and seeders | .release-meta | Size and original 👤 count syntax. Missing seeder count omitted, not fabricated as 0 or -1. |
| Round pick button and MouseArea | .round-pick inside .source-row | Same 56px circular gold ↓, hover response, whole-row activation and keyboard Enter/Space. |
| Weak confirmation | #weakLayer .weak-dialog | Exact warning and Go back / Choose anyway controls; centered dark bordered dialog, not a bottom sheet. Narrow actions wrap only as fitting. |
| Automatic pack inspecting | .inspection | Exact Inspecting pack… and chosen release text. |
| ComicTorrentArchivePicker | .archive-count, .archive-heading-right, .archive-row | Exact N comic archives / CHOOSE ONE, extension tile, path, size and round download glyph. Long paths wrap on phone. |
| Incomplete issue-set state | .decision-content for incomplete | Exact missing-issues explanation, gold issue list, Try another source / Search manually. |
| Combined archive state | .decision-content for combined | Exact combined-archive warning, Go back / Download whole archive anyway and neutral/gold hierarchy. |
| KeyboardAction and state functions | app.js delegated actions | Strong/possible → inspecting; weak → confirmation → cancel or inspect; archive selection → inspect; combined/incomplete return/resume. All local only. |

## Fitting decisions

Desktop retains the original large Fraunces heading, 54px margins, ~300px hero, 150px source rows, 54px letter tile and 56px gold action. Portrait phones use 16px margins, smaller hero/title, wrapping identity and release names, a stacked query and flexible badge rows. Short landscape phones use the compressed original hero with title/identity placed beside Back, inline Search and the same glass table with scroll; nothing is discarded to make a particular row visible. Tablets at 800×1280 and 1280×800 stay in the desktop family instead of receiving a phone-only navigation redesign.

At font scale 1.3 and illustrative 24px top/bottom insets, lists scroll, metadata wraps and the original warning actions can wrap. No operating-system status bar or gesture chrome is rendered as Colosseum UI.

**Behaviour alternatives proposed: None.** No do-or-die new phone UI exception is proposed.

## Mock data and action boundaries

Four *illustrative* source records exercise strong/possible/weak confidence, short/long release titles, trusted and untrusted uploader cases, format coverage, issues and known/unknown seed counts. Four archive paths demonstrate CBZ, CBR, CB7 and CBT. The edition title, ISBN, archive names/sizes and provider data are fixture examples, **not live responses**, with this distinction labelled outside the mock frame. There are no fake blank posters or new in-product control labels.

The original Search entry and button act on local fixture rows only: matched text filters them; an unmatched phrase displays the source's existing no-results copy. Selecting a source or archive changes the shown state without network calls or downloads. Back closes the overlay; the underlying ComicSeriesPage is deliberately not invented. The external controls can reopen the source mock and choose original states. Enter, Space and Escape model existing keyboard affordances.

## Render and verification

Regenerate captures on Laptop 1 from this owned directory with:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\capture.ps1

The wrapper calls verify-mocks.cjs, using already-installed BDC Puppeteer and a **dedicated isolated headless Chrome** profile under captures/. Its browser uses exact logical width/height, deviceScaleFactor 1, waits for local fonts, and saves PNGs of the Colosseum frame without external review controls. It does not open or control shared Windows tabs or desktop input. No dependency installation or external HTTP(S) request is required.

The final captures/verification.json records **21/21 geometry/state checks passed; 10/10 local interaction checks passed; 0 HTTP(S) requests; local fonts available in all 21 cases**. Checks include no header/query/panel overlap, glass within viewport, usable panel height, button hit targets ≥44px, row widths, keyboard focusability, 56px round arrows, and dialog/action bounds. Initial landscape Search height of 40px was corrected to the original 44px before final captures.

Key captures:
- Sources: captures/comic-sources-portrait.png (412×900), captures/comic-sources-landscape.png (900×412), captures/comic-sources-desktop.png (1280×700).
- Confirmation: captures/comic-source-confirmation-portrait.png (412×900), captures/comic-source-confirmation-landscape.png (900×412), captures/comic-source-confirmation-desktop.png (1280×700).
- Stress and other original states: 354×792, 792×354, tablet, 1.3× font/insets, ambiguous archive, incomplete and combined outcomes. The complete screenshot names, states and exact viewports are in captures/verification.json.

Both HTML pages and index.html work under a local HTTP server rooted at D:\Colosseum-Android-Phone-Mocks-20261101, using relative project art/font asset paths. Bundled local Segoe font copies are referenced **only for local review** and not redistributed.

## Unresolved, and what is not verified

- The OnePlus is disconnected. Exact logical viewport, device-pixel ratio, real safe insets, IME/keyboard overlap and in-hand touch behaviour remain unmeasured.
- The Gotham portal artwork is not the exact collected-edition cover. The correct edition image was not present locally. The CSS glass blur is a visual approximation of QML's live ShaderEffectSource/MultiEffect, not pixel-equivalence proof.
- There was **no QML runtime, APK, Android test, provider search, archive validation, torrent transport, ledger or backend action** in this task. UI mock browser checks do not establish product correctness.
- Only outputs/02-comic-sources was written. No references, application source, sibling outputs, commits, installations, releases, uploads or beta state were touched.

