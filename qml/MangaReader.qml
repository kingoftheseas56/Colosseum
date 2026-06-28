// MangaReader — native QML recreation of Tankoban Electron's MangaReader.jsx (recreate, not
// redesign). Driven by ReaderEngine.js (the verbatim layout gem) + the download-fed page source.
// PASS 2: the real reader chrome — persisted prefs (QtCore Settings, app-wide), the three modals
// (chapter grid / page-jump grid / preferences), chapter-crossing prev·next, MangaPlus
// double_page_v2, page-width control, and auto-hide chrome. Reading is DOWNLOAD-FED: pages are
// the local files from Downloads.localPages(); an undownloaded chapter shows "go download it".
import QtQuick
import QtCore
import "ReaderEngine.js" as Engine

Item {
    id: reader
    property Item backdrop
    property string seriesTitle: ""
    property string seriesId: ""
    property string seriesCover: ""           // series cover (from the series view) — for the Continue card
    property var    chapters: []              // ALL chapters (newest-first) — for the modal + crossing
    property string chapterId: ""             // incoming open target (from the series view)
    property string chapterLabel: ""          // incoming fallback label
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- preferences (app-wide, persisted; mirrors Electron mangaPrefs) ---
    Settings {
        id: prefs
        category: "mangaReader"
        property string reading_style: "long_strip"      // long_strip|single_page|double_page|double_page_v2
        property string reading_direction: "right_left"  // left_right|right_left (manga default RTL)
        property string image_fit: "width"               // width|height
        property bool   gap: true
        property bool   dark_background: true
        property bool   back_to_top: true
        property bool   sticky_top_nav: false
        property int    portrait_width_pct: 100
    }
    readonly property string style: prefs.reading_style
    readonly property string fit: prefs.image_fit
    readonly property int    portraitWidthPct: prefs.portrait_width_pct
    // MangaPlus (double_page_v2) reads LEFT-TO-RIGHT regardless of the saved direction.
    readonly property bool rtl: style === "double_page_v2" ? false : prefs.reading_direction === "right_left"
    readonly property bool isDouble: style === "double_page" || style === "double_page_v2"
    readonly property bool paged: style === "single_page" || isDouble

    // --- which chapter we're actually reading (the modal / crossing can change it) ---
    property string curChapterId: ""
    property bool   pendingAtLast: false        // open an older chapter at its last page
    onChapterIdChanged: curChapterId = chapterId
    Component.onCompleted: { curChapterId = chapterId; if (curChapterId.length) load() }
    onCurChapterIdChanged: { load(); recordProgress() }
    // grab keyboard focus whenever the reader is shown, so arrows/Esc work (it's a
    // direct child of the series page and won't get active focus on its own).
    onVisibleChanged: if (visible) reader.forceActiveFocus()

    readonly property int curIndex: {
        for (var i = 0; i < chapters.length; i++)
            if (String(chapters[i].id) === curChapterId) return i
        return -1
    }
    readonly property string curLabel: {
        var c = curIndex >= 0 ? chapters[curIndex] : null
        if (c) return (c.name && String(c.name).length) ? c.name : ("Chapter " + (c.number || ""))
        return chapterLabel
    }
    // newest-first: index-1 = newer (forward read), index+1 = older (previous)
    readonly property bool hasNewer: curIndex > 0
    readonly property bool hasOlder: curIndex >= 0 && curIndex < chapters.length - 1

    // --- data ---
    property var  pagesModel: []               // [{index, url, group}] — LOCAL file:/// urls
    property int  page: 1                       // 1-based current page (anchor in double mode)
    property bool loading: true
    property string errorMsg: ""
    property bool downloading: false
    property int  dlDone: 0
    property int  dlTotal: 0
    property var  dims: ({})                    // { index: {w,h} } natural px
    property int  couplingNudge: 0
    property bool atEnd: false                  // "all caught up" end card

    readonly property int  max: pagesModel.length

    // --- continue tracking: note how far into this series we've read, for the Continue row ---
    function recordProgress() {
        if (typeof Progress === "undefined" || !reader.seriesId.length || reader.max <= 0)
            return
        Progress.record({
            "id": reader.seriesId,
            "kind": "manga",
            "caption": reader.seriesTitle,
            "title": reader.seriesTitle,
            "sub": reader.curLabel,
            "cover": reader.seriesCover,
            "c1": "#3a2f55", "c2": "#15111f",
            "progress": Math.min(1, Math.max(0, reader.page / reader.max)),
            "resume": { "chapterId": reader.curChapterId, "page": reader.page }
        })
    }
    onPageChanged: recordProgress()

    // --- modals + HUD popups ---
    property bool showPrefs: false
    property bool showJump: false
    property bool showChapters: false
    property string hudMenu: ""                 // "mode" | "width" | ""

    Theme { id: theme }

    function load() {
        errorMsg = ""; dims = ({}); loading = false; atEnd = false
        pagesModel = (curChapterId.length && typeof Downloads !== "undefined")
                     ? Downloads.localPages(curChapterId) : []
        if (pagesModel.length > 0) {
            downloading = false
            var start = pendingAtLast ? pagesModel.length : 1
            pendingAtLast = false
            page = isDouble ? (Engine.snapTwoPageIndex(start - 1,
                       { n: pagesModel.length, isSpreadAt: function () { return false }, couplingNudge: couplingNudge }) + 1)
                            : start
            return
        }
        page = 1; pendingAtLast = false
        var st = (curChapterId.length && typeof Downloads !== "undefined")
                 ? Downloads.statusOf(curChapterId) : { state: "none", done: 0, total: 0 }
        downloading = (st.state === "downloading" || st.state === "queued")
        dlDone = st.done; dlTotal = st.total
    }

    function startDownload() {
        if (!curChapterId.length || typeof Downloads === "undefined") return
        downloading = true; errorMsg = ""
        Downloads.downloadChapter(curChapterId, seriesId, seriesTitle, curLabel)
    }

    function reportDims(i, w, h) {
        if (!w || !h || dims[i]) return
        var d = dims; d[i] = { w: w, h: h }; dims = d
    }
    function ctx() {
        return { n: reader.max,
                 isSpreadAt: function (i) { var d = reader.dims[i]; return d ? Engine.isSpread(d.w, d.h) : false },
                 couplingNudge: reader.couplingNudge }
    }
    readonly property int anchor: (isDouble && max) ? Engine.snapTwoPageIndex(page - 1, ctx()) : page - 1
    readonly property var pair: (isDouble && max) ? Engine.getTwoPagePair(anchor, ctx()) : null
    readonly property string curUrl: (page >= 1 && page <= max) ? pagesModel[page - 1].url : ""

    Connections {
        target: typeof Downloads !== "undefined" ? Downloads : null
        function onProgress(cid, done, total) {
            if (cid !== reader.curChapterId) return
            reader.downloading = true; reader.dlDone = done; reader.dlTotal = total
        }
        function onFinished(cid) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = ""; reader.load()
        }
        function onFailed(cid, reason) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = reason
        }
    }

    // --- chapter crossing (newest-first order) ---
    function openChapterById(id, atLast) {
        if (!id || !id.length) return
        pendingAtLast = !!atLast
        curChapterId = String(id)
    }
    function goNextChapter() {
        if (hasNewer) openChapterById(chapters[curIndex - 1].id, false)
        else if (chapters.length && curIndex === 0) atEnd = true
    }
    function goPrevChapter(atLast) {
        if (hasOlder) openChapterById(chapters[curIndex + 1].id, atLast)
    }

    // --- paged turning (direction-aware; crosses chapter at the ends) ---
    function turnNext() {
        if (!max) return
        if (isDouble) { var nx = Engine.stepNext(page - 1, ctx()); if (nx === null) goNextChapter(); else page = nx + 1 }
        else if (page < max) page = page + 1
        else goNextChapter()
    }
    function turnPrev() {
        if (!max) { goPrevChapter(true); return }
        if (isDouble) { var pv = Engine.stepPrev(page - 1, ctx()); if (pv === null) goPrevChapter(true); else page = pv + 1 }
        else if (page > 1) page = page - 1
        else goPrevChapter(true)
    }
    // HUD prev/next: paged turns a page; long_strip jumps chapters
    function prevAction() { paged ? turnPrev() : goPrevChapter(false) }
    function nextAction() { paged ? turnNext() : goNextChapter() }

    function setStyle(s) {
        if (s === style) return
        var keep = page
        prefs.reading_style = s
        if ((s === "double_page" || s === "double_page_v2") && max)
            page = Engine.snapTwoPageIndex(keep - 1, ctx()) + 1
    }

    // Smooth long-strip scrolling (wheel + click + edge bars): animate contentY toward
    // an accumulating target so rapid wheel notches glide instead of stepping harshly.
    property real _scrollTarget: 0
    NumberAnimation { id: scrollAnim; target: flick; property: "contentY"
        duration: 240; easing.type: Easing.OutCubic }
    function smoothScrollBy(dy) {
        var hmax = Math.max(0, flick.contentHeight - flick.height)
        var base = scrollAnim.running ? reader._scrollTarget : flick.contentY
        var t = Math.max(0, Math.min(hmax, base + dy))
        reader._scrollTarget = t
        scrollAnim.stop(); scrollAnim.from = flick.contentY; scrollAnim.to = t; scrollAnim.start()
    }
    function smoothScrollTo(y) { smoothScrollBy(y - flick.contentY) }

    // ===================== auto-hide chrome (Tankoban Max behavior) =====================
    // HUD + side bars recede while reading (after 3s idle) and STAY hidden while you read.
    // They return when you reach for them — the cursor enters the top/bottom 60px edge — or
    // on wheel / click / hovering the HUD. Keyboard scrolling does NOT wake them (immersive).
    // A modal/dropdown open or hovering the HUD freezes them shown; "Pin toolbar" pins them on.
    property bool hudShown: true
    property bool hudHover: false
    property bool edgeCooldown: false                 // brief lock after an edge-reveal (anti-flicker)
    readonly property int hudEdgePx: 60               // reveal band at top/bottom
    readonly property bool pinned: prefs.sticky_top_nav
    readonly property bool frozen: showPrefs || showJump || showChapters || hudMenu !== "" || hudHover
    readonly property bool chromeShown: hudShown || pinned || frozen
    Timer { id: idleHide; interval: 3000; running: reader.max > 0
        onTriggered: if (!reader.frozen && !reader.pinned) reader.hudShown = false }
    Timer { id: edgeCool; interval: 600; onTriggered: reader.edgeCooldown = false }
    function pokeChrome() { hudShown = true; if (!pinned) idleHide.restart() }
    onFrozenChanged: { if (frozen) { idleHide.stop(); hudShown = true } else pokeChrome() }

    // ===================== visual tree =====================
    focus: true
    // Keyboard scroll/nav — Tankoban Max key map. Scroll/turn keys deliberately do NOT pokeChrome():
    // keyboard reading keeps the HUD hidden (immersive). Only Esc + modal handling are exempt.
    Keys.onPressed: (e) => {
        // Esc — close an open modal/menu, else leave the reader
        if (e.key === Qt.Key_Escape) {
            if (showPrefs || showJump || showChapters || hudMenu !== "") {
                showPrefs = false; showJump = false; showChapters = false; hudMenu = ""
            } else reader.backRequested()
            e.accepted = true
            return
        }
        // never scroll / turn pages behind an open modal or dropdown
        if (showPrefs || showJump || showChapters || hudMenu !== "") return
        if (reader.max <= 0) return

        if (reader.style === "long_strip") {
            var shift  = (e.modifiers & Qt.ShiftModifier) !== 0
            var step   = Math.max(64, flick.height * 0.12)     // arrow
            var big    = Math.max(64, flick.height * 0.25)     // shift+arrow
            var screen = flick.height * 0.90                   // space / page
            var hmax   = Math.max(0, flick.contentHeight - flick.height)
            switch (e.key) {
            case Qt.Key_Down:     reader.smoothScrollBy(shift ? big : step);    e.accepted = true; break
            case Qt.Key_Up:       reader.smoothScrollBy(shift ? -big : -step);   e.accepted = true; break
            case Qt.Key_Space:    reader.smoothScrollBy(shift ? -screen : screen); e.accepted = true; break
            case Qt.Key_PageDown: reader.smoothScrollBy(screen);   e.accepted = true; break
            case Qt.Key_PageUp:   reader.smoothScrollBy(-screen);  e.accepted = true; break
            case Qt.Key_Home:     reader.smoothScrollTo(0);        e.accepted = true; break
            case Qt.Key_End:      reader.smoothScrollTo(hmax);     e.accepted = true; break
            }
            return
        }

        // paged (single_page / double_page) — Left/Right RTL-aware
        switch (e.key) {
        case Qt.Key_Left:     (rtl ? turnNext : turnPrev)(); e.accepted = true; break
        case Qt.Key_Right:    (rtl ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_Space:    ((e.modifiers & Qt.ShiftModifier) ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_PageDown: turnNext(); e.accepted = true; break
        case Qt.Key_PageUp:   turnPrev(); e.accepted = true; break
        case Qt.Key_Home:     reader.page = 1; e.accepted = true; break
        case Qt.Key_End:      reader.page = reader.max; e.accepted = true; break
        }
    }

    Rectangle { anchors.fill: parent; color: prefs.dark_background ? "#000000" : "#0a0b10" }

    // ── the page surface (scrolls) ──
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol ? pageCol.height : height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        pixelAligned: true
        interactive: reader.max > 0 && reader.style === "long_strip"
        opacity: reader.max > 0 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        // Smooth wheel scrolling for long-strip (default Flickable wheel steps harshly).
        WheelHandler {
            enabled: reader.style === "long_strip" && reader.max > 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (e) => { reader.pokeChrome(); reader.smoothScrollBy(-e.angleDelta.y * 1.4) }
        }

        // LONG STRIP
        Column {
            id: pageCol
            visible: reader.style === "long_strip"
            width: Math.min(flick.width, flick.width * reader.portraitWidthPct / 100)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: prefs.gap ? 8 : 0
            Repeater {
                model: reader.style === "long_strip" ? reader.pagesModel : []
                delegate: Image {
                    required property var modelData
                    required property int index
                    width: pageCol.width
                    height: (implicitWidth > 0) ? width * (implicitHeight / implicitWidth) : width * 1.45
                    fillMode: Image.PreserveAspectFit
                    source: modelData.url
                    asynchronous: true; cache: true; smooth: true; mipmap: true
                    sourceSize.width: 1100
                    onStatusChanged: if (status === Image.Ready) reader.reportDims(index, implicitWidth, implicitHeight)
                }
            }
        }

        // SINGLE PAGE
        Item {
            visible: reader.style === "single_page"
            width: flick.width; height: flick.height
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width * reader.portraitWidthPct / 100
                height: reader.fit === "height" ? parent.height : ((implicitWidth > 0) ? width * (implicitHeight / implicitWidth) : width * 1.45)
                fillMode: Image.PreserveAspectFit
                source: reader.curUrl
                asynchronous: true; cache: true; smooth: true; mipmap: true
                sourceSize.width: 1400
                onStatusChanged: if (status === Image.Ready) reader.reportDims(reader.page - 1, implicitWidth, implicitHeight)
            }
        }

        // DOUBLE PAGE (double_page + double_page_v2)
        Item {
            id: dbl
            visible: reader.isDouble && reader.pair !== null
            width: flick.width; height: flick.height
            property var layout: (reader.isDouble && reader.pair) ? Engine.computeSpreadLayout({
                kind: reader.pair.kind,
                anchorDims: reader.dims[reader.pair.anchorIndex] || { w: 800, h: 1200 },
                partnerDims: reader.pair.partnerIndex !== null ? (reader.dims[reader.pair.partnerIndex] || { w: 800, h: 1200 }) : null,
                containerW: flick.width, containerH: flick.height, gutter: 0, fitWidth: true, rtl: reader.rtl
            }) : null
            Image { visible: false; asynchronous: true; cache: true
                source: (reader.pair && reader.pair.anchorIndex < reader.max) ? reader.pagesModel[reader.pair.anchorIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair) reader.reportDims(reader.pair.anchorIndex, implicitWidth, implicitHeight) }
            Image { visible: false; asynchronous: true; cache: true
                source: (reader.pair && reader.pair.partnerIndex !== null && reader.pair.partnerIndex < reader.max) ? reader.pagesModel[reader.pair.partnerIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair && reader.pair.partnerIndex !== null) reader.reportDims(reader.pair.partnerIndex, implicitWidth, implicitHeight) }
            Row {
                anchors.centerIn: parent
                spacing: 0
                Repeater {
                    model: dbl.layout ? dbl.layout.pages : []
                    delegate: Image {
                        required property var modelData
                        property int pgIdx: modelData.role === "anchor" ? reader.pair.anchorIndex : reader.pair.partnerIndex
                        width: modelData.w; height: modelData.h
                        fillMode: Image.PreserveAspectFit
                        source: (pgIdx >= 0 && pgIdx < reader.max) ? reader.pagesModel[pgIdx].url : ""
                        asynchronous: true; cache: true; smooth: true; mipmap: true
                    }
                }
            }
        }
    }

    // ── click zones: left/right thirds turn (paged) or scroll (strip) ──
    MouseArea {
        anchors.fill: parent
        enabled: reader.max > 0 && !reader.atEnd
        acceptedButtons: Qt.LeftButton
        onClicked: (m) => {
            reader.pokeChrome()
            var third = width / 3
            if (reader.style === "long_strip") {
                if (m.x < third) reader.smoothScrollBy(-flick.height * 0.82)
                else if (m.x > width - third) reader.smoothScrollBy(flick.height * 0.82)
            } else {
                if (m.x < third) (reader.rtl ? reader.turnNext : reader.turnPrev)()
                else if (m.x > width - third) (reader.rtl ? reader.turnPrev : reader.turnNext)()
            }
        }
    }

    // reveal-on-EDGE overlay (Max behavior): NoButton so it never eats page-turn clicks.
    // Only wakes the HUD when it's currently hidden AND the cursor reaches the top/bottom
    // 60px band — so mid-screen movement while reading leaves the chrome hidden. A 600ms
    // cooldown after a reveal prevents flicker when the cursor lingers at the edge.
    MouseArea {
        anchors.fill: parent; z: 18
        enabled: reader.max > 0
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: (m) => {
            if (reader.chromeShown || reader.edgeCooldown) return
            if (m.y <= reader.hudEdgePx || m.y >= height - reader.hudEdgePx) {
                reader.pokeChrome(); reader.edgeCooldown = true; edgeCool.restart()
            }
        }
    }

    // ── download panel (no local pages; download-fed, never streams) ──
    Column {
        visible: reader.max === 0
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.7
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 26
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.curLabel; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.errorMsg.length > 0
            text: reader.errorMsg; color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: !reader.downloading && reader.errorMsg.length === 0
            text: "Not downloaded yet — download this chapter to read it offline."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.downloading
            text: reader.dlTotal > 0 ? ("Downloading… " + reader.dlDone + " / " + reader.dlTotal + " pages") : "Starting download…"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            visible: reader.downloading && reader.dlTotal > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.6; height: 6; radius: 3; color: theme.glassTint
            Rectangle { height: parent.height; radius: 3; color: theme.gold
                width: parent.width * (reader.dlTotal > 0 ? reader.dlDone / reader.dlTotal : 0)
                Behavior on width { NumberAnimation { duration: 200 } } }
        }
        Rectangle {
            visible: !reader.downloading
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 10; height: 40; width: dlt.implicitWidth + 40
            color: dlMa.containsMouse ? Qt.lighter(theme.gold, 1.1) : theme.gold
            Text { id: dlt; anchors.centerIn: parent
                text: reader.errorMsg.length ? "Retry download" : "⬇  Download chapter"
                color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 14 }
            MouseArea { id: dlMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: reader.startDownload() }
        }
    }

    // back affordance for the download panel (a real sized button, top-left)
    Rectangle {
        visible: reader.max === 0
        z: 100
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 18
        width: bkRow.implicitWidth + 28; height: 40; radius: 10
        color: bkMa.containsMouse ? theme.glassHi : theme.glassTint
        border.width: 1; border.color: theme.edge
        Row {
            id: bkRow; anchors.centerIn: parent; spacing: 7
            Text { text: "‹"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: bkMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() }
    }

    // ── "all caught up" end card ──
    Column {
        visible: reader.atEnd
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.6
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "You're all caught up"
            color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: "You've reached the latest chapter. Check back later for more."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 12
            Rectangle { radius: 9; height: 38; width: stayT.implicitWidth + 30; color: staMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: stayT; anchors.centerIn: parent; text: "‹ Stay here"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { id: staMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: reader.atEnd = false } }
            Rectangle { radius: 9; height: 38; width: bsT.implicitWidth + 30; color: theme.gold
                Text { id: bsT; anchors.centerIn: parent; text: "Back to series"; color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
        }
    }

    // ── edge side-bars (prev/next or scroll) — auto-hide ──
    component NavBar: Rectangle {
        property bool isLeft: true
        property bool shown: true
        enabled: shown
        width: 52; height: parent.height
        color: navMa.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent"
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Text { anchors.centerIn: parent; text: parent.isLeft ? "‹" : "›"
            color: navMa.containsMouse ? theme.gold : Qt.rgba(1,1,1,0.45)
            font.family: theme.display; font.pixelSize: 34 }
        MouseArea {
            id: navMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (reader.style === "long_strip") reader.smoothScrollBy(parent.isLeft ? -flick.height * 0.82 : flick.height * 0.82)
                else (parent.isLeft ? (reader.rtl ? reader.turnNext : reader.turnPrev)
                                    : (reader.rtl ? reader.turnPrev : reader.turnNext))()
            }
        }
    }
    NavBar { isLeft: true;  anchors.left: parent.left;   visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }
    NavBar { isLeft: false; anchors.right: parent.right; visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }

    // floating back-to-top (long strip)
    Rectangle {
        visible: reader.max > 0 && reader.style === "long_strip" && prefs.back_to_top && !reader.atEnd
        z: 16; width: 44; height: 44; radius: 22
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24
        color: ttMa.containsMouse ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
        Text { anchors.centerIn: parent; text: "↑"; color: ttMa.containsMouse ? "#1a1306" : theme.ink
            font.family: theme.display; font.pixelSize: 20 }
        MouseArea { id: ttMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: reader.smoothScrollTo(0) }
    }

    // ===================== HUD (frosted glass, top) =====================
    Glass {
        id: hud
        backdrop: reader.backdrop
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 10
        height: 52; radius: 14
        visible: reader.max > 0 && !reader.atEnd
        opacity: reader.chromeShown ? 1 : 0
        enabled: reader.chromeShown
        Behavior on opacity { NumberAnimation { duration: 180 } }
        z: 20
        HoverHandler { onHoveredChanged: reader.hudHover = hovered }

        // left cluster: back · series · chapter chip
        Row {
            anchors.left: parent.left; anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 14
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink; font.family: theme.display
                font.pixelSize: 24; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
            Text { text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.weight: Font.DemiBold
                font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
            // chapter chip → chapter modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: chRow.implicitWidth + 22; color: chMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: chMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                Row { id: chRow; anchors.centerIn: parent; spacing: 6
                    Text { text: "≣"; color: theme.gold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: reader.curLabel; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: chMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showChapters = true } }
        }

        // right cluster
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 10
            // prev
            Text { text: "Prev"; color: pvMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: pvMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.prevAction() } }
            // page chip → page-jump modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: pc.implicitWidth + 20; color: pgMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: pc; anchors.centerIn: parent; text: reader.page + " / " + reader.max
                    color: theme.gold; font.family: theme.display; font.pixelSize: 13; font.weight: Font.DemiBold }
                MouseArea { id: pgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showJump = true } }
            // next
            Text { text: "Next"; color: nxMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: nxMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.nextAction() } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // reading-mode dropdown trigger
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: mdRow.implicitWidth + 18; color: mdMa.containsMouse || reader.hudMenu === "mode" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: mdRow; anchors.centerIn: parent; spacing: 6
                    Text { text: reader.modeShort(reader.style); color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: mdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "mode" ? "" : "mode") } }

            // direction toggle (locked LTR in MangaPlus)
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28; width: dt.implicitWidth + 18
                color: dMa.containsMouse ? theme.glassHi : theme.glassTint; border.width: 1; border.color: theme.edge
                opacity: reader.style === "double_page_v2" ? 0.5 : 1
                Text { id: dt; anchors.centerIn: parent; text: reader.rtl ? "RTL" : "LTR"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                MouseArea { id: dMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    enabled: reader.style !== "double_page_v2"
                    onClicked: prefs.reading_direction = (reader.rtl ? "left_right" : "right_left") } }

            // change-pairing (double modes)
            Text { visible: reader.isDouble; text: "⇄"; font.pixelSize: 18
                color: reader.couplingNudge ? theme.gold : (swMa.containsMouse ? theme.gold : theme.inkDim)
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: swMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { var n = reader.couplingNudge ? 0 : 1; reader.couplingNudge = n
                        reader.page = Engine.snapTwoPageIndex(reader.page - 1,
                            { n: reader.max, isSpreadAt: function (i) { var d = reader.dims[i]; return d ? Engine.isSpread(d.w, d.h) : false }, couplingNudge: n }) + 1 } } }

            // width dropdown trigger (single/strip)
            Rectangle { visible: reader.style === "single_page" || reader.style === "long_strip"
                anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: wdRow.implicitWidth + 16; color: wdMa.containsMouse || reader.hudMenu === "width" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: wdRow; anchors.centerIn: parent; spacing: 4
                    Text { text: reader.portraitWidthPct + "%"; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: wdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "width" ? "" : "width") } }

            // settings (prefs modal)
            Text { text: "⚙"; color: grMa.containsMouse ? theme.gold : theme.inkDim; font.pixelSize: 18
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: grMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.showPrefs = true } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }
            // window controls
            Image { source: "../assets/icons/minimize.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: miMa.containsMouse ? 1 : 0.7
                MouseArea { id: miMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: poMa.containsMouse ? 1 : 0.7
                MouseArea { id: poMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.closeRequested() } }
        }
    }

    // mode short labels
    function modeShort(s) {
        if (s === "long_strip") return "Strip"
        if (s === "single_page") return "Single"
        if (s === "double_page") return "Double"
        if (s === "double_page_v2") return "MangaPlus"
        return "Mode"
    }

    // ── HUD dropdown menus (mode / width) ──
    Rectangle {
        visible: reader.hudMenu === "mode"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 140
        width: 190; height: modeCol.height + 12
        Column { id: modeCol; width: parent.width; y: 6
            Repeater {
                model: [{v:"long_strip",t:"Long Strip"},{v:"single_page",t:"Single Page"},
                        {v:"double_page",t:"Double Page"},{v:"double_page_v2",t:"Double Page (MangaPlus)"}]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 36; color: mi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t; color: reader.style === modelData.v ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    Text { visible: reader.style === modelData.v; anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter; text: "✓"; color: theme.gold; font.pixelSize: 13 }
                    MouseArea { id: mi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.setStyle(modelData.v); reader.hudMenu = "" } }
                }
            }
        }
    }
    Rectangle {
        visible: reader.hudMenu === "width"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 60
        width: 92; height: wCol.height + 12
        Column { id: wCol; width: parent.width; y: 6
            Repeater {
                model: [50, 60, 70, 74, 78, 90, 100]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 32; color: wi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData + "%"; color: reader.portraitWidthPct === modelData ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: wi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { prefs.portrait_width_pct = modelData; reader.hudMenu = "" } }
                }
            }
        }
    }
    // dismiss a HUD dropdown on outside click
    MouseArea { anchors.fill: parent; z: 29; visible: reader.hudMenu !== ""
        onClicked: reader.hudMenu = "" }

    // ===================== MODALS =====================
    component ModalScrim: Rectangle {
        anchors.fill: parent; color: Qt.rgba(0,0,0,0.62); z: 40
    }
    component ModalCard: Rectangle {
        anchors.centerIn: parent; z: 41; radius: 16; color: "#15171f"
        border.width: 1; border.color: theme.edge
    }

    // ── PREFERENCES ──
    ModalScrim { visible: reader.showPrefs; MouseArea { anchors.fill: parent; onClicked: reader.showPrefs = false } }
    ModalCard {
        visible: reader.showPrefs
        width: 420; height: prefCol.height + 40
        Column {
            id: prefCol; width: parent.width - 48; x: 24; y: 24; spacing: 6
            Text { text: "⚙  Preferences"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
                bottomPadding: 8 }
            PrefToggle { label: "Pin toolbar"; checked: prefs.sticky_top_nav; onToggled: (v) => prefs.sticky_top_nav = v }
            PrefToggle { label: "Gap between pages"; checked: prefs.gap; onToggled: (v) => prefs.gap = v }
            PrefToggle { label: "Back-to-top button"; checked: prefs.back_to_top; onToggled: (v) => prefs.back_to_top = v }
            PrefToggle { label: "Dark background"; checked: prefs.dark_background; onToggled: (v) => prefs.dark_background = v }
            Item { width: 1; height: 8 }
            Text { text: "IMAGE FIT"; color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 11; font.letterSpacing: 2 }
            PrefRadio { label: "Fit width"; checked: prefs.image_fit === "width"; onPicked: prefs.image_fit = "width" }
            PrefRadio { label: "Fit height"; checked: prefs.image_fit === "height"; onPicked: prefs.image_fit = "height" }
        }
    }

    // ── CHAPTER SELECT ──
    ModalScrim { visible: reader.showChapters; MouseArea { anchors.fill: parent; onClicked: reader.showChapters = false } }
    ModalCard {
        visible: reader.showChapters
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: chTitle; text: "≣  Select Chapter"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
            x: 24; y: 22 }
        GridView {
            anchors.top: chTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 96; cellHeight: 44
            model: reader.chapters
            delegate: Item {
                required property var modelData
                width: 96; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: String(modelData.id) === reader.curChapterId
                    color: active ? theme.gold : (cgMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent
                        text: (modelData.number !== undefined && modelData.number !== "") ? modelData.number
                              : (modelData.name || "?")
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter }
                    MouseArea { id: cgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.showChapters = false
                            if (String(modelData.id) !== reader.curChapterId) reader.openChapterById(modelData.id, false) } }
                }
            }
        }
    }

    // ── PAGE JUMP ──
    ModalScrim { visible: reader.showJump; MouseArea { anchors.fill: parent; onClicked: reader.showJump = false } }
    ModalCard {
        visible: reader.showJump
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: pgTitle; text: "Select Page"; color: theme.ink; font.family: theme.display; font.pixelSize: 18; x: 24; y: 22 }
        GridView {
            anchors.top: pgTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 60; cellHeight: 44
            model: reader.max
            delegate: Item {
                required property int index
                width: 60; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: index + 1 === reader.page
                    color: active ? theme.gold : (pgGMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent; text: index + 1
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: pgGMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.page = index + 1; reader.showJump = false
                            if (reader.style === "long_strip") flick.contentY = 0 } }
                }
            }
        }
    }

    // ── reusable pref controls ──
    component PrefToggle: Item {
        id: ptRoot
        property string label: ""
        property bool checked: false
        signal toggled(bool v)
        width: parent ? parent.width : 360; height: 40
        Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            text: ptRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 42; height: 24; radius: 12
            color: ptRoot.checked ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
            Rectangle { width: 18; height: 18; radius: 9; color: "#ffffff"; y: 3
                x: ptRoot.checked ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: 120 } } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: ptRoot.toggled(!ptRoot.checked) }
    }
    component PrefRadio: Item {
        id: prRoot
        property string label: ""
        property bool checked: false
        signal picked()
        width: parent ? parent.width : 360; height: 34
        Row { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 10
            Rectangle { width: 18; height: 18; radius: 9; color: "transparent"; border.width: 2
                border.color: prRoot.checked ? theme.gold : theme.edge; anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 5; color: theme.gold; visible: prRoot.checked } }
            Text { text: prRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: prRoot.picked() }
    }
}
