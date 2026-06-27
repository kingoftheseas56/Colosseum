// MangaSeries — the manga detail page (Tankoban mode). Colosseum series-view design (mock:
// agents/colosseum-series-mock.html, Hemanth-approved 2026-06-27): a series IS its volumes, so the
// VOLUME SHELF of real tankōbon covers is the hero AND the navigation — pick a cover and the glass
// chapter table below re-headers to it. Floats over the wallpaper; metadata is inline (no glass pills);
// gold stays a sparing accent. Data is LIVE from the native engine via the `Manga` bridge:
//   title → WeebCentral search → (chapters + detail)
//         → AniList art()      → banner / cover / synopsis / genres / year / score
//         → MangaFire volumes()→ clean per-volume covers + chapter ranges (normalized in MangaVolumes.js)
// Opened from a Top-10 manga tile.

import QtQuick
import "MangaVolumes.js" as Vol

Item {
    id: page
    property Item backdrop
    property string seriesTitle: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- resolved state ---
    property string seriesId: ""
    property string seriesUrl: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int    year: 0
    property string synopsis: ""
    property var genres: []
    property int score: 0
    property var chaptersModel: []
    property bool loading: true
    property string errorMsg: ""

    // --- seamless reveal gate ---
    // The page fires WeebCentral (chapters), AniList (art) and MangaFire (volumes) in parallel,
    // each at a different speed. We must NEVER reveal the page until ALL three are in — otherwise
    // the user sees the flat chapter list / low-q art first and watches it reflow. _maybeReveal()
    // drops `loading` only when everything is ready, so the page appears once, already finished.
    property bool chaptersReady: false
    property bool artReady: false
    property bool volumesReady: false
    function _maybeReveal() {
        if (chaptersReady && artReady && volumesReady) { loading = false; revealGuard.stop() }
    }

    // --- volumes (MangaFire via MangaVolumes.js) ---
    property var volumes: []                                  // [{number,cover,startNum,endNum,chapterStart,chapterEnd}]
    property var volGroups: Vol.group(chaptersModel, volumes) // { options:[{key,label}], byKey:{} }
    property string activeVol: ""                             // user's pick (volume number as string)
    property string shownVol: (activeVol.length && volGroups.byKey && volGroups.byKey[activeVol] !== undefined)
                              ? activeVol
                              : (volGroups.options.length ? volGroups.options[0].key : "")
    // the chapters actually shown: the active volume's, or the flat list when there's no volume data
    property var visibleChapters: loading ? []
        : (volGroups.options.length ? (volGroups.byKey[shownVol] || []) : chaptersModel)

    function volRange(volNum) {
        for (var i = 0; i < volumes.length; i++)
            if (String(volumes[i].number) === volNum)
                return volumes[i].chapterStart + "–" + volumes[i].chapterEnd
        return ""
    }

    Theme { id: theme }

    onSeriesTitleChanged: resolve()
    Component.onCompleted: if (seriesTitle.length) resolve()

    function resolve() {
        loading = true; errorMsg = ""
        seriesId = ""; banner = ""; cover = ""; author = ""; status = ""; year = 0
        synopsis = ""; genres = []; score = 0; chaptersModel = []
        volumes = []; activeVol = ""
        chaptersReady = false; artReady = false; volumesReady = false
        if (seriesTitle.length) {
            revealGuard.restart()        // never hang on a dead source — reveal what we have after N s
            Manga.search(seriesTitle)    // → chapters + WeebCentral detail
            Manga.art(seriesTitle)       // → AniList banner / cover / synopsis / genres / year
            Manga.volumes(seriesTitle)   // → MangaFire volume structure (covers + chapter ranges)
        }
    }

    // Safety net: if a source never answers, reveal after this timeout rather than spin forever.
    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    function fmtDate(ms) {
        var n = Number(ms)
        if (!n || n <= 0) return ""
        return new Date(n).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    Connections {
        target: Manga
        function onSearchResults(results) {
            if (results.length === 0) {
                page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on WeebCentral."
                page.loading = false
                return
            }
            var r = results[0]
            page.seriesId = r.id; page.seriesUrl = r.url
            // NOTE: deliberately do NOT take WeebCentral's low-res cover for the banner — that was
            // the source of the "low-q art that changes after a while" swap. The banner comes only
            // from AniList (hi-res), set in onArtResult.
            page.author = r.author; page.status = r.status
            Manga.chapters(r.id)
            Manga.detail(r.id, r.url, r.title, r.cover)
        }
        function onChaptersResults(chs) { page.chaptersModel = chs; page.chaptersReady = true; page._maybeReveal() }
        function onDetailResult(d) {
            // AniList is the source for synopsis + genres (onArtResult). WeebCentral detail only
            // contributes status + author — NOT its plainer description (AniList's reads better).
            if (d.status && d.status.length) page.status = d.status
            if (d.author && d.author.length) page.author = d.author
        }
        function onArtResult(a) {
            if (a.banner && a.banner.length) page.banner = a.banner
            if (a.cover && a.cover.length) page.cover = a.cover
            if (a.description && a.description.length) page.synopsis = a.description
            if (a.genres && a.genres.length) page.genres = a.genres
            if (a.score) page.score = a.score
            if (a.year) page.year = a.year
            page.artReady = true; page._maybeReveal()
        }
        function onVolumesResult(d) { page.volumes = Vol.fromMangaFire(d.volumes || []); page.volumesReady = true; page._maybeReveal() }
        function onEngineError(msg) { if (page.loading) { page.errorMsg = msg; page.loading = false; revealGuard.stop() } }
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                          // absorb clicks from the world page below

    // Base: a live mirror of the wallpaper so the series view FLOATS over the backdrop (the doctrine's
    // "fancy OS-widget table over the backdrop") while still hiding the world page it sits on top of.
    Rectangle { anchors.fill: parent; color: "#07080c" }        // fallback if backdrop is ever null
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
    }
    // adaptive scrim — keeps text + chrome legible over any wallpaper, darker toward the chapter list
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.42) }
            GradientStop { position: 0.42; color: Qt.rgba(0.03, 0.035, 0.05, 0.72) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.9) }
        }
    }

    // ---- top scrim so the back/window controls read against ANY background (bright banner or dark) ----
    ChromeScrim { z: 16 }

    // ---- ‹ Back (pinned, floats over the banner) ----
    Item {
        id: backBtn
        x: theme.margin; y: 28; width: backRow.implicitWidth + 16; height: 34; z: 20
        Row {
            id: backRow; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } } }
        }
        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: page.backRequested() }
    }

    // ---- window controls (minimize / power) — the SAME icons as the home/world top bar ----
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested() }
        }
    }

    // ---- the page: one vertical scroll; banner → synopsis → volume shelf → glass chapter table ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // The whole page stays invisible until fully assembled, then fades in as one finished piece.
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            // ── BANNER HERO (full-bleed art; content inset to the margin) ──
            Item {
                width: parent.width
                height: 360

                Image {
                    id: bannerImg
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                    // soft fade in when the pixels arrive — never a hard pop, even on first load
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                // wash the banner down into the page so it reads as one surface (IP color stays up top)
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.035, 0.055, 0.15) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.03, 0.035, 0.05, 0.45) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.92) }
                    }
                }

                Column {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.bottomMargin: 30
                    spacing: 12

                    Text {
                        text: "Manga · Tankoban"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.seriesTitle
                        color: theme.ink; font.family: theme.display; font.pixelSize: 64
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                    }
                    // INLINE metadata — author (bright) · status · year · ★score · genres. No glass pills.
                    Row {
                        spacing: 11
                        Text { visible: page.author.length; text: page.author
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.author.length && (page.status.length || page.year)
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length; text: page.status
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length && page.year
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.year > 0; text: page.year
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0; text: "★ " + page.score
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: page.genres.slice(0, 3).join(" · ")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    }
                    // Primary CTA — Read. (Per-volume download moved down to the chapter-table header.)
                    Row {
                        spacing: 12
                        topPadding: 8
                        Rectangle {
                            width: readRow.implicitWidth + 40; height: 42; radius: 11; color: theme.gold
                            Row {
                                id: readRow; anchors.centerIn: parent; spacing: 9
                                Text { text: "▶"; color: "#1a1306"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "Read"; color: "#1a1306"; font.family: theme.ui; font.pixelSize: 14
                                    font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onEntered: parent.opacity = 0.92; onExited: parent.opacity = 1.0 }
                        }
                    }
                }
            }

            // ── synopsis (inset) ──
            Text {
                visible: page.synopsis.length > 0
                x: theme.margin
                width: Math.min(880, parent.width - 2 * theme.margin)
                text: page.synopsis
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15
                lineHeight: 1.5; wrapMode: Text.WordWrap
                topPadding: 22; bottomPadding: 6
            }

            // ── VOLUMES — the signature shelf ──
            Item {
                width: parent.width; height: volumesSec.height
                visible: page.volumes.length > 0
                Column {
                    id: volumesSec
                    width: parent.width
                    spacing: 0
                    // section label + bright count (inline, never a pill)
                    Row {
                        x: theme.margin; spacing: 11; topPadding: 30; bottomPadding: 14
                        Text { text: "VOLUMES"; color: theme.inkDimmer; font.family: theme.display
                            font.pixelSize: 13; font.letterSpacing: 3 }
                        Text { text: page.volumes.length; color: theme.ink; font.family: theme.display
                            font.pixelSize: 15; font.weight: Font.Bold }
                    }
                    // the shelf: real tankōbon covers, scrollable, sitting on a ledge
                    Item {
                        width: parent.width; height: 214

                        // the ledge the books sit on (fixed; covers scroll over it)
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 20
                            height: 1; opacity: 0.18
                            gradient: Gradient { orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.06; color: theme.ink }
                                GradientStop { position: 0.94; color: theme.ink }
                                GradientStop { position: 1.0; color: "transparent" } }
                        }

                        ListView {
                            id: shelf
                            anchors.fill: parent
                            orientation: ListView.Horizontal
                            leftMargin: theme.margin; rightMargin: theme.margin
                            spacing: 18
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: page.volumes

                            delegate: Item {
                                id: vtile
                                required property var modelData
                                width: 116; height: 206
                                property bool on: String(modelData.number) === page.shownVol
                                Column {
                                    width: parent.width; spacing: 9
                                    Item {
                                        width: 116; height: 166
                                        y: vtile.on ? -12 : (vtMa.containsMouse ? -6 : 0)
                                        Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                                        // frame + fallback (shows the number when a cover is missing)
                                        Rectangle {
                                            anchors.fill: parent
                                            color: "#1a1c24"
                                            border.width: vtile.on ? 2 : 1
                                            border.color: vtile.on ? Qt.rgba(0.94,0.77,0.29,0.9) : Qt.rgba(1,1,1,0.12)
                                            Text {
                                                anchors.centerIn: parent
                                                visible: cover.status !== Image.Ready
                                                text: vtile.modelData.number
                                                color: Qt.rgba(1,1,1,0.5); font.family: theme.display
                                                font.pixelSize: 40; font.weight: Font.Bold
                                            }
                                        }
                                        Image {
                                            id: cover
                                            anchors.fill: parent
                                            anchors.margins: vtile.on ? 2 : 1
                                            source: vtile.modelData.cover ? vtile.modelData.cover : ""
                                            visible: status === Image.Ready
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true; cache: true
                                        }
                                    }
                                    Column {
                                        width: parent.width; spacing: 5
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "Vol " + vtile.modelData.number
                                            color: vtile.on ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 12
                                            font.weight: vtile.on ? Font.DemiBold : Font.Normal
                                        }
                                        Rectangle {
                                            visible: vtile.on
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 22; height: 2; radius: 2; color: theme.gold
                                        }
                                    }
                                }
                                MouseArea {
                                    id: vtMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.activeVol = String(vtile.modelData.number)
                                }
                            }
                        }

                        // ── sideways nav: chevrons appear when there's more shelf to scroll ──
                        NumberAnimation { id: shelfAnim; target: shelf; property: "contentX"
                            duration: 320; easing.type: Easing.OutCubic }
                        Rectangle {
                            id: navLeft
                            visible: shelf.contentX > 2
                            anchors.left: parent.left; anchors.leftMargin: 12
                            y: 60; width: 44; height: 44; radius: 22
                            color: navLeftMa.containsMouse ? Qt.rgba(0,0,0,0.66) : Qt.rgba(0,0,0,0.44)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink
                                font.family: theme.display; font.pixelSize: 28 }
                            MouseArea {
                                id: navLeftMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { shelfAnim.stop(); shelfAnim.to = Math.max(shelf.contentX - shelf.width * 0.8, 0); shelfAnim.start() }
                            }
                        }
                        Rectangle {
                            id: navRight
                            visible: shelf.contentX < shelf.contentWidth - shelf.width - 2
                            anchors.right: parent.right; anchors.rightMargin: 12
                            y: 60; width: 44; height: 44; radius: 22
                            color: navRightMa.containsMouse ? Qt.rgba(0,0,0,0.66) : Qt.rgba(0,0,0,0.44)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "›"; color: theme.ink
                                font.family: theme.display; font.pixelSize: 28 }
                            MouseArea {
                                id: navRightMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { shelfAnim.stop(); shelfAnim.to = Math.min(shelf.contentX + shelf.width * 0.8, shelf.contentWidth - shelf.width); shelfAnim.start() }
                            }
                        }
                    }
                }
            }

            // ── CHAPTER TABLE — the floating glass OS-widget; header = selected volume ──
            Item {
                width: parent.width
                height: chTable.height + 24
                visible: page.visibleChapters.length > 0

                Glass {
                    id: chTable
                    x: theme.margin
                    width: parent.width - 2 * theme.margin
                    height: tableInner.height
                    radius: 18
                    backdrop: page.backdrop
                    track: flick.contentY               // recompute blur as the page scrolls

                    Column {
                        id: tableInner
                        width: parent.width
                        // header
                        Item {
                            width: parent.width; height: 58
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                Text { text: "Vol. " + page.shownVol; color: theme.ink
                                    font.family: theme.display; font.pixelSize: 19; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: page.visibleChapters.length + " chapters"; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                // per-volume download — lives with the volume it acts on (DL wired in a later layer)
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: dlVolRow.implicitWidth + 26; height: 30; radius: 8
                                    color: dlVolMa.containsMouse ? theme.glassHi : theme.glassTint
                                    border.width: 1
                                    border.color: dlVolMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                                    Row {
                                        id: dlVolRow; anchors.centerIn: parent; spacing: 7
                                        Text { text: "↓"; color: theme.ink; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: "Download volume"; color: theme.inkDim; font.family: theme.ui
                                            font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { id: dlVolMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            var chs = page.visibleChapters
                                            for (var i = 0; i < chs.length; i++) {
                                                var id = String(chs[i].id || "")
                                                if (!id.length) continue
                                                var lbl = (chs[i].name && String(chs[i].name).length)
                                                          ? chs[i].name : ("Chapter " + (chs[i].number || ""))
                                                Downloads.downloadChapter(id, page.seriesId, page.seriesTitle, lbl)
                                            }
                                        } }
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                text: { var r = page.volRange(page.shownVol); return r.length ? "Ch " + r : "" }
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                        }
                        // chapter rows (selected volume → bounded count → a Repeater is fine)
                        Repeater {
                            model: page.visibleChapters
                            delegate: Item {
                                id: row
                                required property var modelData
                                width: tableInner.width; height: 92

                                // per-row download state, kept live via the Downloads signals
                                property string chId: String(row.modelData.id || "")
                                property string dlState: "none"   // none | queued | downloading | done | error
                                property int dlDone: 0
                                property int dlTotal: 0
                                readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                property string liveThumb: ""   // first-page url for an UNdownloaded chapter (scraped)
                                // chapter thumbnail = its FIRST page: downloaded -> local file (instant),
                                // else the scraped first-page url resolved via Downloads.fetchThumb.
                                readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : row.liveThumb
                                function firstLocalUrl() {
                                    if (typeof Downloads === "undefined") return ""
                                    var lp = Downloads.localPages(row.chId)
                                    return (lp && lp.length) ? lp[0].url : ""
                                }
                                function chLabel() {
                                    return (row.modelData.name && String(row.modelData.name).length)
                                        ? row.modelData.name : ("Chapter " + (row.modelData.number || ""))
                                }
                                function statusLine() {
                                    if (dlState === "done") return "● Downloaded"
                                    if (dlState === "queued") return "Queued…"
                                    if (dlState === "downloading")
                                        return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                                    if (dlState === "error") return "⚠ Failed — tap to retry"
                                    return ""
                                }
                                function openReader() {
                                    page.openChapterId = row.chId
                                    page.openChapterLabel = row.chLabel()
                                }
                                function startDownload() {
                                    if (typeof Downloads === "undefined" || !row.chId.length) return
                                    row.dlState = "queued"
                                    Downloads.downloadChapter(row.chId, page.seriesId, page.seriesTitle, row.chLabel())
                                }
                                // download-fed: tap reads a downloaded chapter, else downloads it (the reader only opens what's on disk)
                                function primary() {
                                    if (row.dlState === "done") row.openReader()
                                    else if (!row.inFlight) row.startDownload()
                                }
                                function refreshDl() {
                                    if (typeof Downloads === "undefined") return
                                    var st = Downloads.statusOf(row.chId)
                                    row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                                }
                                function requestThumb() {
                                    if (typeof Downloads !== "undefined") Downloads.fetchThumb(page.seriesId, row.chId)
                                }
                                Component.onCompleted: { refreshDl(); requestThumb() }
                                Connections {
                                    target: typeof Downloads !== "undefined" ? Downloads : null
                                    function onProgress(cid, done, total) {
                                        if (cid !== row.chId) return
                                        row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total
                                    }
                                    function onFinished(cid) { if (cid === row.chId) row.dlState = "done" }
                                    function onFailed(cid, reason) { if (cid === row.chId) row.dlState = "error" }
                                    function onThumbReady(cid, url) { if (cid === row.chId && url.length) row.liveThumb = url }
                                    function onRemoved(cid) {
                                        if (cid !== row.chId) return
                                        row.dlState = "none"; row.liveThumb = ""; row.requestThumb()
                                    }
                                }

                                Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }

                                // thumbnail (portrait) — first page once downloaded, numbered placeholder otherwise
                                Item {
                                    id: thumb
                                    anchors.left: parent.left; anchors.leftMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 58; height: 80
                                    Rectangle {
                                        anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1
                                        border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                            text: row.modelData.number || "?"; color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 22 }
                                    }
                                    Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                        source: row.thumbUrl; visible: status === Image.Ready
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                        sourceSize.width: 170 }
                                }

                                // title + status subtitle
                                Column {
                                    anchors.left: thumb.right; anchors.leftMargin: 16
                                    anchors.right: trailing.left; anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                    Text { width: parent.width; text: row.chLabel()
                                        color: rowMa.containsMouse ? theme.gold : theme.ink
                                        font.family: theme.ui; font.pixelSize: 17; elide: Text.ElideRight }
                                    Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                        color: row.dlState === "done" ? theme.gold
                                             : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                        font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                                }

                                // trailing control: ✓→✕ delete (done) · ✕ cancel (in-flight) · ↓/↻ download/retry
                                Item {
                                    id: trailing
                                    anchors.right: parent.right; anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 36; height: 36
                                    Rectangle { anchors.fill: parent; radius: 18
                                        color: trMa.containsMouse ? theme.glassHi : "transparent" }
                                    Text {
                                        anchors.centerIn: parent
                                        text: row.dlState === "done" ? (trMa.containsMouse ? "✕" : "✓")
                                            : row.inFlight ? "✕"
                                            : row.dlState === "error" ? "↻" : "↓"
                                        color: (row.dlState === "done" && trMa.containsMouse) ? "#e6a3a3"
                                             : row.dlState === "done" ? theme.gold
                                             : trMa.containsMouse ? theme.gold : theme.inkDim
                                        font.pixelSize: 16
                                        font.weight: (row.dlState === "done" && !trMa.containsMouse) ? Font.Bold : Font.Normal
                                    }
                                    MouseArea {
                                        id: trMa; anchors.fill: parent; hoverEnabled: true; z: 5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            if (row.dlState === "done") Downloads.deleteChapter(row.chId)
                                            else if (row.inFlight) Downloads.cancelDownload(row.chId)
                                            else row.startDownload()
                                        }
                                    }
                                }

                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                    color: Qt.rgba(1,1,1,0.05); visible: row.y + row.height < tableInner.height }
                                MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: row.primary() }
                            }
                        }
                    }
                }
            }

            // post-reveal error (inset)
            Text {
                visible: !page.loading && page.errorMsg.length > 0
                x: theme.margin
                text: page.errorMsg
                color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }   // bottom breathing room
        }
    }

    // ---- clean loading state ----
    // Shown while the page assembles; it fades out as the finished page fades in (see Flickable opacity),
    // so the user sees one calm transition — never the flat list or low-q art being built in front of them.
    Column {
        id: loadingState
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.seriesTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading…"
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }
    }

    // ---- reader overlay: opened from a chapter row (the recreated Tankoban reader) ----
    // Direct child (NOT a Loader+inline-Component): inside a nested Component the outer
    // `page` id does not resolve, so every page.* binding was undefined and onBackRequested
    // silently threw — the reader could never close. As a direct child, `page` resolves
    // like everywhere else. Idle cost is nil: with no chapterId it fetches nothing and
    // visible:false removes it from input.
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.seriesId
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.minimizeRequested()
        onCloseRequested: page.closeRequested()
    }
}
