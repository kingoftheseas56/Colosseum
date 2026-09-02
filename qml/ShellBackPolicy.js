.pragma library

// Pure shell-level Escape arbitration. This file selects WHO owns one Escape press;
// the owning surface still decides WHAT its semantic back/escape operation does.
// No QObject or Loader mutation belongs here, which keeps the state matrix deterministic
// and lets tests prove precedence without booting the full application shell.
function on(value) { return value === true }

function actionFor(state) {
    var s = state || ({})

    // Covers that must never let an Escape leak into hidden application state.
    if (on(s.transitioning) || on(s.bootVisible) || on(s.accountHostVisible))
        return "consume"

    // Same-window modal/transient surfaces, highest visual/interaction ownership first.
    if (on(s.identityCeremonyOpen)) return "cancelIdentityCeremony"
    if (on(s.watchPartyJoinOpen)) return "watchPartyJoin"
    if (on(s.wallpaperActive)) return "wallpaper"
    if (on(s.accountFlyoutVisible)) return "accountFlyout"
    if (on(s.accountCenterVisible)) return "accountCenter"
    if (on(s.openRecentOpen)) return "openRecent"
    if (on(s.taskbarOpen)) return "taskbar"

    // A session surface owns Escape before pages that may still be instantiated beneath it.
    // activeSessionKind also heals the audited zombie state where a raw shell close hid the
    // surface without clearing Sessions.activeId.
    if (on(s.playerOpen) || s.activeSessionKind === "movie") return "player"
    if (on(s.bookReaderActive) || s.activeSessionKind === "book") return "bookReader"
    if (on(s.vaultComicActive) || on(s.comicReaderActive) || s.activeSessionKind === "comic")
        return "comicReader"

    // Taskbar full pages all sit at z:56. Later document siblings win if a broken caller
    // ever leaves two active at once, so the policy is deterministic even in recovery.
    if (on(s.updateActive)) return "update"
    if (on(s.keyboardGuideActive)) return "keyboardGuide"
    if (on(s.settingsActive)) return "settings"
    if (on(s.extensionsActive)) return "extensions"
    if (on(s.vaultActive)) return "vault"
    if (on(s.downloadsActive)) return "downloads"

    // Detail/browse layers follow their existing z-order and document order.
    if (on(s.bookActive)) return "book"
    if (on(s.theatreSeriesActive)) return "theatreSeries"
    if (on(s.westernActive)) return "western"
    if (on(s.seriesActive)) return "series"
    if (on(s.universeActive)) return "universe"
    if (on(s.universeHallActive)) return "universeHall"
    if (on(s.searchActive)) return "search"
    if (on(s.worldSearchActive)) return "worldSearch"
    if (on(s.comicSeriesActive)) return "comicSeries"
    if (on(s.locgPublisherActive)) return "locgPublisher"
    if (on(s.comicIndexActive)) return "comicIndex"
    if (on(s.comicBoardActive)) return "comicBoard"
    if (on(s.continueSeeAllActive)) return "continueSeeAll"
    if (on(s.theatreGenreActive)) return "theatreGenre"
    if (on(s.theatreGenreIndexActive)) return "theatreGenreIndex"
    if (on(s.biblioGenreActive)) return "biblioGenre"
    if (on(s.biblioGenreIndexActive)) return "biblioGenreIndex"
    if (on(s.genreActive)) return "genre"
    if (on(s.genreIndexActive)) return "genreIndex"

    if (on(s.worldOpen)) return "world"
    return "quit"
}


