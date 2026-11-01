# 06-biblio-book — fidelity and fitting record

**Artifact:** [biblio-book.html](biblio-book.html), indexed from [index.html](index.html).
**Scope:** local static HTML/CSS/JS fitting study. This is not QML code, an integrated Android build, a device test, or approval to publish.
**Authority:** approved desktop snapshot `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, with the earlier alpha `77844eef0bd3b7fee2c1f884be4a76db6d63321d` used for prior phone-fitting dimensions. The rejected Android-drift snapshot is not a design source.

## Source-to-HTML component map

| Approved source | HTML counterpart | Fidelity notes |
| --- | --- | --- |
| `Main.qml` `bookLayer` Loader, z 53 | `#app-screen > .safe-zone` | Entire Biblio detail as its own over-world layer. No invented bottom navigation, phone OS chrome, world tabs, or extra route controls. |
| `Theme.qml`, `BiblioBook.qml` page gradient | `.app-screen` | Navy `#0c0f18 → #06070b`; ink `#f7f7f5`, dim `#c9c8d0`, dimmer `#9a99a5`, sparing gold `#f0c44a`. |
| `Glass.qml`, `BiblioBook.qml` `bar` | `.book-bar.glass` | Radius 16, translucent white/dark scrim, edge, local CSS backdrop blur. Desktop 54px margin, 22px top, 64px height. CSS blur is an approximation, not a Qt texture equivalence claim. |
| `BackAction.qml` plain variant | `#book-back` | 20px hand-drawn path using identical chevron coordinates, **Back** at 14px, inkDim idle/ink hover; 44px visible hit height in mock. |
| `BiblioBook.qml` bar title and system controls | `.world-title` and `.window-actions` | Fraunces **Biblio**; min, fullscreen, power rendered only in desktop reference. Hidden on Android phone **and** tablet per classification. |
| `BiblioBook.qml` `page: Flickable` | `#book-scroll` | Independent vertical scroll under sticky source-style top bar, with no horizontal content overflow and viewport-relative sizing. |
| `BiblioBook.qml` cover physical object | `.book-jacket` | Exact desktop 268×402 shape; left spine 11px, right paper edge 7px, 26px drop-shadow, crop image; 208×312 in normal phone portrait; short-landscape height scales down. |
| `BiblioBook.qml` genre `Text`; `BiblioApi.js:genreLine` | `.eyebrow` | Uppercase genre · **author** · year, 12px desktop with 1.8 letter-spacing; 10px phone. No separate invented author UI. |
| `BiblioBook.qml` title `Text` | `.book-title` | Fraunces Regular 54px desktop / 34px phone portrait / 30px short landscape; 1.02 line-height and wrapping for unusually long book names. |
| `BiblioBook.qml` tagline | `.tagline` | Fraunces Italic, curly quotes, 28px desktop / 22px portrait / 20px landscape; 1.3 line-height. |
| `BiblioBook.qml` gold tick + hairline | `.gold-rule` | 34×3 gold tick on fading 1px white hairline, original hierarchy after tagline. |
| `BiblioBook.qml` `dropCapHtml` synopsis | `.synopsis::first-letter` | Fraunces body 17px desktop / 15px phone at 1.7 line-height, raised illuminated first letter 62px desktop / 54px phone. CSS pseudo-element is a visual approximation of QML RichText inline span. |
| `BiblioBook.qml` `primaryCta` + `primaryReadStatus` | `#read-button` + `#read-status` | Gold Read button with dark text, radius 13, 50px desktop/48px phone; original status string. Choice demo uses the approved read-choice surface. |
| `LibraryButton.qml` | `#library-button` | Ghost translucent white, edge, 50px/13px in detail, **+ Library** ↔ **✓ In Library** with gold check. Mock state only. |
| `collections/AddToCollectionsButton.qml`, `CollectionIcon.qml` | `#collections-button` | Existing **Add to Collections** wording, exact Collections SVG path, white ghost/edge. Desktop visible 42px/radius 11; fitted phone visible 44px to meet phone touch target. |
| `BiblioBook.qml` torrent `Glass` / 64px row / See more | `#torrent-list`, `.torrent-row`, `#torrent-more` | 5 rows default, expands to all 7; single-line elided title, seeders/size/PACK second line, arrow download indicator. Top match subtle gold wash. **No bogus torrent format chip.** |
| `BiblioBook.qml` Editions `Glass` / rows | `#edition-list`, `.edition-row` | 52px minimum, actual outlined format pill, source/size/year/language text, subtle gold for preferred edition. |
| `BiblioBook.qml` Audiobook `Glass` / rows | `#audiobook-list`, `.audiobook-row` | Format pill, size/language/posted, arrow status, paired-audio section. No retired **Listen** action added. |
| `BiblioBook.qml` `readChoiceShade/readChoiceCard` | `#read-shade` / `.read-choice` | 72% dark shade, glass radius 18, original **Choose an edition** title, Read rows, and Cancel. Clamped to viewport with inner scroll. |
| `collections/AddToCollectionsPopup.qml` | `#collections-shade` / `.collections-picker` | Existing picker title, description, selected title, search, membership rows, new collection, Cancel/Done. Static collection names are illustration; no named-collection persistence. |

