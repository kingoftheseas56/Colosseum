# Colosseum Fresh Book Reader — Design

**Date:** 2026-07-16
**Author:** Agent 2 (Claude, Fable), Biblio lane
**Status:** Ratified by Hemanth section-by-section (architecture 2026-07-16, sections 1–4 same day)
**Mock:** `~/Desktop/Brotherhood/agents/colosseum-book-reader-chrome-mock.html` (eyes-on approved; icon-only HUD amendment applied)

## What this is, in one sentence

Replace the imported TB2 foliate web-app reader with a book reader built fresh for
Colosseum — native QML chrome over a minimal web "paper" — retaining every current
feature except TTS, polished to the standard of the video player and manga reader.

## Why

The current reader is TB2's entire web application (~14,500 lines of JS including all
of its chrome as HTML) embedded in a `WebEngineView`. Every polish gap and input bug
we have fought — swallowed keys, hover reveal zones, click-swallower panels, settle
timers — comes from chrome living inside a webpage. The video player and manga reader
feel right because their chrome is native QML. The renderer was never the problem;
the web chrome was.

## Ratified architecture

**Native chrome, web paper.** (Option A of three; fully-web and fully-native were
rejected — fully-web repeats the disease, fully-native cannot render real EPUB
CSS/layout faithfully; every serious reader renders books through an HTML engine.)

- **The paper** (one `WebEngineView`): renders book pages and *nothing else*. Runs
  Anx Reader's foliate-js fork plus one thin glue file we write. No UI, no storage,
  no network, no keyboard handling. Stateless every launch.
- **The chrome** (QML): everything visible, everything pressed. All keyboard handling.
  Holds only volatile UI state (open panel, active tab).
- **The spine** (C++): owns all persistent state and all networking, per the
  "QML paints, C++ decides" house rule.
- **One paper for all formats (v1):** EPUB/MOBI/AZW3/FB2/TXT via foliate; PDF via
  pdf.js on the same surface. The chrome talks to a paper *interface*, so a native
  PDF paper (QtPdf) can slide in behind the same chrome later if PDF feel ever lags.
  Our current reader's proven pdf.js glue (`engine_pdf.js`) is the fallback if the
  donor's PDF path disappoints.
- **Grown in-repo as a standalone harness exe** (see Delivery), swapped into Biblio
  when it beats the old reader eyes-on. Not a separate repo: the toolchain
  (QtWebEngine/QWebChannel/MSVC) and the native stores already live here.

### Donor: Anx Reader's foliate-js fork (verified 2026-07-16)

`anxcye/anx-reader` (`assets/foliate-js`, MIT) — a production Flutter reader
(8.4k stars, ships on Windows) whose architecture is exactly ours: native chrome
commanding a dumb webview paper. Their fork = upstream foliate-js + Readest's
MIT-era fixes + selection/highlight handling rewritten to be driven from native
chrome — precisely the surgery we would otherwise do ourselves.

Field it beat:
- **Upstream `johnfactotum/foliate-js`** (MIT): the source of truth but explicitly
  unstable ("expect it to break… at any time", no releases). Kept as the reference
  we cherry-pick fixes from deliberately.
- **Readest** (22.4k stars): UI lives inside the web layer (the disease) and the app
  code is **AGPL — no code may be copied from it**. Design reference, eyes only.
- **epub.js**: the generation foliate-js superseded.

We take from Anx: the foliate fork (paper layer) and the bridge *vocabulary* (message
shapes between native and paper). We take nothing Dart/Flutter, no AI features, no
cloud sync, no TTS.

## Section 1 — Anatomy (chrome layout)

The reading state is **naked**: paper edge to edge, zero chrome. Mouse movement fades
chrome in over the page (glass over paper, player-HUD behavior); idle fades it out.
Keys never wake chrome.

What wakes:

1. **Top bar** — icon-only buttons (SVG, no text buttons): back-arrow left;
   search / contents / appearance / bookmark-this-page right. Text in the bar is
   metadata only: book title · author centered (inline-quiet, no pills), chapter
   label beside the icons.
2. **Bottom bar** — thin gold progress rail across the whole book with chapter
   ticks; draggable scrub; quiet inline text: "Page N of M in chapter" left,
   "X% of book" right. After any jump, a **return ghost chip** ("Return to page N")
   floats above the rail.
3. **Left glass panel** — tabs: **Contents / Bookmarks / Highlights / Audio**.
   Contents: read chapters dimmed, current gold. Highlights: color as an edge rule
   on the quote, note indented beneath.
4. **Right glass panel — Appearance**: theme swatches (Paper / Sepia / Slate /
   Night), typeface cards, size stepper, line-spacing + margin sliders, justify
   toggle, reading-ruler controls. Everything live-applies; no Apply button.
5. **Search** — thin floating sheet under the top bar: input, result count,
   jump-rows with the hit marked.

Page turning: click left/right page edges (subtle chevrons while chrome is awake),
arrow keys, PgUp/PgDn, Space — all handled in QML.

Visual language: player-HUD constants — Inter UI, Fraunces display, gold accent,
white-alpha grays, glass bars on near-black. The mock is the reference.

## Section 2 — Responsibility split

**The paper does:** unpack + lay out + paginate; execute commands (turn page, go to
position/chapter/search-hit, apply appearance, run search, paint/remove highlight);
report events (ready + TOC + metadata, position changed, selection made with rects,
search results, footnote/link tapped, error). Nothing else.

