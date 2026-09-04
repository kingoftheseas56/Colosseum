# Arc 41 keyboard action residual audit

This report is generated from the current production QML tree. Pointer handlers are candidates; ownership is resolved by a nearby keyboard owner or an explicit residual rationale.

- QML files scanned: 278
- Candidate rows: 770
- Classifications: COVERED=549, DELEGATED=128, EXCEPTION=93
- Explicit residual rows: 65

## Residual classifications

| File | Rows | Classification | Rationale |
|---|---:|---|---|
| `qml/CataloguePosterCard.qml` | 2 | DELEGATED | CataloguePosterGrid owns the collection index and activation |
| `qml/FullscreenTransitionShield.qml` | 1 | EXCEPTION | Transition shield intentionally absorbs input while the shell changes state |
| `qml/MangaSeriesThumbnailMock.qml` | 10 | EXCEPTION | Retired WeebCentral route; live route is MangaSeries.qml |
| `qml/ScrollGlide.qml` | 1 | EXCEPTION | WheelHandler is pointer-only scroll mechanics; the owning page handles keyboard scroll |
| `qml/TheatreWorld.qml` | 1 | EXCEPTION | Full-screen click catcher intentionally absorbs background input |
| `qml/VaultPosterCard.qml` | 2 | DELEGATED | VaultPage owns the collection index and activation |
| `qml/VaultWideCard.qml` | 2 | DELEGATED | VaultPage owns the collection index and activation |
| `qml/WorldPage.qml` | 1 | EXCEPTION | Full-screen click catcher intentionally absorbs stray background input |
| `qml/comicreader/ComicReaderCommandBar.qml` | 1 | DELEGATED | ComicReaderKeyboardArea owns focus, activation, context, and slider keys |
| `qml/comicreader/ComicReaderHud.qml` | 14 | DELEGATED | ComicReaderKeyboardArea owns focus, activation, context, and slider keys |
| `qml/comicreader/ComicReaderLoupe.qml` | 4 | DELEGATED | ComicReaderKeyboardArea owns focus, activation, context, and slider keys |
| `qml/comicreader/ComicReaderStripSurface.qml` | 1 | EXCEPTION | WheelHandler is pointer-only scroll mechanics; the owning reader handles keyboard scroll |
| `qml/player2/Harness.qml` | 1 | EXCEPTION | Verification-only player harness, not a shipped route |
| `qml/player2/controls/PlaybackStatsCard.qml` | 1 | EXCEPTION | Stats card absorbs background clicks and exposes no command |
| `qml/reader2/BottomRail.qml` | 4 | DELEGATED | ReaderKeyboardArea owns focus, activation, context, and slider keys |
| `qml/reader2/HarnessShelf.qml` | 2 | EXCEPTION | Verification-only reader harness, not a shipped route |
| `qml/reader2/ReaderChrome.qml` | 16 | DELEGATED | ReaderKeyboardArea owns focus, activation, context, and slider keys |
| `qml/reader2/TopBar.qml` | 1 | DELEGATED | ReaderKeyboardArea owns focus, activation, context, and slider keys |

No candidate is silently omitted. `BUG` is reserved for a pointer action with no local, delegated, or explicit exceptional owner.
