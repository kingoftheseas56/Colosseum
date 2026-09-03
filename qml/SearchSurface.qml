// SearchSurface — the reusable search overlay for the non-Biblio worlds (Tankoban, Theatre). Same
// Harbor-adapted shape as Biblio's search (field leads, Top Match hero, results grid, recent), but
// source-agnostic: it asks WorldSearch.searchFor(searchMode, …) and emits itemRequested(data) for the
// host to route to that world's detail. (Biblio keeps its own richer BiblioSearch with series + libgen.)

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "WorldSearch.js" as WorldSearch

Item {
    id: surf
    objectName: searchMode.length ? searchMode.toLowerCase() + "SearchSurface" : "searchSurface"
    property Item backdrop
    property string searchMode: ""                   // "Tankoban" | "Theatre"
    property string placeholder: "Search…"
    property string primaryLabel: "Open"
    property var results: []
    property bool searching: false
    property bool searched: false
    property var expandedGroups: []   // group names the user opened via "See more" (per query)
    property var recent: []
    property string lastDispatchedQuery: ""
    property var historyStore: typeof SearchHistory !== "undefined" ? SearchHistory : null
    property var searchDispatcher: WorldSearch.searchFor
    property string searchError: ""
    property string browseError: ""
    readonly property int resultCount: results.length
    readonly property int recentCount: recent.length
    readonly property bool showingProviderError: searchError.length > 0 && !searching
    readonly property bool showingNoResults: searched && results.length === 0
        && !searching && searchError.length === 0
    property var _searchCancel: null
    property var _browseCancel: null
    property var _surpriseCancel: null
    property int _searchGeneration: 0
    property int _browseGeneration: 0
    property int _surpriseGeneration: 0

    signal backRequested()
    signal itemRequested(var data)
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    readonly property bool isEmpty: queryInput.text.trim().length === 0

    // Harbor's empty-state "Try a genre": chips open an inline, popularity-ranked browse grid in place.
    readonly property var genres: WorldSearch.genresFor(searchMode)
    property string browseGenre: ""            // "" = the default empty view; set = inline genre grid
    property var browseItems: []
    property bool browseLoading: false
    property bool surprising: false

    Theme { id: theme }
    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => { if (!event.accepted) searchScrollKeys.handle(event) }
    MouseArea { anchors.fill: parent }
    Component.onCompleted: {
        surf.loadRecent()
        queryInput.forceActiveFocus()
    }
    Component.onDestruction: {
        surf.cancelAllRequests()
        surf.commitCurrentQuery()
    }
    onSearchModeChanged: {
        surf.cancelAllRequests()
        surf.loadRecent()
    }
    onHistoryStoreChanged: surf.loadRecent()
    Connections {
        target: surf.historyStore
        function onChanged(scope) {
            if (scope === surf.historyScope())
                surf.loadRecent()
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // race guard: apply a result only if its query still matches the field
    function cancelHandle(handle) {
        if (!handle) return
        if (typeof handle === "function") { handle(); return }
        if (handle.cancel && typeof handle.cancel === "function") handle.cancel()
    }
    function cancelSearch() {
        surf._searchGeneration++
        surf.cancelHandle(surf._searchCancel)
        surf._searchCancel = null
        surf.searching = false
    }
    function cancelBrowse() {
        surf._browseGeneration++
        surf.cancelHandle(surf._browseCancel)
        surf._browseCancel = null
        surf.browseLoading = false
    }
    function cancelSurprise() {
        surf._surpriseGeneration++
        surf.cancelHandle(surf._surpriseCancel)
        surf._surpriseCancel = null
        surf.surprising = false
    }
    function cancelAllRequests() {
        surf.cancelSearch()
        surf.cancelBrowse()
        surf.cancelSurprise()
    }
    function runSearch() {
        var q = queryInput.text.trim()
        surf.cancelSearch()
        if (q.length < 2) {
            surf.lastDispatchedQuery = ""
            surf.searchError = ""
            surf.results = []; surf.searched = false; return
        }
        surf.lastDispatchedQuery = q
        surf.searchError = ""   // fresh dispatch → stale provider error must not linger
        surf.expandedGroups = []   // a NEW query starts collapsed again (See-more state is per-query)
        surf.searching = true
        if (typeof GuiStallProbe !== "undefined" && GuiStallProbe)
            GuiStallProbe.setContext("search", surf.searchMode)
        var generation = surf._searchGeneration
        surf._searchCancel = surf.searchDispatcher(surf.searchMode, q, function(items, error) {
            if (generation !== surf._searchGeneration || q !== queryInput.text.trim()) return
            surf.results = items || []
            surf.searchError = error || ""
            surf.searching = false
            surf.searched = true
        }, (typeof ComicsCatalog !== "undefined") ? ComicsCatalog : null)
    }
    function historyScope() { return surf.searchMode.toLowerCase() }
    function loadRecent() { surf.recent = surf.historyStore ? surf.historyStore.list(surf.historyScope()) : [] }
    function commitCurrentQuery() {
        var q = queryInput.text.trim()
        if (surf.historyStore && q.length >= 2 && q === surf.lastDispatchedQuery)
            surf.recent = surf.historyStore.record(surf.historyScope(), q)
    }
    function fillAndSearch(q) { queryInput.text = q; runSearch(); commitCurrentQuery() }
    function openItem(data) { surf.cancelAllRequests(); surf.commitCurrentQuery(); surf.itemRequested(data) }
    function openTop() { if (surf.results.length > 0) surf.openItem(surf.results[0].data) }
    function removeRecent(q) { if (surf.historyStore) surf.recent = surf.historyStore.remove(surf.historyScope(), q) }
    function keepSearchDelegateVisible(item) {
        if (!item || !scroll) return
        const p = item.mapToItem(scroll.contentItem, 0, 0)
        if (p.y < scroll.contentY + 12) scroll.contentY = Math.max(0, p.y - 12)
        else if (p.y + item.height > scroll.contentY + scroll.height - 12)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height), p.y + item.height - scroll.height + 12)
    }

    // Harbor's genre-browse: open a genre into an inline grid (guarded so a slow reply for a genre
    // you've since left doesn't paint over the new one).
    function openGenre(g) {
        surf.cancelBrowse()
        surf.browseGenre = g
        surf.browseItems = []
        surf.browseError = ""
        surf.browseLoading = true
        var generation = surf._browseGeneration
        surf._browseCancel = WorldSearch.browseGenre(surf.searchMode, g, function(items, error) {
            if (generation !== surf._browseGeneration || surf.browseGenre !== g) return
            surf.browseItems = items || []
            surf.browseError = error || ""
            surf.browseLoading = false
        })
    }
    function closeGenre() { surf.cancelBrowse(); surf.browseGenre = ""; surf.browseItems = []; surf.browseError = "" }
    function doSurprise() {
        if (surf.surprising) return
        surf.cancelSurprise()
        surf.surprising = true
        var generation = surf._surpriseGeneration
        surf._surpriseCancel = WorldSearch.surprise(surf.searchMode, function(item, error) {
            if (generation !== surf._surpriseGeneration) return
            surf.surprising = false
            surf.browseError = error || ""
            if (item && item.data) surf.itemRequested(item.data)
        })
    }

    // results (minus the Top Match) split into ordered sections by their group — Movies / Series for
    // Theatre, a single Manga group for Tankoban (Harbor-style grouped discovery).
    function groupedResults() {
        var rest = surf.results.length > 1 ? surf.results.slice(1) : []
        var order = [], map = ({})
        for (var i = 0; i < rest.length; i++) {
            var g = rest[i].group || "Results"
            if (!map[g]) { map[g] = []; order.push(g) }
            map[g].push(rest[i])
        }
        return order.map(function(g) { return { group: g, items: map[g] } })
    }

    Timer { id: debounce; interval: 220; onTriggered: surf.runSearch() }

    Shortcut { sequences: ["Return", "Enter"]; onActivated: { debounce.stop(); surf.runSearch(); surf.commitCurrentQuery(); surf.openTop() } }

    // ── visible exit (audit fix: Esc was the only door out) + the search field ──
    BackAction {
        id: searchBack
        x: theme.margin
        anchors.verticalCenter: field.verticalCenter
        onTriggered: { surf.cancelAllRequests(); surf.commitCurrentQuery(); surf.backRequested() }
    }
    Rectangle {
        id: field
        // The field stops short of the window-chrome cluster instead of running under it: both were
        // anchored to the same right margin, and their vertical bands overlapped by 12px, so the
        // minimize/fullscreen/power buttons sat on the field's top-right corner. Derived from the
        // chrome row's own width so it can never drift apart again.
        x: searchBack.x + searchBack.width + 20; y: 44
        width: surf.width - x - theme.margin - winChrome.width - 24; height: 72; radius: 16
        color: Qt.rgba(0, 0, 0, 0.30); border.width: 1; border.color: theme.edge

        Canvas {
            id: glass; width: 21; height: 21
            x: 22; anchors.verticalCenter: parent.verticalCenter
            onPaint: {
                var ctx = getContext("2d"); ctx.reset()
                ctx.strokeStyle = "#9a99a5"; ctx.lineWidth = 1.7; ctx.lineCap = "round"
                ctx.beginPath(); ctx.arc(9, 9, 6.3, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(13.6, 13.6); ctx.lineTo(19.5, 19.5); ctx.stroke()
            }
        }
        TextInput {
            id: queryInput
            objectName: "searchSurfaceInput"
            anchors.left: glass.right; anchors.leftMargin: 15
            anchors.right: rightCluster.left; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            color: theme.ink; font.family: theme.display; font.pixelSize: 22
            clip: true; focus: true; selectByMouse: true
            onTextChanged: { surf.cancelSearch(); debounce.restart() }
            Keys.onEscapePressed: { surf.cancelAllRequests(); surf.commitCurrentQuery(); surf.backRequested() }
        }
        Text {
            visible: queryInput.text.length === 0
            anchors.left: glass.right; anchors.leftMargin: 15
            anchors.verticalCenter: parent.verticalCenter
            text: surf.placeholder; color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 22
        }
        Row {
            id: rightCluster
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter; spacing: 12
            Rectangle {
                visible: queryInput.text.length > 0
                width: 26; height: 26; radius: 13
                anchors.verticalCenter: parent.verticalCenter
                color: clearMa.containsMouse ? Qt.rgba(1,1,1,0.14) : Qt.rgba(1,1,1,0.06)
                Text { anchors.centerIn: parent; text: "✕"; color: theme.inkDimmer; font.pixelSize: 12 }
                MouseArea { id: clearMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { queryInput.text = ""; queryInput.forceActiveFocus() } }
                KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Clear search"; focusRadius: 13
                    onTriggered: { queryInput.text = ""; queryInput.forceActiveFocus() } }
            }
            Rectangle {
                width: escTxt.width + 16; height: 24; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"; border.width: 1; border.color: theme.edge
                Text { id: escTxt; anchors.centerIn: parent; text: "Esc"; color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.5 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { surf.cancelAllRequests(); surf.backRequested() } }
                KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Exit search"; focusRadius: 6
                    onTriggered: { surf.cancelAllRequests(); surf.backRequested() } }
            }
        }
    }

    // ── content ──
    // Render structure deliberately mirrors BiblioSearch's Flickable (the surface that provably paints
    // through this same active-toggled Loader): an UNCLIPPED Top Match card carrying a SINGLE
    // layer.enabled effect (the cover shadow). The earlier divergence — a clipped card wrapping a
    // second full-fill MultiEffect blur backdrop — was the black-paint bug (a blurred FBO-backed layer
    // inside a freshly-activated Loader subtree never painted). No backdrop blur, no card clip, no
    // hairline workaround: match the proven painter, don't patch a broken one.
    Flickable {
        id: scroll
        anchors.top: field.bottom; anchors.topMargin: 30
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.right: parent.right; anchors.rightMargin: theme.margin
        anchors.bottom: parent.bottom; anchors.bottomMargin: 8
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 30
        boundsBehavior: Flickable.StopAtBounds
        pixelAligned: false
        ScrollBar.vertical: HouseScrollBar { flick: scroll }

        Column {
            id: content
            width: scroll.width
            spacing: 0

            // ── empty state = Harbor's extended view. Default: Recent + Try-a-genre + Surprise me.
            //    Picking a genre swaps THIS view for an inline, popularity-ranked browse grid. ──
            Column {
                width: parent.width; spacing: 0
                visible: surf.isEmpty

                // ===== INLINE GENRE BROWSE (replaces the default view while a genre is open) =====
                Column {
                    visible: surf.browseGenre.length > 0
                    width: parent.width; spacing: 0

                    Row {
                        spacing: 16
                        BackAction {                                     // subview exit → back to search
                            label: "Search"
                            labelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                            onTriggered: surf.closeGenre()
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter; spacing: 2
                            Text { text: "BROWSING"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                                font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                            Text { text: surf.browseGenre; color: theme.ink; font.family: theme.display; font.pixelSize: 24 }
                        }
                    }
                    Item { width: 1; height: 24 }

                    Text {
                        visible: surf.browseLoading && surf.browseItems.length === 0
                        text: "Loading…"; color: theme.inkDimmer; font.family: theme.display
                        font.pixelSize: 18; topPadding: 16
                    }
                    Grid {
                        id: browseGrid
                        visible: surf.browseItems.length > 0
                        width: parent.width; columns: 6; columnSpacing: 22; rowSpacing: 26
                        property real cellW: (width - columnSpacing * (columns - 1)) / columns
                        property int currentIndex: surf.browseItems.length > 0 ? 0 : -1
                        focusPolicy: surf.browseItems.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => browseKeys.handle(event)
                        KeyboardCollectionController {
                            id: browseKeys; view: browseGrid; orientation: "grid"; columns: browseGrid.columns; count: surf.browseItems.length
                            positionIndexFn: function(index) { surf.keepSearchDelegateVisible(browseRepeater.itemAt(index)) }
                            onActivated: (index) => surf.openItem(surf.browseItems[index].data)
                        }
                        Repeater {
                            id: browseRepeater
                            model: surf.browseItems
                            delegate: Column {
                                id: browseCard
                                required property var modelData
                                required property int index
                                readonly property bool keyboardSelected: browseGrid.activeFocus && browseGrid.currentIndex === index
                                width: browseGrid.cellW; spacing: 9
                                Rectangle {
                                    width: parent.width; height: width * 1.5; radius: 8; clip: true; color: "#14131a"
                                    border.width: browseCard.keyboardSelected ? 2 : 0; border.color: theme.gold
                                    Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                    scale: bcMa.containsMouse ? 1.03 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    MouseArea { id: bcMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: surf.openItem(modelData.data) }
                                }
                                Text { width: parent.width; text: modelData.title ? modelData.title : ""
                                    color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                    elide: Text.ElideRight; maximumLineCount: 1 }
                                Text { width: parent.width; text: modelData.subtitle ? modelData.subtitle : ""
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                    elide: Text.ElideRight; maximumLineCount: 1 }
                            }
                        }
                    }
                    Text {
                        visible: !surf.browseLoading && surf.browseItems.length === 0 && surf.browseError.length === 0
                        text: "Nothing here"; color: theme.inkDimmer; font.family: theme.display
                        font.pixelSize: 18; topPadding: 16
                    }
                }

                Text {
                    visible: surf.browseError.length > 0
                    text: surf.browseError
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                // ===== DEFAULT EMPTY VIEW =====
                Column {
                    visible: surf.browseGenre.length === 0
                    width: parent.width; spacing: 0

                    // RECENT SEARCHES — chip searches; ✕ removes (Harbor parity)
                    Text {
                        visible: surf.recent.length > 0
                        text: "RECENT SEARCHES"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.letterSpacing: 1.8
                    }
                    Item { visible: surf.recent.length > 0; width: 1; height: 16 }
                    Flow {
                        id: recentFlow
                        visible: surf.recent.length > 0
                        width: parent.width; spacing: 10
                        property int currentIndex: surf.recent.length > 0 ? 0 : -1
                        focusPolicy: surf.recent.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => recentKeys.handle(event)
                        KeyboardCollectionController {
                            id: recentKeys; view: recentFlow; orientation: "horizontal"; count: surf.recent.length; contextEnabled: true
                            onActivated: (index) => surf.fillAndSearch(surf.recent[index])
                            onContextRequested: (index) => surf.removeRecent(surf.recent[index])
                        }
                        Repeater {
                            id: recentRepeater
                            model: surf.recent
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool keyboardSelected: recentFlow.activeFocus && recentFlow.currentIndex === index
                                height: 40; radius: 999; width: rcRow.width + 30
                                color: rcMa.containsMouse || keyboardSelected ? Qt.rgba(1,1,1,0.10) : theme.glassTint
                                border.width: keyboardSelected ? 2 : 1; border.color: keyboardSelected ? theme.gold : theme.edge
                                MouseArea { id: rcMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: surf.fillAndSearch(modelData) }
                                Row { id: rcRow; anchors.centerIn: parent; spacing: 8
                                    Text { text: modelData; color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                        anchors.verticalCenter: parent.verticalCenter }
                                    Rectangle { width: 20; height: 20; radius: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: xMa.containsMouse ? Qt.rgba(1,1,1,0.18) : "transparent"
                                        Text { anchors.centerIn: parent; text: "✕"; color: theme.inkDimmer; font.pixelSize: 10 }
                                        MouseArea { id: xMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: surf.removeRecent(modelData) } }
                                }
                            }
                        }
                    }
                    Item { visible: surf.recent.length > 0; width: 1; height: 36 }

                    // TRY A GENRE — a chip opens the inline browse grid above
                    Text {
                        text: "TRY A GENRE"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.letterSpacing: 1.8
                    }
                    Item { width: 1; height: 16 }
                    Flow {
                        id: genreFlow
                        width: parent.width; spacing: 10
                        property int currentIndex: surf.genres.length > 0 ? 0 : -1
                        focusPolicy: surf.genres.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => genreKeys.handle(event)
                        KeyboardCollectionController {
                            id: genreKeys; view: genreFlow; orientation: "grid"; columns: Math.max(1, Math.min(4, surf.genres.length)); count: surf.genres.length
                            onActivated: (index) => surf.openGenre(surf.genres[index])
                        }
                        Repeater {
                            id: genreRepeater
                            model: surf.genres
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool keyboardSelected: genreFlow.activeFocus && genreFlow.currentIndex === index
                                height: 44; radius: 999; width: gLbl.width + 36
                                color: gMa.containsMouse || keyboardSelected ? Qt.rgba(1,1,1,0.10) : theme.glassTint
                                border.width: keyboardSelected ? 2 : 1; border.color: keyboardSelected ? theme.gold : theme.edge
                                Text { id: gLbl; anchors.centerIn: parent; text: modelData
                                    color: gMa.containsMouse ? theme.ink : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13 }
                                MouseArea { id: gMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: surf.openGenre(modelData) }
                            }
                        }
                    }
                    Item { width: 1; height: 42 }

                    // SURPRISE ME — random genre → random top title → opens it
                    Item {
                        width: parent.width; height: 24
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                            Text { text: "✦"; color: surMa.containsMouse ? theme.gold : theme.inkDimmer; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { text: surf.surprising ? "Picking…" : "Surprise me"
                                color: surMa.containsMouse ? theme.ink : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea { id: surMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: surf.doSurprise() }
                        KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Surprise me"; focusRadius: 8
                            onTriggered: surf.doSurprise() }
                    }
                }
            }

            // results state
            Column {
                objectName: "searchSurfaceResults"
                width: parent.width; spacing: 0
                visible: !surf.isEmpty

                Text {
                    visible: surf.results.length > 0
                    text: "TOP MATCH"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: surf.results.length > 0; width: 1; height: 14 }
                Rectangle {
                    id: topCard
                    // Automation identity (Lanista), same DiscoverBrowser.qml:727 precedent: keyed by
                    // the result's own data.id (theatre meta.id) or its title fallback, never index.
                    objectName: (topCard.m && topCard.m.title)
                                ? ("searchResult_" + String(topCard.m.data && topCard.m.data.id ? topCard.m.data.id : topCard.m.title))
                                : ""
                    visible: surf.results.length > 0
                    property var m: surf.results.length > 0 ? surf.results[0] : ({})
                    width: parent.width; height: 210; radius: 18
                    color: theme.glassTint; border.width: 1; border.color: theme.edge

                    Item {                                   // cover-object (Biblio's mini dust-jacket)
                        id: tmCover
                        anchors.left: parent.left; anchors.leftMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110; height: 165
                        Image {
                            id: tmImg; anchors.fill: parent
                            source: topCard.m && topCard.m.cover ? topCard.m.cover : ""
                            fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                            layer.enabled: true
                            layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0,0,0,0.7)
                                shadowBlur: 1.0; shadowVerticalOffset: 16; autoPaddingEnabled: true }
                        }
                        Rectangle { anchors.left: parent.left; width: 8; height: parent.height; radius: 2
                            gradient: Gradient { orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Qt.rgba(0,0,0,0.5) }
                                GradientStop { position: 0.6; color: Qt.rgba(0,0,0,0.05) }
                                GradientStop { position: 1; color: Qt.rgba(1,1,1,0.08) } } }
                    }
                    Column {
                        anchors.left: tmCover.right; anchors.leftMargin: 28
                        anchors.right: openBtn.left; anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Text { width: parent.width; text: topCard.m && topCard.m.title ? topCard.m.title : ""
                            color: theme.ink; font.family: theme.display; font.pixelSize: 32
                            elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { width: parent.width
                            text: (topCard.m ? (topCard.m.meta || topCard.m.subtitle || "") : "").toUpperCase()
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                            font.letterSpacing: 1.0; elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { visible: text.length > 0; width: parent.width
                            text: topCard.m && topCard.m.synopsis ? topCard.m.synopsis : ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.32
                            wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight; opacity: 0.85 }
                    }
                    Rectangle {
                        id: openBtn
                        anchors.right: parent.right; anchors.rightMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 116; height: 48; radius: 12; color: theme.gold
                        Row { anchors.centerIn: parent; spacing: 7
                            Text { text: surf.primaryLabel; color: "#241a05"; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { text: "›"; color: "#241a05"; font.pixelSize: 16 } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (topCard.m) surf.openItem(topCard.m.data) }
                    }
                    MouseArea { anchors.fill: parent; z: -1; onClicked: if (topCard.m) surf.openItem(topCard.m.data) }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Open top match"; focusRadius: 18
                        onTriggered: if (topCard.m) surf.openItem(topCard.m.data) }
                }
                Item { visible: surf.results.length > 0; width: 1; height: 38 }

                // grouped sections (Movies / Series / Manga) — capped at one row until "See more"
                Repeater {
                    model: surf.groupedResults()
                    delegate: Column {
                        id: section
                        required property var modelData          // { group, items }
                        readonly property bool expanded: surf.expandedGroups.indexOf(modelData.group) >= 0
                        function toggleExpanded() {
                            var g = section.modelData.group
                            var open = surf.expandedGroups.slice()
                            var at = open.indexOf(g)
                            if (at >= 0) open.splice(at, 1); else open.push(g)
                            surf.expandedGroups = open
                        }
                        width: parent.width; spacing: 0
                        Text {
                            text: modelData.group.toUpperCase() + "  ·  " + modelData.items.length
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                            font.weight: Font.DemiBold; font.letterSpacing: 1.6
                        }
                        Item { width: 1; height: 16 }
                        Grid {
                            id: secGrid
                            width: parent.width; columns: 6; columnSpacing: 22; rowSpacing: 26
                            property real cellW: (width - columnSpacing * (columns - 1)) / columns
                            readonly property var visibleItems: section.expanded ? section.modelData.items : section.modelData.items.slice(0, secGrid.columns)
                            property int currentIndex: visibleItems.length > 0 ? 0 : -1
                            focusPolicy: visibleItems.length > 0 ? Qt.TabFocus : Qt.NoFocus
                            Keys.onPressed: (event) => resultKeys.handle(event)
                            KeyboardCollectionController {
                                id: resultKeys; view: secGrid; orientation: "grid"; columns: secGrid.columns; count: secGrid.visibleItems.length
                                positionIndexFn: function(index) { surf.keepSearchDelegateVisible(resultRepeater.itemAt(index)) }
                                onActivated: (index) => surf.openItem(secGrid.visibleItems[index].data)
                            }
                            Repeater {
                                id: resultRepeater
                                model: secGrid.visibleItems
                                delegate: Column {
                                    id: resultCard
                                    required property var modelData
                                    required property int index
                                    readonly property bool keyboardSelected: secGrid.activeFocus && secGrid.currentIndex === index
                                    // Same keyed precedent as topCard above (DiscoverBrowser.qml:727): id-or-title, never index.
                                    objectName: modelData && modelData.title
                                                ? ("searchResult_" + String(modelData.data && modelData.data.id ? modelData.data.id : modelData.title))
                                                : ""
                                    width: secGrid.cellW; spacing: 9
                                    Rectangle {
                                        width: parent.width; height: width * 1.5; radius: 8; clip: true; color: "#14131a"
                                        border.width: resultCard.keyboardSelected ? 2 : 0; border.color: theme.gold
                                        Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                            fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                        scale: cardMa.containsMouse ? 1.03 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        MouseArea { id: cardMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: surf.openItem(modelData.data) }
                                    }
                                    Text { width: parent.width; text: modelData.title ? modelData.title : ""
                                        color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                        elide: Text.ElideRight; maximumLineCount: 1 }
                                    Text { width: parent.width; text: modelData.subtitle ? modelData.subtitle : ""
                                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                        elide: Text.ElideRight; maximumLineCount: 1 }
                                }
                            }
                        }
                        // See more / Show less — only when the group actually overflows one row
                        Item { visible: seeMorePill.visible; width: 1; height: 18 }
                        Rectangle {
                            id: seeMorePill
                            visible: modelData.items.length > secGrid.columns
                            height: 34; radius: 17
                            width: seeMoreRow.implicitWidth + 30
                            color: seeMoreMa.containsMouse ? theme.glassHi : theme.glassTint
                            border.width: 1; border.color: seeMoreMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.edge
                            Row {
                                id: seeMoreRow; anchors.centerIn: parent; spacing: 7
                                Text {
                                    text: section.expanded ? "Show less"
                                        : "See more · " + (section.modelData.items.length - secGrid.columns)
                                    color: seeMoreMa.containsMouse ? theme.gold : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: section.expanded ? "▴" : "▾"
                                    color: seeMoreMa.containsMouse ? theme.gold : theme.inkDimmer
                                    font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: seeMoreMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: section.toggleExpanded()
                            }
                            KeyboardAction { anchors.fill: parent; pointerEnabled: false
                                accessibleName: section.expanded ? "Show less" : "See more"; focusRadius: 17
                                onTriggered: section.toggleExpanded()
                            }
                        }
                        Item { width: 1; height: 34 }
                    }
                }

                Text {
                    visible: surf.searchError.length > 0 && !surf.searching
                    text: surf.searchError
                    color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: 18; topPadding: 30
                }
                Text {
                    visible: surf.searched && surf.results.length === 0 && !surf.searching && surf.searchError.length === 0
                    text: "No results"; color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: 20; topPadding: 30
                }
            }
        }
    }

    ScrollGlide { id: searchGlide; flick: scroll }
    KeyboardScrollController {
        id: searchScrollKeys; flick: scroll; glide: searchGlide; arrowScrolling: false
    }

    // window chrome (fullscreen rule removed 2026-07-20): the canonical
    // minimize · fullscreen-toggle · power cluster every page carries.
    Row {
        id: winChrome
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: chromeMinMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: chromeMinMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: surf.minimizeRequested()
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Minimize"; focusRadius: 4
                onTriggered: surf.minimizeRequested() }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Toggle fullscreen"; focusRadius: 4
                onTriggered: surf.fullscreenRequested() }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: surf.fullscreenRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: chromePowMa.containsMouse ? 1.0 : 0.72
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Close Colosseum"; focusRadius: 4
                onTriggered: { surf.cancelAllRequests(); surf.closeRequested() } }
            MouseArea {
                id: chromePowMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { surf.cancelAllRequests(); surf.closeRequested() }
            }
        }
    }
}
