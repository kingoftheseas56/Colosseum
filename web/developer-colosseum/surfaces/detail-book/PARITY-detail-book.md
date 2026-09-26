# W2-9 Book detail parity ledger

Working GitHub mirror while the local Preflight checkout is unavailable. Before hand-back, copy this ledger beside `BRIEF-W2-9.md` as required by the brief.

Authority: `qml/BiblioBook.qml` plus its shared controls `LibraryButton.qml`, `BackAction.qml`, `HouseScrollBar.qml`, and `KeyboardAction.qml`.

| Source | Element / behavior | Required parity | W2-9 implementation |
|---|---|---|---|
| BiblioBook.qml:75-82 | Solid Biblio page ground | Dark top-to-bottom Biblio wash, no underlying world bleed | ✅ `surface.css .db-ground` |
| BiblioBook.qml:95-115 | LibGen source load | Load on book change, loading state, local-copy refresh after results | ✅ native `detail.book` draft |
| BiblioBook.qml:116-132 | Audiobook source load + Stream prewarm | Rehydrate local state, ABB search, prewarm top match | ✅ native `detail.book` draft |
| BiblioBook.qml:136-160 | Book torrent search | Superseding live search, ranked native results, loading completion | ✅ native `detail.book` draft |
| BiblioBook.qml:163-179 | Edition labels | Exact size/source/year/language metadata and format badge rules | ✅ native data + `surface.js` |
| BiblioBook.qml:181-190 | Download-fed Read | No book streaming, local file is the reader input | ✅ native Read action |
| BiblioBook.qml:192-207 | One-copy-per-book replacement | New acquisition deletes prior local ebook/torrent copy; adoption does not | ✅ native Read/acquire actions |
| BiblioBook.qml:208-216 | Auto-collect on acquisition | Save the book to Biblio Collection when acquiring | ✅ native action through existing Collection owner |
| BiblioBook.qml:217-229 | Reading progress | Resolve book progress for CTA/status | ✅ native feed snapshot |
| BiblioBook.qml:231-239 | Primary label | Exact `Read`, `Continue`, `Read when ready` | ✅ native feed + hero renderer |
| BiblioBook.qml:240-261 | Primary status text | Exact ready/finding/choice/queued/downloading/failure copy | ✅ native feed + hero renderer |
| BiblioBook.qml:263-302 | Foreground Read state | Generation-scoped lookup/choice/acquisition intent | ✅ native draft |
| BiblioBook.qml:304-349 | Candidate discovery | Adopt in-flight tracked copies before starting a new one | ✅ native draft |
| BiblioBook.qml:351-368 | Read choice opening/return | Modal opens only for multiple tracked candidates and returns focus after close | ✅ web focus scope; focus-return proof pending live walk |
| BiblioBook.qml:369-403 | Target candidate | Done opens, in-flight adopts, otherwise starts exact chosen native acquisition | ✅ native draft |
| BiblioBook.qml:404-414 | Choose/cancel source | Choose exact candidate; Cancel abandons foreground intent only | ✅ `detail.book.chooseRead` / `cancelRead` |
| BiblioBook.qml:415-429 | Read completion | Exact chosen file must be `done`, then Reader2 opens local path | ✅ delegated native Reader2 handoff |
| BiblioBook.qml:430-486 | Read failures/fallback order | Preserve error wording and LibGen-before-ranked-torrent policy | ✅ native draft |
| BiblioBook.qml:488-498 | Primary Read click | Local copy opens immediately; otherwise foreground lookup starts | ✅ `detail.book.read` |
| BiblioBook.qml:499-517 | Explicit edition click | Acquire-only, never auto-open Reader2 | ✅ `detail.book.downloadEdition` |
| BiblioBook.qml:518-527 | Explicit torrent click | Acquire-only, never auto-open Reader2 | ✅ `detail.book.downloadTorrent` |
| BiblioBook.qml:548-561 | Audiobook click | Resolve ABB hash, native download, pair to local ebook when possible, auto-collect | ✅ `detail.book.downloadAudiobook` |
| BiblioBook.qml:564-601 | Durable local recovery | Recover local ebook by md5, then normalized title+author, then torrent copy | ✅ native draft |
| BiblioBook.qml:603-655 | Live download signals | Resolving/progress/finished/failed/removed update the page | ✅ native owner signal wiring draft; final FeedRegistry ownerSignals alignment pending |
| BiblioBook.qml:657-714 | Book-local top bar | Back + `Biblio` title + Minimize/Fullscreen/Close at y=22, height=64 | ⛔ shared web shell owns chrome; documented in `REQUEST-detail-book.md` |
| BiblioBook.qml:717-726 | Vertical page scroll | Content begins at y=108, stop-at-bounds, proper 12px scrollbar | ✅ board begins at 96px + 12px surface padding = 108px; surface scrollbar CSS |
| BiblioBook.qml:728-732 | Body margins | Left/right Theme margin and body-height behavior | ✅ shared `--margin` + grid |
| BiblioBook.qml:735-783 | Physical cover | 268×402 cover, page edge, spine, dark load tint, cover crop, shadow | ✅ `surface.css .db-cover-*`; visual proof pending |
| BiblioBook.qml:785-810 | Primary Read control | 268×50, radius 13, gold, disabled opacity .5, keyboard activation | ✅ `.db-read` |
| BiblioBook.qml:812-820 | Read status | 12px centered, error becomes #e6a3a3 | ✅ `.db-read-status` |
| BiblioBook.qml:821-827 + LibraryButton.qml | Library control | 268×50 on this page, +/✓ glyph, exact `Library` / `In Library`, live toggle | ✅ `.db-library` + native action |
| BiblioBook.qml:828-830 | No standalone Listen button | Audiobook playback stays inside Reader2 | ✅ no Listen CTA added |
| BiblioBook.qml:834-845 | Text column + eyebrow | 64px column gap, 18px top padding, 12px uppercase genre/author/year line | ✅ CSS grid + `.db-eyebrow` |
| BiblioBook.qml:848-852 | Book title | Display face, 54px, 1.02 line-height, wrapping | ✅ `.db-copy h1` |
| BiblioBook.qml:854-860 | Tagline | Optional quoted italic 28px hero line | ✅ `.db-tagline` |
| BiblioBook.qml:862-874 | Hairline + gold tick | Edge-to-transparent rule with 34×3 gold leading tick | ✅ `.db-rule` |
| BiblioBook.qml:876-881 + 85-93 | Drop-cap synopsis | Max 640px, 17px display face, 1.7 line-height, 62px raised first letter | ✅ `.db-synopsis` |
| BiblioBook.qml:884-893 | Torrent heading | Exact `TORRENTS · SEARCHING…`, count, or `NONE` | ✅ `renderTorrents` |
| BiblioBook.qml:894-926 | Torrent panel states | 640px max, radius 14; exact `Searching torrents…` / `No torrents found` | ✅ `renderTorrents` |
| BiblioBook.qml:927-994 | Torrent rows | 64px row, title, ▲ seeders, size, PACK, no format pill, top recommendation tint | ✅ `sourceRow("torrent")` |
| BiblioBook.qml:942-982 | Torrent live states | idle ↓, resolving …, downloading N%, done ✓, failed retry | ✅ row indicator |
| BiblioBook.qml:995-1004 | Torrent cap | Top 5 by default, exact `See N more` / `See less`, keyboard reachable | ✅ local web expansion |
| BiblioBook.qml:1005-1028 | Edition heading/panel states | Exact `EDITIONS · SEARCHING…`, count/NONE, `Searching LibGen…`, `No editions found` | ✅ `renderEditions` |
| BiblioBook.qml:1029-1126 | Edition rows | 52px row, best tint, format pill, exact metadata, ↓/↗/…/%/✓/retry indicators | ✅ `sourceRow("edition")`; external-page action remains data-dependent |
| BiblioBook.qml:1128-1154 | Audiobook heading/panel | Exact `AUDIOBOOK · SEARCHING…`, count/NONE, 640px panel | ✅ `renderAudiobooks` |
| BiblioBook.qml:1155-1187 | Background audiobook rehydrate | Slug-less running download shows exact resolving/progress banner | ✅ `renderAudiobooks` |
| BiblioBook.qml:1188-1243 | Audiobook rows | Exact loading/empty copy, 52px rows, 58×24 badge, metadata, active-row state only | ✅ `sourceRow("audio")` |
| BiblioBook.qml:1247-1263 | Read-choice shade/card | Full-page .72 black shade, centered glass card, 620px max, radius 18 | ✅ `renderChoice` |
| BiblioBook.qml:1264-1279 | Read-choice keyboard scope | Escape cancel, Tab contained, Up/Down, Home/End, Enter/Space | ✅ arrows/Enter/Space via shared focus engine; Escape via `__close`; Tab/Home/End handled locally |
| BiblioBook.qml:1281-1294 | Choice heading/copy | Exact `Choose an edition` and Reader2 explanatory sentence | ✅ `renderChoice` |
| BiblioBook.qml:1296-1325 | Choice rows | 58px, radius 10, focus/hover fills, source label/meta, gold `Read` | ✅ `.db-choice-row` |
| BiblioBook.qml:1327-1334 | Cancel row | 42px, radius 10, edge border, exact `Cancel` | ✅ `.db-choice-cancel` |
| BiblioBook.qml:1339-1340 | Keyboard scroll | Down/Up page movement through shared focus/board scroll behavior | ✅ route uses shared focus engine; live walk pending |
| HouseScrollBar.qml:30-49 | Proper scrollbar material | 12px rail, 8px thumb, white idle/hover, gold pressed, 140ms color motion | ✅ geometry/colors built; 140ms transition verification pending |
| BackAction.qml | Back hover/focus language | Quieter Biblio white hover and canonical chevron | ⛔ shared shell Home/Back control owns this chrome; request documented |
| KeyboardAction.qml | Focus aura | Keyboard activation and visible focus | ✅ hero/action buttons use `data-focus`; source rows use QML-equivalent selected fill |
| Brief W2-9 | 1920×1080 screenshot | Side-by-side with QML original | ⏳ requires runnable local app/browser |
| Brief W2-9 | 1280×720 screenshot | Side-by-side with QML original | ⏳ requires runnable local app/browser |
| Brief W2-9 | walk.mjs | Every focusable reachable; Escape leaves route | ⏳ requires runnable local checkout |
| Brief W2-9 | Core selftest | `dev/selftest.html` PASS | ⏳ requires runnable local checkout |
| Brief W2-9 | Real actions | Read, Collection, edition, torrent, audiobook and failure path exercised | ⏳ requires native build/runtime |