## Fitting changes (position/size only)

1. **Desktop 1280×700:** original two-column dust-jacket layout, 268px cover column + 64px gap; text remains in a continuous independent column. Approved system glyph buttons visible.
2. **Phone portrait 412×900 and 354×792:** original cover centers at 208×312. For first-view comprehension, the blocks stack **cover → existing title/tagline/genre metadata → existing Read/Library/Collections controls → complete synopsis/source lists**. This is an ordering change only; all existing visual controls and source sections remain. The earlier alpha stacked cover+actions ahead of title; this study instead places title between cover and actions, and flags the difference for review.
3. **Phone landscape 900×412 and 792×354:** preserve the two-column jacket / text relationship because it fits without squeezing the literary typography. Use 32px gap, 24px outer margin; short-phone cover heights of 280px and 222px respectively. Actions scroll beneath the cover within their original left column. Phone landscape at exactly 900px counts as phone here because the short dimension is less than 600px, unlike the narrower historical alpha width branch.
4. **Tablet 800×1280 and 1280×800:** retain two columns, a 240×360 jacket, 40px gap/margins, 46px editorial title; no phone-only controls or desktop window buttons.
5. **Long text and font scale:** long book title wraps in Fraunces; genre/author/year metadata wraps; source metadata wraps while the torrent title preserves its source elision. Adjustable font scale 1.0 or 1.3 and top/bottom safe insets 0 or illustrative 24px are reviewer controls **outside** the app.
6. **Popup fitting:** original centred glass/dialog forms; max-width and max-height clamp within the safe viewport, and contents scroll for short landscape. No generic phone bottom sheet, new tab, separate routes, or new download form.

## Existing behaviours demonstrated locally

- Read opens the existing tracked-edition choice; Cancel, Escape and selection close it. Selection can display the original “Preparing your edition…” state, but starts no download.
- Library toggles the existing visual saved/unsaved state.
- Add to Collections opens the existing-style centred picker with local search, membership toggles and new-name entry.
- The torrent **See 2 more** row toggles its original expanded/collapsed state; source row clicks show only a visual status fixture.
- The reviewer-only scenario selector can show short/full synopsis, read choice, picker, saved state and expanded torrents. **BiblioBook.qml itself has no synopsis expand/collapse button**, so none was invented inside the app.

**Behaviour alternatives proposed:** none. No replacement for the original read-choice modal, collection picker or source controls is proposed. No NEW phone UI exception is requested.

## Data and artwork honesty

- The approved application ordinarily obtains book covers and blurbs through BiblioApi/iTunes and acquisition rows through services. This offline mock makes **no** provider/account/backend requests.
- A retail cover for the selected illustrative long-title book was not included in the frozen local assets. The displayed actual project image is `reference/desktop/assets/extensions/house/cosmere.jpg`, a Cosmere universe background **not** a retail cover. It is visibly rendered as the cover-shape study and explicitly disclosed outside the frame; do not use this substitute as a production asset.
- The long-title title/author/year/genre, synopsis and source row entries are local illustrative fixtures. They test fitting, not book metadata accuracy, live availability, download counts or database ownership. No stock images or blank wireframe posters were generated.
- The local Segoe UI copies are linked only from `reference/local-fonts` for review and are not bundled for redistribution. Fraunces is loaded from the frozen project's own font assets.

## Uncertainties and review gates

- OnePlus CPH2487 is disconnected. Phone logical sizes, real DPR, cutout and actual OS insets are provisional. Screenshots prove a browser mock at those logical sizes, **not** physical-device behaviour.
- HTML/CSS glass blur, Fraunces rasterisation, rich-text drop-cap baseline and hit testing approximate QML/QtWebEngine, not runtime equivalence.
- The nominated `MediaDetailScaffold.qml` is **absent from approved desktop and alpha source**, present only under rejected `reference/android-drift/qml`. The authoritative page has original inline renderers; no drift scaffold/sheet design was copied.
- Populated torrent/edition/audiobook fixture rows do not validate real provider search, downloads, reader transport, audiobook pairing or account collections.
- Portrait block ordering requires Hemanth's visual review as a **fitting choice**, not as a new UI proposal. App implementation, device test, beta, publication and all repository changes remain out of scope.

## Render verification

`node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\06-biblio-book\capture.cjs"`

Runs a temporary localhost HTTP server rooted at the frozen batch, launches isolated headless Chrome via already-installed Puppeteer, renders app-frame PNGs into `captures/`, checks horizontal overflow, cover/title overlap, core phone touch target heights, source row heights, local cover load, modal containment, and basic UI interactions. The temporary browser profile is confined to this output directory and removed after the run; no existing Chrome sessions or tabs are touched. All findings are recorded in `captures/validation.json`. **This is browser-mock verification only, not QML or Android verification.**
