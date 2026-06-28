// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar (clock·pills·system) → Universe hero → unified Continue row → per-medium trending rows.
// Mock data only (no Universe data engine yet). Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import "Catalog.js" as Catalog
import "Universes.js" as Universes
import "UniverseApi.js" as UniverseApi
import "McuApi.js" as Mcu
import "TheatreApi.js" as TheatreApi
import "ContinueCovers.js" as ContinueCovers

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    property string currentSurface: "Home"
    property string wallpaperSource: "../assets/wallpaper/captured-motion.jpg"

    Settings {
        id: wallpaperSettings
        location: Qt.resolvedUrl("../wallpapers.ini")
        category: "wallpapers"
        property string homePick: ""
        property string tankobanPick: ""
        property string biblioPick: ""
        property string theatrePick: ""
    }

    function wallpaperKey(world) {
        if (world === "Tankoban") return "tankobanPick"
        if (world === "Biblio") return "biblioPick"
        if (world === "Theatre") return "theatrePick"
        return "homePick"
    }
    function parsePick(raw) {
        if (!raw) return null
        if (typeof raw === "object") return raw
        try {
            var parsed = JSON.parse(raw)
            if (typeof parsed === "string") return { image_url: parsed }
            return parsed
        } catch (e) {
            return { image_url: raw }
        }
    }
    function pickFor(world) {
        return parsePick(wallpaperSettings[wallpaperKey(world)])
    }
    function wallpaperWorldForSession(rec) {
        var appType = rec && rec.appType ? ("" + rec.appType).toLowerCase() : ""
        var contentKind = rec && rec.contentKind ? ("" + rec.contentKind).toLowerCase() : ""
        if (appType === "theatre" || contentKind === "movie") return "Theatre"
        if (appType === "tankoban" || contentKind === "comic") return "Tankoban"
        if (appType === "biblio" || contentKind === "book") return "Biblio"
        return currentSurface || "Home"
    }
    function refreshWallpaper() {
        var pick = pickFor(currentSurface)
        wallpaperSource = pick && pick.image_url ? pick.image_url : "../assets/wallpaper/captured-motion.jpg"
    }
    function setWallpaperPick(world, pick) {
        wallpaperSettings[wallpaperKey(world)] = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        if (currentSurface === world) refreshWallpaper()
    }
    function setWallpaperEverywhere(pick) {
        var raw = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        wallpaperSettings.homePick = raw
        wallpaperSettings.tankobanPick = raw
        wallpaperSettings.biblioPick = raw
        wallpaperSettings.theatrePick = raw
        refreshWallpaper()
    }

    Component.onCompleted: refreshWallpaper()

    // Esc: close the series page if open, else leave a world page, else quit. Ctrl+Q always quits.
    Shortcut { sequences: ["Escape"]; onActivated: {
        if (win.playerOpen) win.closePlayer()
        else if (bookReaderLayer.active) win.closeBookReader()
        else if (bookLayer.active) win.closeBook()
        else if (biblioSeriesLayer.active) win.closeBiblioSeries()
        else if (biblioGenreLayer.active) win.closeBiblioGenre()
        else if (searchLayer.active) win.closeSearch()
        else if (worldSearchLayer.active) win.closeWorldSearch()
        else if (theatreSeriesLayer.active) win.closeTheatreSeries()
        else if (seriesLayer.active) win.closeSeries()
        else if (genreLayer.active) win.closeGenre()
        else if (genreIndexLayer.active) win.closeGenreIndex()
        else if (universeLayer.active) win.closeUniverse()
        else if (worldStack.current !== "") win.closeWorld()
        else Qt.quit()
    } }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: Qt.quit() }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell keeps
    // running, art stays warm). A frameless fullscreen window has no normal frame to land in, so when
    // Windows restores it from the taskbar we snap it straight back to fullscreen — never a stray bare
    // rectangle stuck with no titlebar to grab.
    function minimizeShell() { win.showMinimized() }
    onVisibilityChanged: if (win.visibility === Window.Windowed) win.visibility = Window.FullScreen

    // ---- navigation: open a medium's world page over the persistent wallpaper ----
    // Each visited mode keeps ONE live Loader (created on first entry, never destroyed); navigating
    // Home or between modes just toggles visibility. So returning to a mode shows the already-loaded
    // world with its covers INTACT instead of re-downloading them. Real mode pages route to their
    // own QML; unbuilt modes fall back to DemoWorld.qml.
    function worldSourceFor(medium) {
        if (medium === "Tankoban") return "TankobanWorld.qml"
        if (medium === "Theatre") return "TheatreWorld.qml"
        if (medium === "Biblio") return "BiblioWorld.qml"
        return "DemoWorld.qml"
    }
    function openWorld(medium) {
        var found = false
        for (var i = 0; i < openModes.count; i++)
            if (openModes.get(i).mode === medium) { found = true; break }
        if (!found) openModes.append({ mode: medium })   // first visit → create its keep-alive Loader
        worldStack.current = medium
        currentSurface = medium
        refreshWallpaper()
        topbar.visible = false
        page.visible = false
    }
    function closeWorld() {
        worldStack.current = ""                           // hide all worlds; none destroyed
        currentSurface = "Home"
        refreshWallpaper()
        topbar.visible = true
        page.visible = true
    }

    // ---- universe page: a cross-medium destination over the wallpaper, from the home hero ----
    //      The shell picks the right TEMPLATE by the universe's category (anime vs cinematic);
    //      the Loader reloads onto that source, so Marvel opens the CinematicPage, One Piece the
    //      anime UniversePage. ----
    function universeSourceFor(category) {
        return category === "cinematic" ? "CinematicPage.qml" : "UniversePage.qml"
    }
    function openUniverse(name) {
        universeLayer.universeName = name
        universeLayer.universeSource = win.universeSourceFor(Universes.categoryFor(name))
        if (universeLayer.item) universeLayer.item.universeName = name
        universeLayer.active = true
        topbar.visible = false
        page.visible = false
    }
    function closeUniverse() {
        universeLayer.active = false
        topbar.visible = true
        page.visible = true
    }

    function openGenre(name) {
        genreLayer.genreName = name
        if (genreLayer.active && genreLayer.item) genreLayer.item.genreName = name
        else genreLayer.active = true
    }
    function closeGenre() { genreLayer.active = false }

    // ---- genre INDEX (the "Explore" directory of all genres) — a layer below the genre page so a
    //      picked genre opens its GenrePage over the index. Reached from a genre widget's "Explore"
    //      or the genre page's "Explore" pill. ----
    function openGenreIndex() { genreIndexLayer.active = true }
    function closeGenreIndex() { genreIndexLayer.active = false }

    function openBiblioGenre(name) {
        biblioGenreLayer.genreName = name
        if (biblioGenreLayer.active && biblioGenreLayer.item) biblioGenreLayer.item.genreName = name
        else biblioGenreLayer.active = true
    }
    function closeBiblioGenre() { biblioGenreLayer.active = false }

    // ---- series detail: a layer over the current world page (opened from a Top-10 title tile) ----
    function openSeries(title) {
        seriesLayer.resumeSeriesId = ""
        seriesLayer.resumeChapterId = ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list
            seriesLayer.item.seriesTitle = title
        } else seriesLayer.active = true
    }
    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).
    function openSeriesAt(title, seriesId, chapterId) {
        seriesLayer.resumeSeriesId = seriesId || ""
        seriesLayer.resumeChapterId = chapterId || ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.seriesTitle = title
            if (seriesId) seriesLayer.item.seriesId = seriesId
            seriesLayer.item.openChapterId = chapterId || ""
        } else seriesLayer.active = true
    }
    function closeSeries() { seriesLayer.active = false }

    // ---- Theatre detail: its own layer (Cinemeta meta + Torrentio sources), parallel to series ----
    function openTheatreSeries(item) {
        theatreSeriesLayer.pendingItem = item
        if (theatreSeriesLayer.active && theatreSeriesLayer.item) theatreSeriesLayer.item.itemData = item
        else theatreSeriesLayer.active = true
    }
    function closeTheatreSeries() { theatreSeriesLayer.active = false }

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    function openPlayer(infoHash, fileIdx, title, backdrop, subType, subId) {
        if (!playerLayer.active) playerLayer.active = true
        win.playerOpen = true
        // `backdrop` is the poster url; subType/subId (e.g. "movie"/"tt123" or "series"/"tt123:1:2")
        // let the player fetch online subtitles for this exact title/episode.
        playerLayer.item.playTorrent(infoHash, fileIdx, title, backdrop, subType, subId)
    }
    function closePlayer() {
        if (playerLayer.item) playerLayer.item.stop()
        win.playerOpen = false
    }

    // ---- book detail: Biblio's own dust-jacket page, a layer over the world ----
    function openBook(b) {
        bookLayer.book = b
        bookLayer.active = true
    }
    function closeBook() { bookLayer.active = false }

    // ---- the reader: foliate EPUB reader over everything (download-fed, never a stream) ----
    function openBookReader(path, book) {
        if (!path) return
        bookReaderLayer.bookPath = path
        bookReaderLayer.bookMeta = book || ({})
        if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(path, book || ({}))
        else bookReaderLayer.active = true
    }
    function closeBookReader() { bookReaderLayer.active = false }

    // ---- series detail: Biblio's FictionDB series page, a layer over the world ----
    function openBiblioSeries(group) {
        biblioSeriesLayer.group = group
        biblioSeriesLayer.active = true
    }
    function closeBiblioSeries() { biblioSeriesLayer.active = false }

    // ---- search: a layer over the world. Biblio has its own rich surface; Tankoban + Theatre use the
    //      generic SearchSurface fed by their own source (AniList / Cinemeta). ----
    function openSearch() {
        var w = worldStack.current
        if (w === "Biblio") { searchLayer.active = true; return }
        if (w === "Tankoban") {
            worldSearchLayer.searchMode = "Tankoban"
            worldSearchLayer.placeholder = "Search manga…"
            worldSearchLayer.active = true
        } else if (w === "Theatre") {
            worldSearchLayer.searchMode = "Theatre"
            worldSearchLayer.placeholder = "Search movies & series…"
            worldSearchLayer.active = true
        }
    }
    function closeSearch() { searchLayer.active = false }
    function closeWorldSearch() { worldSearchLayer.active = false }
    function routeWorldSearchItem(data) {
        win.closeWorldSearch()
        if (worldSearchLayer.searchMode === "Tankoban") win.openSeries(data.title)
        else if (worldSearchLayer.searchMode === "Theatre") win.openTheatreSeries(data)
    }

    // ---- Continue card has TWO actions: the center icon RESUMES into the content; clicking
    //      elsewhere opens the SERIES / DETAIL view. Both use the resume payload each world wrote. ----
    //  resume (center play/read icon):
    function resumeContinue(entry) {
        if (!entry) return
        var r = entry.resume || ({})
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            if (r.infoHash) win.openMovieSession(r.infoHash, r.fileIdx || 0, title, entry.cover || "")
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            win.openComicSession(title, entry.id || "", r.chapterId || "")
        } else if (entry.kind === "book") {
            if (r.path) win.openBookSession(r.path, r.book ? r.book : entry)
            else win.openBook(r.book ? r.book : entry)
        }
    }
    //  detail (click anywhere else on the card): the series / movie / book page.
    function detailContinue(entry) {
        if (!entry) return
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            var id = (entry.id || "").split(":")[0]                      // base tt id (strip episode suffix)
            if (id.indexOf("tt") !== 0) { win.resumeContinue(entry); return }   // raw torrent, no Theatre page
            // resolve movie vs series live from Cinemeta (probe series first; a hit → series, else movie),
            // then open the Theatre detail. No stored type needed, so existing entries work too.
            TheatreApi.loadMeta("series", id, function(meta) {
                win.openTheatreSeries({ id: id, type: meta ? "series" : "movie",
                                        title: title, cover: entry.cover || "" })
            })
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            win.openSeries(title)                                        // the series page (chapter list)
        } else if (entry.kind === "book") {
            win.openBook(entry.resume && entry.resume.book ? entry.resume.book : entry)
        }
    }

    // ===== OS-shell session engine (Approach 2: only the active surface is instantiated) =====
    // The UI opens content by registering a SESSION; Sessions.activeChanged then drives the
    // capture -> teardown -> build -> restore switch. contentKind picks the surface.

    // UI entry points (replace direct open* calls from cards / world pages):
    function openMovieSession(infoHash, fileIdx, title, backdrop, subType, subId) {
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": title || "Movie",
            "target": { "infoHash": infoHash, "fileIdx": fileIdx || 0, "title": title || "",
                        "backdrop": backdrop || "", "subType": subType || "", "subId": subId || "" }
        })
    }
    function openComicSession(title, seriesId, chapterId) {
        Sessions.openOrSwitch({
            "appType": "tankoban", "contentKind": "comic", "title": title || "Comic",
            "target": { "title": title || "", "seriesId": seriesId || "", "chapterId": chapterId || "" }
        })
    }
    function openBookSession(path, book) {
        if (!path) return
        var b = book || ({})
        Sessions.openOrSwitch({
            "appType": "biblio", "contentKind": "book", "title": b.title || "Book",
            "target": { "path": path, "book": b, "id": (b.id !== undefined ? ("" + b.id) : path) }
        })
    }

    // dispatcher: build the active surface from a record (+ restore its saved state).
    function activateSession(rec) {
        if (!rec || !rec.id) return
        var t = rec.target || ({})
        var st = rec.savedState || ({})
        currentSurface = wallpaperWorldForSession(rec)
        refreshWallpaper()
        if (rec.contentKind === "movie") {
            if (!playerLayer.active) playerLayer.active = true
            win.playerOpen = true
            playerLayer.item.playTorrent(t.infoHash, t.fileIdx || 0, t.title, t.backdrop, t.subType, t.subId)
            if (playerLayer.item.restoreState) playerLayer.item.restoreState(st)   // precision: Task 5
        } else if (rec.contentKind === "comic") {
            seriesLayer.resumeSeriesId = t.seriesId || ""
            seriesLayer.resumeChapterId = (st.chapterId || t.chapterId || "")
            seriesLayer.title = t.title
            if (seriesLayer.active && seriesLayer.item) {
                seriesLayer.item.seriesTitle = t.title
                if (t.seriesId) seriesLayer.item.seriesId = t.seriesId
                seriesLayer.item.openChapterId = (st.chapterId || t.chapterId || "")
            } else seriesLayer.active = true
            if (seriesLayer.item && seriesLayer.item.restoreState) seriesLayer.item.restoreState(st)  // Task 4
        } else if (rec.contentKind === "book") {
            bookReaderLayer.bookPath = t.path
            bookReaderLayer.bookMeta = t.book || ({})
            if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(t.path, t.book || ({}))
            else bookReaderLayer.active = true
            // book precision: foliate auto-restores its own CFI on reopen of the same path (Task 6).
        }
    }
    // capture the live outgoing surface's state (called BEFORE teardown).
    function captureSession(rec) {
        if (!rec || !rec.id) return ({})
        if (rec.contentKind === "movie" && playerLayer.item && playerLayer.item.captureState) return playerLayer.item.captureState()
        if (rec.contentKind === "comic" && seriesLayer.item && seriesLayer.item.captureState) return seriesLayer.item.captureState()
        if (rec.contentKind === "book"  && bookReaderLayer.item && bookReaderLayer.item.captureState) return bookReaderLayer.item.captureState()
        return ({})
    }
    // tear the outgoing surface down. Player: stop media but KEEP the mpv host (use-after-free guard).
    function teardownSession(rec) {
        if (!rec || !rec.id) return
        if (rec.contentKind === "movie") {
            if (playerLayer.item) playerLayer.item.stop()
            win.playerOpen = false
        } else if (rec.contentKind === "comic") {
            seriesLayer.active = false
        } else if (rec.contentKind === "book")  {
            bookReaderLayer.active = false
        }
    }

    // ---- design tokens (the skin: glass is the constant; gold is sparing) ----
    Theme { id: theme }

    // ---- bundled editorial serif (the theme's target display face: theme.display = "Fraunces") ----
    FontLoader { source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../assets/fonts/Fraunces-Italic.ttf" }

    // =====================================================================
    // BACKDROP — the persistent wallpaper everything composites over.
    // =====================================================================
    Item {
        id: wall
        anchors.fill: parent
        // Real OS wallpaper — a placeholder PICK (Windows 11 "Captured Motion"; its translucent
        // glass-ribbon motif echoes our material, and it's dark enough for the glass to read).
        // Swap from the parked personalization gallery later. Glass composites over WHATEVER sits in
        // `wall`, so the Image "just works" — and it pops against the chrome instead of reading as an app.
        Image {
            anchors.fill: parent
            source: win.wallpaperSource
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        // gentle global vignette so chrome + text read against the wallpaper, bright or dark
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.34) }
                GradientStop { position: 0.5; color: Qt.rgba(0,0,0,0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.46) }
            }
        }
    }

    // ---- reusable: a clickable row header (the nav-in to a world). Continue isn't a world,
    //      so it opts out with navigable:false (no chevron, no click). ----
    component RowHeader: Item {
        id: rh
        property string title
        property bool navigable: true
        signal clicked()
        implicitWidth: rhRow.implicitWidth
        implicitHeight: rhRow.implicitHeight
        Row {
            id: rhRow
            spacing: 8
            Text {
                text: rh.title
                color: (rh.navigable && rhMa.containsMouse) ? theme.ink : theme.inkDim
                font.family: theme.display; font.pixelSize: 23
            }
            Text {
                text: "›"
                visible: rh.navigable
                color: theme.gold; font.pixelSize: 22
                opacity: rhMa.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: rhMa; anchors.fill: parent
            hoverEnabled: rh.navigable
            cursorShape: rh.navigable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (rh.navigable) rh.clicked()
        }
    }

    // ---- reusable: a unified Continue card (glass chrome, solid art slot) ----
    component ContinueCard: Glass {
        id: card
        backdrop: wall
        width: 340; height: 148; radius: 14
        signal resumeActivated()   // center play/read icon → resume INTO the content
        signal detailActivated()   // anywhere else on the card → the series / detail view
        property string kind       // "video" → play glyph; "manga"/"comic"/"book" → read glyph
        property string badge
        property string title
        property string sub
        property real progress: 0
        property color art: "#333"
        property string cover: ""
        Row {
            anchors.fill: parent
            Item {   // art slot — real cover over a gradient fallback
                width: 112; height: parent.height
                clip: true
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.lighter(art, 1.25) }
                        GradientStop { position: 1; color: Qt.darker(art, 1.4) }
                    }
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    source: cover
                    visible: cover.length > 0 && status === Image.Ready
                }
            }
            Item {
                width: parent.width - 112; height: parent.height
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 15; spacing: 0
                    Text {
                        text: badge; color: theme.gold
                        font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.3
                        Layout.alignment: Qt.AlignLeft
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: title; color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { text: sub; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; topPadding: 4; bottomPadding: 8 }
                    Rectangle {
                        Layout.fillWidth: true; height: 4; radius: 2; color: Qt.rgba(1,1,1,0.2)
                        Rectangle { width: parent.width * progress; height: parent.height; radius: 2; color: theme.gold }
                    }
                }
            }
        }
        // click ANYWHERE on the card → the series / detail view (the center button below sits on
        // top, so a click on it never falls through to this one)
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.detailActivated()
        }
        // the center play / read button, over the cover — resumes INTO the content
        Rectangle {
            id: resumeBtn
            width: 48; height: 48; radius: 24
            x: 56 - width / 2                       // centered on the 112px cover slot
            anchors.verticalCenter: parent.verticalCenter
            color: rbHov.hovered ? Qt.rgba(0,0,0,0.80) : Qt.rgba(0,0,0,0.55)
            border.width: 1.5; border.color: Qt.rgba(1,1,1,0.9)
            scale: rbHov.hovered ? 1.08 : 1.0
            Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
            Image {
                anchors.centerIn: parent
                width: card.kind === "video" ? 19 : 22; height: width
                source: card.kind === "video" ? "../assets/icons/play.svg"
                      : card.kind === "book"  ? "../assets/icons/books.svg"
                      : "../assets/icons/manga.svg"
                fillMode: Image.PreserveAspectFit
            }
            HoverHandler { id: rbHov }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.resumeActivated() }
        }
    }

    // (PortraitTile · Pill · SysIcon · the top bar now live in shared sibling files:
    //  PortraitTile.qml and TopBar.qml — reused by the world-page template.)

    // =====================================================================
    // FOREGROUND
    // =====================================================================

    // ---- 1. TOP BAR (fixed, glass over wallpaper) — shared shell chrome.
    //      activeMedium "" → HOME: no pill selected (the no-selection rule). Tapping a pill
    //      enters that world. ----
    TopBar {
        id: topbar
        z: 20
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onMinimizeClicked: win.minimizeShell()
        onPowerClicked: Qt.quit()
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        z: 0
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: win.height - 96
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 40
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: theme.margin
            width: win.width - theme.margin * 2
            topPadding: 10
            spacing: 30

            // ---- 2. UNIVERSE HERO — a real cycling carousel of the universe collection ----
            //      Swipe/drag between universes · dots track + jump · auto-advances. Real banner
            //      key-art (TMDB / AniList, disk-cached). Data: Universes.universes.
            Glass {
                id: hero
                backdrop: wall
                track: page.contentY
                width: parent.width; height: 340; radius: 20
                tint: 0.06

                SwipeView {
                    id: heroView
                    anchors.fill: parent
                    clip: true
                    Repeater {
                        model: Universes.universes
                        delegate: Item {
                            required property var modelData

                            // banner key-art (solid content), rounded to the panel; the IP color
                            // stands in while it loads, then a left-weighted scrim keeps text legible.
                            Rectangle {
                                anchors.fill: parent; radius: hero.radius; clip: true
                                color: modelData.c1 ? modelData.c1 : "#1a1410"
                                Image {
                                    anchors.fill: parent
                                    source: modelData.banner
                                    asynchronous: true; cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: status === Image.Ready ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 300 } }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.86) }
                                        GradientStop { position: 0.52; color: Qt.rgba(0,0,0,0.42) }
                                        GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.06) }
                                    }
                                }
                            }

                            // content (chrome over the art)
                            Column {
                                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 44
                                spacing: 12
                                Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                                Text { text: modelData.name; color: theme.ink; font.family: theme.display; font.pixelSize: 48 }
                                Text {
                                    text: modelData.blurb
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; width: 500; wrapMode: Text.WordWrap
                                }
                                // medium counts as an inline editorial metadata line (bright count · dim
                                // medium) — NOT glass pills, NO gold separators. Transparent "tablet" chips
                                // read cheap over busy banner art (Hemanth, 2026-06-27).
                                Row {
                                    spacing: 22
                                    Repeater {
                                        model: modelData.chips
                                        delegate: Text {
                                            required property var modelData
                                            textFormat: Text.StyledText
                                            font.family: theme.ui; font.pixelSize: 15
                                            text: {
                                                var s = modelData.t
                                                var i = s.indexOf(" ")
                                                var first = i < 0 ? s : s.substring(0, i)
                                                // bold the leading COUNT only; a medium name with no
                                                // number (incl. multi-word like "Graphic Novel") stays
                                                // one uniform weight — never half-bold.
                                                if (!/^\d/.test(first))
                                                    return "<font color='#c9c8d0'>" + s + "</font>"
                                                return "<b><font color='#f7f7f5'>" + first +
                                                       "</font></b> <font color='#c9c8d0'>" + s.substring(i + 1) + "</font>"
                                            }
                                        }
                                    }
                                }
                                Row {
                                    spacing: 12; topPadding: 6
                                    Rectangle {
                                        id: exploreBtn
                                        radius: 12; height: 46; width: exploreRow.implicitWidth + 44
                                        gradient: Gradient {
                                            GradientStop { position: 0; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.23) : Qt.rgba(1,1,1,0.14) }
                                            GradientStop { position: 1; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05) }
                                        }
                                        border.width: 1
                                        border.color: exMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.85) : Qt.rgba(1,1,1,0.26)
                                        Behavior on border.color { ColorAnimation { duration: 160 } }
                                        Row {
                                            id: exploreRow; anchors.centerIn: parent; spacing: 10
                                            Text { text: "Explore the universe"; color: theme.ink
                                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                                anchors.verticalCenter: parent.verticalCenter }
                                            Text { text: "→"; color: theme.gold; font.pixelSize: 16
                                                anchors.verticalCenter: parent.verticalCenter
                                                transform: Translate {
                                                    x: exMa.containsMouse ? 3 : 0
                                                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                                }
                                            }
                                        }
                                        MouseArea {
                                            id: exMa; anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: win.openUniverse(Universes.universes[heroView.currentIndex].name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // dots — track the current universe, click to jump (overlay above the SwipeView)
                Row {
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 30
                    spacing: 8; z: 5
                    Repeater {
                        model: Universes.universes.length
                        delegate: Rectangle {
                            required property int index
                            width: index === heroView.currentIndex ? 22 : 8; height: 8; radius: 4
                            color: index === heroView.currentIndex ? theme.gold : Qt.rgba(1,1,1,0.35)
                            Behavior on width { NumberAnimation { duration: 150 } }
                            MouseArea {
                                anchors.fill: parent; anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: heroView.currentIndex = index
                            }
                        }
                    }
                }

                // gentle auto-advance through the collection
                Timer {
                    interval: 6500; running: true; repeat: true
                    onTriggered: heroView.currentIndex = (heroView.currentIndex + 1) % Universes.universes.length
                }
            }

            // ---- 3. CONTINUE (one unified row, all mediums mixed; scrolls horizontally) ----
            //      Real resume data from the Progress store; hidden entirely until there's
            //      something to resume. (Naming Progress.revision keeps the binding live.)
            Column {
                id: contCol
                width: parent.width
                spacing: 14
                property var contItems: (Progress.revision, Progress.recent("", 12))
                visible: contItems.length > 0
                function badgeFor(k) {
                    return ({ video: "VIDEO", manga: "MANGA", comic: "COMIC", book: "BOOK" })[k]
                           || (k ? k.toUpperCase() : "")
                }
                RowHeader { title: "Continue"; navigable: false }   // unified resume row, not a world
                Flickable {
                    id: contFlick
                    width: parent.width; height: 148
                    contentWidth: contRow.width; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: contRow
                        spacing: 18
                        Repeater {
                            model: contCol.contItems
                            delegate: ContinueCard {
                                id: contCard
                                required property var modelData
                                // saved cover, or — for a manga saved without art — an AniList fallback
                                property string resolvedCover: (modelData.cover !== undefined && ("" + modelData.cover).length)
                                                               ? modelData.cover : ""
                                Component.onCompleted: if (!resolvedCover && (modelData.kind === "manga" || modelData.kind === "comic"))
                                    ContinueCovers.fetch(modelData.title || modelData.caption || "", function(u) { contCard.resolvedCover = u })
                                track: page.contentY + contFlick.contentX
                                kind: modelData.kind !== undefined ? modelData.kind : ""
                                badge: contCol.badgeFor(modelData.kind)
                                title: modelData.title !== undefined ? modelData.title : (modelData.caption || "")
                                sub: modelData.sub !== undefined ? modelData.sub : ""
                                cover: resolvedCover
                                progress: modelData.progress !== undefined ? modelData.progress : 0
                                art: modelData.c1 !== undefined ? modelData.c1 : "#333"
                                onResumeActivated: win.resumeContinue(modelData)
                                onDetailActivated: win.detailContinue(modelData)
                            }
                        }
                    }
                }
            }

            // ---- 4. MODE-INTRO WIDGETS — the board that introduces each app AND shows what's inside.
            //      First prototype: Tankoban as a BOOKSHELF (manga covers standing on a shelf ledge).
            //      The other modes get their own widget forms next; this is the shape to react to.
            Bookshelf {
                backdrop: wall
                track: page.contentY
                width: parent.width
                mangaBooks: Catalog.topManga
                comicsBooks: Catalog.topComics
                onClicked: win.openWorld("Tankoban")
                onBookClicked: win.openWorld("Tankoban")
            }

            Item { width: 1; height: 16 }   // bottom breathing room
        }
    }

    // ---- world pages: one keep-alive Loader PER visited mode, stacked over the home on the SAME
    //      wallpaper. worldStack.current picks which is visible; "" = home. Kept alive so covers
    //      don't re-fetch on return (the home's top bar + scroll hide while a world is up). ----
    ListModel { id: openModes }
    Item {
        id: worldStack
        anchors.fill: parent
        property string current: ""                      // "" = home; else the visible mode
        Repeater {
            model: openModes
            delegate: Loader {
                required property string mode
                anchors.fill: parent
                visible: worldStack.current === mode
                active: true
                source: win.worldSourceFor(mode)
                onLoaded: {
                    item.medium = mode
                    item.backdrop = wall
                    item.homeRequested.connect(win.closeWorld)
                    item.mediumSelected.connect(win.openWorld)
                    item.seriesRequested.connect(win.openSeries)
                    item.bookRequested.connect(win.openBook)
                    item.genreRequested.connect(win.openGenre)
                    if (item.genreIndexRequested) item.genreIndexRequested.connect(win.openGenreIndex)
                    var biblioGenreSignal = item["biblio" + "GenreRequested"]
                    if (biblioGenreSignal) biblioGenreSignal.connect(win.openBiblioGenre)
                    if (item.continueResumeRequested) item.continueResumeRequested.connect(win.resumeContinue)
                    if (item.continueDetailRequested) item.continueDetailRequested.connect(win.detailContinue)
                    if (mode === "Theatre") {
                        var theatreSignal = item["theatre" + "ItemRequested"]
                        if (theatreSignal) theatreSignal.connect(win.openTheatreSeries)
                    }
                    item.searchClicked.connect(win.openSearch)
                    item.minimizeClicked.connect(win.minimizeShell)
                    item.powerClicked.connect(function() { Qt.quit() })
                }
            }
        }
    }

    // ---- universe page layer: opened from the home hero "Explore the universe". Its source is the
    //      per-category template (anime UniversePage / cinematic CinematicPage), chosen in
    //      openUniverse(). Signal sets differ per template, so each optional connect is guarded. ----
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 40
        active: false
        visible: active
        property string universeName: ""
        property string universeSource: "UniversePage.qml"
        source: universeSource
        onLoaded: {
            item.backdrop = wall
            item.universeName = universeLayer.universeName
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            if (item.searchClicked) item.searchClicked.connect(win.openSearch)
            if (item.seriesRequested) item.seriesRequested.connect(win.openSeries)   // anime template only
            if (item.watchRequested) item.watchRequested.connect(win.openTheatreSeries)
        }
    }

    Loader {
        id: genreLayer
        anchors.fill: parent
        z: 45
        active: false
        visible: active
        property string genreName: ""
        source: "GenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = genreLayer.genreName
            item.backRequested.connect(win.closeGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.seriesRequested.connect(win.openSeries)
            item.exploreRequested.connect(function() { win.closeGenre(); win.openGenreIndex() })
        }
    }

    // ---- genre INDEX layer (the "Explore" directory). z below genreLayer so picking a genre opens
    //      its page over the index. ----
    Loader {
        id: genreIndexLayer
        anchors.fill: parent
        z: 44
        active: false
        visible: active
        source: "GenreIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.genrePicked.connect(win.openGenre)
        }
    }

    Loader {
        id: biblioGenreLayer
        anchors.fill: parent
        z: 46
        active: false
        visible: active
        property string genreName: ""
        source: "BiblioGenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = biblioGenreLayer.genreName
            item.backRequested.connect(win.closeBiblioGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.bookRequested.connect(win.openBook)
        }
    }

    // ---- series detail layer: opened from a Top-10 title tile, sits OVER the world page ----
    Loader {
        id: seriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string title: ""
        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…
        property string resumeChapterId: ""   //   …in this series (set seriesId BEFORE the chapter)
        source: "MangaSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = seriesLayer.title
            if (seriesLayer.resumeSeriesId) item.seriesId = seriesLayer.resumeSeriesId
            if (seriesLayer.resumeChapterId) item.openChapterId = seriesLayer.resumeChapterId
            item.backRequested.connect(win.closeSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- Theatre detail layer: opened from a Theatre tile, sits OVER the world page ----
    Loader {
        id: theatreSeriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property var pendingItem: ({})
        source: "TheatreSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.itemData = theatreSeriesLayer.pendingItem
            item.backRequested.connect(win.closeTheatreSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.playRequested.connect(win.openMovieSession)
        }
    }

    // ---- video player layer: above every detail/series layer (mpv under house glass) ----
    Loader {
        id: playerLayer
        anchors.fill: parent
        z: 60
        active: false
        visible: win.playerOpen
        source: "PlayerPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closePlayer)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- book detail layer: Biblio's OWN dust-jacket page over the world (above series) ----
    Loader {
        id: bookLayer
        anchors.fill: parent
        z: 53
        active: false
        visible: active
        property var book: ({})
        source: "BiblioBook.qml"
        onLoaded: {
            item.backdrop = wall
            item.book = bookLayer.book
            item.backRequested.connect(win.closeBook)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.readRequested.connect(win.openBookReader)
        }
    }

    // ---- the reader: foliate EPUB reader (WebEngine), over the book detail ----
    Loader {
        id: bookReaderLayer
        anchors.fill: parent
        z: 58
        active: false
        visible: active
        property string bookPath: ""
        property var bookMeta: ({})
        source: "BookReader.qml"
        onLoaded: {
            item.open(bookReaderLayer.bookPath, bookReaderLayer.bookMeta)
            item.closed.connect(win.closeBookReader)
            item.minimizeRequested.connect(win.minimizeShell)
        }
    }

    // ---- generic world search layer: Tankoban + Theatre (SearchSurface + their own source) ----
    Loader {
        id: worldSearchLayer
        anchors.fill: parent
        z: 51
        active: false
        visible: active
        property string searchMode: ""
        property string placeholder: "Search…"
        source: "SearchSurface.qml"
        onLoaded: {
            item.backdrop = wall
            item.searchMode = worldSearchLayer.searchMode
            item.placeholder = worldSearchLayer.placeholder
            item.backRequested.connect(win.closeWorldSearch)
            item.itemRequested.connect(win.routeWorldSearchItem)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- search layer: Biblio's search surface over the world (below the book detail) ----
    Loader {
        id: searchLayer
        anchors.fill: parent
        z: 51
        active: false
        visible: active
        source: "BiblioSearch.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeSearch)
            item.homeRequested.connect(function() { win.closeSearch(); win.closeWorld() })
            item.bookRequested.connect(win.openBook)
            item.seriesRequested.connect(win.openBiblioSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- Biblio series detail layer: opened from a SERIES card (above search, below the book detail) ----
    Loader {
        id: biblioSeriesLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        property var group: ({})
        source: "BiblioSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.group = biblioSeriesLayer.group
            item.backRequested.connect(win.closeBiblioSeries)
            item.bookRequested.connect(win.openBook)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- universe art warmer: once the shell is up, quietly pull the BUILT universes' art into the
    //      disk cache so opening "Explore" shows it INSTANTLY (the app's download-once-then-instant
    //      model). Idle work — runs after boot, off the critical path; hidden Images do the warming.
    //      Bounded to the two built exemplars (One Piece anime, Marvel cinematic). ----
    Item {
        id: universeWarmer
        property var opUrls: []
        property var mcuUrls: []
        property var warmUrls: opUrls.concat(mcuUrls)
        function warm() {
            UniverseApi.loadUniverse("One Piece", function(u) { universeWarmer.opUrls = UniverseApi.imageUrls(u) })
            Mcu.loadMcu(function(d) { universeWarmer.mcuUrls = Mcu.imageUrls(d) })
        }
        Repeater {
            model: universeWarmer.warmUrls
            delegate: Image {
                required property string modelData
                source: modelData
                asynchronous: true; cache: true; visible: false
            }
        }
    }

    // ---- session switch glue: capture the outgoing surface, tear it down, build + restore the next ----
    Connections {
        target: Sessions
        function onActiveChanged(prevId, nextId) {
            var prev = Sessions.get(prevId)
            if (prev && prev.id) {
                Sessions.saveState(prevId, win.captureSession(prev))
                win.teardownSession(prev)
            }
            var next = Sessions.get(nextId)
            if (next && next.id) win.activateSession(next)
        }
    }

    // ---- OS-style boot loader: prefetch covers, then fade away to reveal the shell with art warm ----
    BootSplash {
        id: boot
        anchors.fill: parent
        z: 1000
        onFinished: { bootFade.start(); universeWarmer.warm() }
        NumberAnimation { id: bootFade; target: boot; property: "opacity"; to: 0; duration: 400
            onFinished: boot.visible = false }
    }
}
