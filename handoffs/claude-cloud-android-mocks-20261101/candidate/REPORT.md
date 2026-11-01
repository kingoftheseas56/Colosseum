# Combined candidate · final report

**Branch** `claude/intelligent-thompson-usa0dk` on `kingoftheseas56/Colosseum`, based on input `codex/claude-cloud-android-mocks-20261101` (`dcdcfea`). The wider source stays at the fixed pin `a40f66be`. Only `handoffs/claude-cloud-android-mocks-20261101/candidate/` was added. `baseline/`, the task documents, app source and QML are untouched.

**Run it.** From the handoff folder run `python3 -m http.server 8000`, then open `/candidate/index.html`. This is the one combined entry: all 53 pages, alternates, comparisons and documents. For Batch 5's wallpaper art, first run `python3 candidate/verify/fetch-kde-wallpapers.py`.

**Merge endpoint (Rule 28).** This branch carries candidate mocks and evidence for Hemanth/Codex review. It is not for main: no adoption into `master` is authorised. It should be merged into the input handoff branch, or retired, once Hemanth has made the open choices below.

## What was done, per batch

| Batch | Work | Fresh result (before → after) |
|---|---|---|
| 1 · Sources and manga (9) | Rebuilt series/chapters made the main mocks (older reading room kept as `?route=reading-room`). Added the real TitlePageBar: Back pill “Tankoban”, Search, and one system menu with Quit confirmation. On Android phones and tablets the menu invoker and its actions are not rendered. Wordmark, device progress and synopsis More kept. Both cover-flow pages rebuilt on the chosen header as allocation A (fit) and B (scroll), with the flow unchanged. Five source pickers carried unchanged. | 19/19 focused viewport cases; flow geometry for 18 cases; Codex's source-picker interactions 6/6 |
| 2 · Detail (8) | Integrated Sol's corrected `17-theatre-details/app.js` (hash-checked 64fd16d3…). Both Theatre renderers kept. B2-018 comparison kept. | Theatre window gating 2/28 → 28/28 (adds 412/900 and tablet portrait) |
| 3 · Browsing (16) | Integrated Sol's latest Search `app.js` (5f3adf98…). That shared script also serves four other pages; it now renders window controls on the desktop host only, which fixes a tablet leak on genre-index, genre-results and continue-see-all. Restored the Apple Books icon from the pin. | Search 18/78 → 78/78 on the final revision. Tablet leak fixed (6 cases in smoke). |
| 4 · Accounts (13) | Independently qualified all 13 pages, since no final report or receiving review existed. Fixed Minimize / Fullscreen / Quit showing on Android tablet presets (account forms, Account Centre, flyout underlay). No generic replacement sheets remain. | 101/117 → 117/117; Pro's nine regression probes 9/9; zero network requests |
| 5 · Tracker (7) | Integrated Sol's corrected `tracker.js` (866b0560…). Made the omitted KDE wallpapers restorable byte-exactly (hash-verified, not committed). | 20/130 → 130/130 (adds 412/900 and 900/412) |

**All pages.** The fresh smoke covers 56 routes (53 pages plus 3 alternates) × 9 cases each: the seven brief viewports, plus 354×792 and 792×354 at 1.3 text with 24 px insets. Each family is driven through its own preset convention, and the check confirms the frame really renders at the requested size.

| Smoke metric (504 cases) | Baseline | Candidate |
|---|---|---|
| Script errors | 0 | 0 |
| Failed local requests | 18 | 0 |
| Broken images | 18 | 0 |
| Horizontal overflow | 0 | 0 |
| Android window actions reachable | 38 | 0 |
| External requests | 0 | 0 |

The candidate figures assume the KDE restore has been run. Without it, wallpaper-search shows the inherited missing-image gap. Per-page results are in `COVERAGE.csv`; before/after images are in `compare.html`.

## Commits