**C++ owns:**
- **All persistent state — in the stores that already exist.** The current reader
  already persists natively via `BookBridge`: `progress.json`, `bookmarks.json`,
  `annotations.json`, `settings.json` in AppData, plus the audio attachment store
  (`fb5a741`). The new reader reads and writes the **same stores**. Swap-day
  migration: **none**. Positions, bookmarks, highlights, settings carry over.
- **Networking:** dictionary lookups (Wiktionary) through the native net service.
  The paper never fetches.
- **Book bytes** served to the paper as base64 (QWebChannel corrupts binary
  QByteArray — lesson baked in from day one).
- **Audiobook playback + attachment** (existing `AudioPairingStore` plumbing).

**QML owns:** all chrome, all keyboard, volatile UI state only.

**The seam:** a small versioned message vocabulary (~a dozen commands down, ~eight
events up), shaped after Anx's proven bridge, carried over QWebChannel.

Reference flows:
- *Page turn:* QML key → `nextPage` command → paper reflows → `relocated` event →
  C++ saves progress; QML updates rail; audio follow (if on) nudges the audiobook.
  One event drives all three.
- *Highlight:* selection on paper → `selection` event → **native QML menu at the
  cursor** (colors / note / copy / define) → C++ stores → paint command down.
  "Define" rides the same path: C++ fetches, QML card renders.

## Section 3 — Feature parity (the contract)

| Feature | Today | Fresh reader |
|---|---|---|
| Formats | EPUB, MOBI, FB2, TXT, PDF | Same + **AZW3** (free from the fork) |
| Resume | native `progress.json` | Same store — every book resumes in place |
| Page turning | web-handled keys/clicks | QML keys, edge clicks, scrub rail |
| Chapter nav + return | web nav module | Contents tab, rail ticks, return ghost |
| Table of contents | HTML sidebar tab | Contents tab (TOC reported at open) |
| Search | HTML overlay | Floating sheet; paper finds, QML lists |
| Bookmarks | HTML tab, native store | Bookmark icon + tab; same store |
| Highlights + notes | web popup, native store | Native selection menu; same store |
| Dictionary | Wiktionary via web fetch | Same lookup via C++ net; QML card |
| Footnotes | in-page popups | Tap → QML footnote card |
| Appearance | literary themes, font, size, spacing, margins, justify | Right panel, live-apply; same store |
| Reading ruler | drawn inside the web page | **Pure QML overlay** (leaves web entirely) |
| Read-along audio | foundation landed (`fb5a741`) | Audio tab: attached audiobook, Follow toggle, transport; reuses the QML docked player |

**Deliberately gone:**
- **TTS** (~4,600 lines: `tts_core`, `tts_engine_edge`, `tts_hud`) — dropped whole,
  unreliable, per Hemanth's call.
- **Pairing as a user concept** (amendment 2026-07-16): audiobooks are downloaded
  from the book's own page, so **the downloader writes the attachment at completion,
  keyed by book id**. No picker, no matching step, no unpair UI. Chapter alignment
  is computed, never user-managed. Audio tab shows the attached audiobook if
  downloaded; otherwise points to the book page (download-fed doctrine).
- **The entire HTML chrome** — sidebar, panels, popup menus, keyboard code.

## Section 4 — Delivery

**Harness (the test bench):** new small exe target `reader_harness` in the Colosseum
CMake (same pattern as the torrent harnesses), launched from a bat file. Boots
straight into the new reader in a window with a thin shelf strip of books already in
the downloads folder. **Writes to a sandboxed copy of the stores by default** —
development can never scramble real reading state; flipped to real stores only for
final acceptance. All new code in its own folders; the shipping reader is untouched
until swap day.

**Build order (eyes-on build after every step):**
1. Paper stands — fork vendored, glue written, harness opens an EPUB, pages turn.
2. The seam — bridge v2 wired to existing stores; resume + position saving work.
3. Chrome skeleton — top bar, bottom rail, reveal. (Must *feel* like the player.)
4. Panels — Contents/Bookmarks/Highlights; Appearance with live-apply.
5. The pen — selection menu, highlights, notes, dictionary, footnotes.
6. Search, ruler (QML overlay), Audio tab (read-along increments 4–5), PDF/TXT.
7. Polish pass against this parity table, old reader side by side.

**Testing doctrine:** paper glue iterates in a plain browser (Anx's debug-page
pattern); pure logic gets headless qml.exe harness tests with exit-code verdicts
(grep contracts are shape, not behavior); every increment ends eyes-on — pixels are
Hemanth's. Full Codex review of the reader against this spec before swap.

**Swap day (one small commit):** Biblio's open-book pointer moves to the new reader;
the old reader goes to an archive branch first (house pattern), then
`resources/book_reader/` (~14.5k lines + 12MB vendor) and `BookReader.qml` are
deleted. Stores stay. Acceptance: this parity table walked in the full app, eyes-on.

## Out of scope

- TTS in any form.
- New features beyond parity (stats, sync, AI anything) — polish, not scope growth.
- Native PDF paper (QtPdf) — explicitly deferred; the paper interface leaves the
  door open.
- Library/Biblio surfaces outside the reader (the book page, shelves, downloads).

## Open items for the implementation plan

- Exact bridge message list (names + payloads), drafted from Anx's channel vocabulary.
- Which glue files from Anx's `src/` come over vs. get rewritten (their fork bundles
  via webpack; decide bundle-as-is vs. unbundled vendoring).
- Verify the fork's PDF path early (step 1); fall back to our `engine_pdf.js` glue
  if it disappoints.
- Sandbox-store mechanism for the harness (env var vs. build flag).