| Commit | Change |
|---|---|
| `d8b096e` | Seed: copy of the finished mocks; only escaping paths rewritten (ledger: `verify/seed-rewrites.json`) |
| `22be0b0` | Sol's Batch 2/3/5 corrections integrated |
| `f264902` | Batch 1 rebuilt title page and flow pair |
| `8700ba3` | Batch 4 qualification and tablet fix |
| `c31616c` | Batch 3 tablet fix and per-family smoke |
| final commit | Documents, final evidence, and verification scripts now committed |

Two corrections to the commit record:
- `8700ba3` claims zero reachable window actions across all pages. That came from a generic-parameter audit that could not reach tablet presets, and `c31616c` found and fixed three more pages. That audit has been removed.
- Before the final commit, the verification scripts sat in a git-ignored `tools/` folder. They now live in `candidate/verify/`.

Final hashes of the changed scripts:

| File | SHA-256 prefix |
|---|---|
| `04-manga-series/app.js` | 6ccee54b |
| `04-manga-series/style.css` | fec093cb |
| `17-theatre-details/app.js` | a450f6ad (Sol's file plus a path rewrite) |
| `20-search-browse/app.js` | 5f61f55d (Sol's 5f3adf98 plus the desktop-only window gate) |
| `07-biblio-home/app.js` | 40e329b7 |
| `11-account-entry/app.js` | 08385acd |
| `12-account-forms/app.js` | 75dd1482 |
| `tracker.js` | df2c91b7 (Sol's file plus a path rewrite) |

Tooling: Playwright with headless Chromium 153.0.8010.12 on Linux, an owned loopback server, and external requests blocked.

## Decisions still open for Hemanth

1. **Cover-flow allocation.**
   - Under the selected header, A (source fit) fails on every phone viewport, because the masthead alone is about 533 px tall in portrait. It fits on tablets and desktop.
   - B (page scroll) keeps the 190 px floor and 1.10 focus everywhere. The cost is 102 px of scrolling in portrait and 405–559 px in landscape.
   - In phone landscape, B cannot show the cover and its action row at once below the pinned bar.
   - The main mock uses B, because the viewed preview did; this is not an approval.
2. **Wordmark.** Kept in the title bar, moving to a second row when it would collide. TitlePageBar has no wordmark, so this keeps or drops a declared element.
3. **Language box in Tankoban Mode.** The preview shows it disabled; the rebuilt source hides it.
4. **Theatre legacy vs rebuilt.** Still a separate choice. One `Main.qml` flag currently switches manga and Theatre together, so it needs splitting before native adoption.
5. **B2-018 Universe tabs below Back.** Recommended fit vs original placement.
6. **B4-009.** Should account navigation scroll with the content while the keyboard is visible at 792×354?
7. **Batch 3 poster bleed.** Remains unselected.

## Not verified

- **Native behaviour.** No Qt/QML, Android device, Windows or native-runtime behaviour was exercised. Native focus, IME and insets, safe areas, real accessibility trees, metrics and performance are all unverified.
- **Fonts.** Segoe UI binaries are intentionally absent, so Linux fallback fonts render instead and metrics are unverified. Fraunces loads from the staged project assets.
- **Art and fixtures.** Artwork remains the declared stand-ins (no exact manga jackets). Providers, accounts, sync and downloads are local fixtures with no service calls.
- **Mock viewport classifier.** The mocks decide “Android vs desktop” from viewport size, as Sol's did. Native must use `androidHost` (see `QML-PORTING.md` N2).
- **Inherited gaps kept.** These are not fabricated:
  - Batch 4 `REVIEW.md` and the final `REPORT.json` were never produced.
  - Output 05/08/21 reports are missing.
  - The original worker outputs reference a missing Theatre `collections.svg` (those outputs are not part of the candidate).
- **Older receipts.** Earlier Sol and Pro receipts are history. The results above are fresh runs on this tree.

States: **authored and browser-tested (Linux Chromium)**, **committed and pushed**. Nothing was deployed, published, released, adopted into main, or native-qualified.
