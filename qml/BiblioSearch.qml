// BiblioSearch — Biblio's search overlay. Owner: A2. Harbor-adapted (function/feel, not a clone):
// the field leads (no chrome bar, Esc closes), the best hit blooms into a Top Match "mini dust-jacket",
// the rest fall into a cover grid, and an empty state offers Recent · Jump to · Browse-a-genre.
// Live as you type (180ms). Apple Books is the source; clicking any result opens its BiblioBook detail.

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi

Item {
    id: search
    objectName: "biblioSearchSurface"
    property Item backdrop
    property var results: []
    property bool searching: false
    property bool searched: false
    property var audioResults: []                     // audiobooks column (Apple media=audiobook)
    property bool audioSearching: false
    property bool booksExpanded: false                // one row until "See more" (like Tankoban/Theatre)
    property bool audioExpanded: false
    property var recent: []                          // in-session recent queries
    property string lastDispatchedQuery: ""
    property var historyStore: typeof SearchHistory !== "undefined" ? SearchHistory : null
    property var searchDispatcher: BiblioApi.search
    property var _bookCancel: null
    property var _audioCancel: null
    property int _bookGeneration: 0
    property int _audioGeneration: 0
    property var audioSearchDispatcher: BiblioApi.searchAudiobooks
    property string bookSearchError: ""
    property string audioSearchError: ""
    readonly property string searchError: bookSearchError.length ? bookSearchError : audioSearchError
    readonly property int bookResultCount: results.length
    readonly property int audioResultCount: audioResults.length
    readonly property int recentCount: recent.length
    readonly property bool showingProviderError: searchError.length > 0
        && !searching && !audioSearching
    readonly property bool showingNoResults: searched && results.length === 0
        && audioResults.length === 0 && !searching && !audioSearching
        && searchError.length === 0

    // Everything after the Top Match falls into the cover grid.
    readonly property var restResults: search.results.length > 1 ? search.results.slice(1) : []

    signal backRequested()
    signal homeRequested()
    signal bookRequested(var book)
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    readonly property bool isEmpty: queryInput.text.trim().length === 0
    readonly property bool phoneNarrow: win.androidHost && win.width < 840

    Theme { id: theme }

    // A phone result is one full-width row, shared by Books and Audiobooks.
    component PhoneBookResult: Rectangle {
        id: phoneRow
        property var entry: ({})
        property bool isAudio: false
        property bool keyboardSelected: false
        signal activated()
        height: Math.max(116, phoneDetails.implicitHeight + 24)
        radius: 12; color: theme.glassTint
        border.width: keyboardSelected ? 2 : 1
        border.color: keyboardSelected ? theme.gold : theme.edge
        Row {
            anchors.fill: parent; anchors.margins: 12; spacing: 14
            Rectangle {
                width: 66; height: 92; radius: 7; clip: true; color: "#14131a"
                Image { anchors.fill: parent; source: phoneRow.entry.cover || ""
                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                Rectangle {
                    visible: phoneRow.isAudio
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 3
                    width: 22; height: 22; radius: 11; color: Qt.rgba(0,0,0,0.65)
                    Image { anchors.centerIn: parent; width: 12; height: 12
                        source: "../assets/icons/music.svg" }
                }
            }
            Column {
                id: phoneDetails
                width: Math.max(0, parent.width - 80); spacing: 7
                Text { width: parent.width; text: phoneRow.entry.title || ""
                    color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(16); wrapMode: Text.Wrap }
                Text { width: parent.width; text: phoneRow.entry.author || ""
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(13); wrapMode: Text.Wrap }
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: phoneRow.activated() }
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => { if (!event.accepted) searchScrollKeys.handle(event) }
    Keys.onReleased: (event) => searchScrollKeys.handleRelease(event)
    MouseArea { anchors.fill: parent }
    Component.onCompleted: {
        search.loadRecent()
        queryInput.forceActiveFocus()
    }
    Component.onDestruction: {
        search.cancelAllRequests()
        search.commitCurrentQuery()
    }
    onHistoryStoreChanged: search.loadRecent()
    Connections {
        target: search.historyStore
        function onChanged(scope) {
            if (scope === "biblio")
                search.loadRecent()
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // ── behaviour ──
    // Race guard: a result is applied ONLY if its query still matches the current input — slow
    // replies for half-typed queries can land last and would otherwise clobber the answer.
    function cancelHandle(handle) {
        if (!handle) return
        if (typeof handle === "function") { handle(); return }
        if (handle.cancel && typeof handle.cancel === "function") handle.cancel()
    }
    function cancelBooks() {
        search._bookGeneration++
        search.cancelHandle(search._bookCancel)
        search._bookCancel = null
        search.searching = false
    }
    function cancelAudio() {
        search._audioGeneration++
        search.cancelHandle(search._audioCancel)
        search._audioCancel = null
        search.audioSearching = false
    }
    function cancelAllRequests() {
        search.cancelBooks()
        search.cancelAudio()
    }
    function runAppleSearch() {
        var q = queryInput.text.trim()
        search.cancelAllRequests()
        if (q.length < 2) {
            search.lastDispatchedQuery = ""
            search.results = []; search.audioResults = []; search.searched = false
            search.searching = false; search.audioSearching = false
            search.bookSearchError = ""; search.audioSearchError = ""
            return
        }
        search.lastDispatchedQuery = q
        search.bookSearchError = ""; search.audioSearchError = ""   // fresh dispatch clears stale lane errors
        search.booksExpanded = false; search.audioExpanded = false   // fresh query → collapse both to one row
        search.searching = true
        var bookGeneration = search._bookGeneration
        search._bookCancel = search.searchDispatcher(q, function(books, error) {
            if (bookGeneration !== search._bookGeneration || q !== queryInput.text.trim()) return
            search.results = books || []
            search.bookSearchError = error || ""
            search.searching = false
            search.searched = true
        })
        // audiobooks column — same query, same stale-guard, own lane (and own error truth:
        // one failed lane must not erase the healthy lane's rows)
        search.audioSearching = true
        var audioGeneration = search._audioGeneration
        search._audioCancel = search.audioSearchDispatcher(q, function(abs, error) {
            if (audioGeneration !== search._audioGeneration || q !== queryInput.text.trim()) return
            search.audioResults = abs || []
            search.audioSearchError = error || ""
            search.audioSearching = false
        })
    }
    function loadRecent() { search.recent = search.historyStore ? search.historyStore.list("biblio") : [] }
    function commitCurrentQuery() {
        var q = queryInput.text.trim()
        if (search.historyStore && q.length >= 2 && q === search.lastDispatchedQuery)
            search.recent = search.historyStore.record("biblio", q)
    }
    function fillAndSearch(q) { queryInput.text = q; runAppleSearch(); commitCurrentQuery() }
    function openBook(book) { search.cancelAllRequests(); search.commitCurrentQuery(); search.bookRequested(book) }
    function openTop() { if (search.results.length > 0) search.openBook(search.results[0]) }
    function removeRecent(q) { if (search.historyStore) search.recent = search.historyStore.remove("biblio", q) }
    function keepSearchDelegateVisible(item) {
        if (!item || !scroll) return
        const p = item.mapToItem(scroll.contentItem, 0, 0)
        if (p.y < scroll.contentY + 12) scroll.contentY = Math.max(0, p.y - 12)
        else if (p.y + item.height > scroll.contentY + scroll.height - 12)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height), p.y + item.height - scroll.height + 12)
    }

    Timer { id: debounce; interval: 200; onTriggered: search.runAppleSearch() }

    Shortcut { sequences: ["Return", "Enter"]; onActivated: { debounce.stop(); search.runAppleSearch(); search.commitCurrentQuery(); search.openTop() } }

    // ── visible exit (audit fix: Esc was the only door out) + the search field ──
    BackAction {
        id: searchBack
        x: search.phoneNarrow ? 16 : theme.margin
        y: search.phoneNarrow ? 14 : undefined
        anchors.verticalCenter: search.phoneNarrow ? undefined : field.verticalCenter
        hoverColor: "#ffffff"   // Biblio world rule: white hover, not gold
        onTriggered: { search.cancelAllRequests(); search.commitCurrentQuery(); search.backRequested() }
    }
    Rectangle {
        id: field
        // The field stops short of the window-chrome cluster instead of running under it: both were
        // anchored to the same right margin, and their vertical bands overlapped by 12px, so the
        // minimize/fullscreen/power buttons sat on the field's top-right corner. Derived from the
        // chrome row's own width so it can never drift apart again. (Same fix as SearchSurface.)
        x: search.phoneNarrow ? 16 : searchBack.x + searchBack.width + 20
        y: search.phoneNarrow ? 72 : 44
        width: search.phoneNarrow ? Math.max(0, search.width - 32)
                                  : search.width - x - theme.margin - winChrome.width - 24
        height: search.phoneNarrow ? 56 : 72; radius: 16
        color: Qt.rgba(0, 0, 0, 0.30); border.width: 1; border.color: theme.edge

        Canvas {
            id: glass; width: search.phoneNarrow ? 19 : 21; height: width
            x: search.phoneNarrow ? 15 : 22; anchors.verticalCenter: parent.verticalCenter
            onPaint: {
                var ctx = getContext("2d"); ctx.reset()
                ctx.strokeStyle = "#9a99a5"; ctx.lineWidth = 1.7; ctx.lineCap = "round"
                ctx.beginPath(); ctx.arc(9, 9, 6.3, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(13.6, 13.6); ctx.lineTo(19.5, 19.5); ctx.stroke()
            }
        }
        TextInput {
            id: queryInput
            objectName: "biblioSearchInput"
            anchors.left: glass.right; anchors.leftMargin: search.phoneNarrow ? 10 : 15
            anchors.right: rightCluster.left; anchors.rightMargin: search.phoneNarrow ? 4 : 14
            anchors.verticalCenter: parent.verticalCenter
            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(search.phoneNarrow ? 18 : 22)
            clip: true; focus: true; selectByMouse: true
            onTextChanged: { search.cancelAllRequests(); debounce.restart() }
            // the field holds keyboard focus, so close on Esc here rather than relying on the
            // window shortcut reaching past it
            Keys.onEscapePressed: { search.cancelAllRequests(); search.commitCurrentQuery(); search.backRequested() }
        }
        Text {
            visible: queryInput.text.length === 0
            anchors.left: queryInput.left
            width: queryInput.width; clip: true
            anchors.verticalCenter: parent.verticalCenter
            text: "Search books…"; color: theme.inkDimmer; font.family: theme.display
            font.pixelSize: theme.fontSize(search.phoneNarrow ? 18 : 22)
        }
        Row {
            id: rightCluster
            anchors.right: parent.right; anchors.rightMargin: search.phoneNarrow ? 6 : 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: search.phoneNarrow ? 0 : 12
            Rectangle {                              // clear
                visible: queryInput.text.length > 0
                width: search.phoneNarrow ? 44 : 26; height: width; radius: height / 2
                anchors.verticalCenter: parent.verticalCenter
                color: clearMa.containsMouse ? Qt.rgba(1,1,1,0.14) : Qt.rgba(1,1,1,0.06)
                Text { anchors.centerIn: parent; text: Qt.platform.os === "android" ? "×" : "✕"; color: theme.inkDimmer; font.pixelSize: theme.fontSize(12) }
                MouseArea { id: clearMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { queryInput.text = ""; queryInput.forceActiveFocus() } }
                KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Clear search"; focusRadius: 13
                    onTriggered: { queryInput.text = ""; queryInput.forceActiveFocus() } }
            }
            Rectangle {                              // Esc hint
                visible: !search.phoneNarrow
                width: escTxt.width + 16; height: 24; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"; border.width: 1; border.color: theme.edge
                Text { id: escTxt; anchors.centerIn: parent; text: "Esc"; color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: theme.fontSize(11); font.letterSpacing: 0.5 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { search.cancelAllRequests(); search.backRequested() } }
                KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Exit search"; focusRadius: 6
                    onTriggered: { search.cancelAllRequests(); search.backRequested() } }
            }
        }
    }

    // ── content ──
    Flickable {
        id: scroll
        anchors.top: field.bottom; anchors.topMargin: search.phoneNarrow ? 20 : 30
        anchors.left: parent.left; anchors.leftMargin: search.phoneNarrow ? 16 : theme.margin
        anchors.right: parent.right; anchors.rightMargin: search.phoneNarrow ? 16 : theme.margin
        anchors.bottom: parent.bottom; anchors.bottomMargin: 8
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 30
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: scroll }

        Column {
            id: content
            width: scroll.width
            spacing: 0

            // ───────── EMPTY STATE ─────────
            Column {
                width: parent.width; spacing: 0
                visible: search.isEmpty

                // Recent (only if there's history this session)
                Text {
                    visible: search.recent.length > 0
                    text: "RECENT"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: search.recent.length > 0; width: 1; height: 16 }
                Flow {
                    id: recentFlow
                    visible: search.recent.length > 0
                    width: parent.width; spacing: 10
                    property int currentIndex: search.recent.length > 0 ? 0 : -1
                    focusPolicy: search.recent.length > 0 ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => recentKeys.handle(event)
                    KeyboardCollectionController {
                        id: recentKeys; view: recentFlow; orientation: "horizontal"
                        count: search.recent.length; contextEnabled: true
                        onActivated: (index) => search.fillAndSearch(search.recent[index])
                        onContextRequested: (index) => search.removeRecent(search.recent[index])
                    }
                    Repeater {
                        id: recentRepeater
                        model: search.recent
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool keyboardSelected: recentFlow.activeFocus && recentFlow.currentIndex === index
                            height: search.phoneNarrow ? Math.max(48, rcLabel.implicitHeight + 16) : 40
                            radius: 999; width: search.phoneNarrow ? recentFlow.width : rcLabel.implicitWidth + 57
                            color: rcMa.containsMouse || keyboardSelected ? Qt.rgba(1,1,1,0.12) : theme.glassTint
                            border.width: keyboardSelected ? 2 : 1
                            border.color: keyboardSelected ? theme.gold : theme.edge
                            Text { id: rcLabel; anchors.left: parent.left; anchors.leftMargin: 17
                                anchors.right: removeRecentButton.left; anchors.rightMargin: 5
                                anchors.verticalCenter: parent.verticalCenter; text: modelData; color: theme.ink
                                font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                                wrapMode: search.phoneNarrow ? Text.Wrap : Text.NoWrap
                                elide: search.phoneNarrow ? Text.ElideNone : Text.ElideRight }
                            MouseArea { id: rcMa; anchors.left: parent.left; anchors.right: removeRecentButton.left
                                anchors.top: parent.top; anchors.bottom: parent.bottom; hoverEnabled: true
                                objectName: "biblioRecentBody"; cursorShape: Qt.PointingHandCursor
                                onClicked: search.fillAndSearch(modelData) }
                            Rectangle { id: removeRecentButton; width: search.phoneNarrow ? 44 : 28; height: parent.height; radius: height / 2
                                anchors.right: parent.right; anchors.rightMargin: 3; anchors.verticalCenter: parent.verticalCenter
                                color: removeRecentMa.containsMouse ? Qt.rgba(1,1,1,0.14) : "transparent"
                                Text { anchors.centerIn: parent; text: Qt.platform.os === "android" ? "×" : "✕"; color: theme.inkDimmer; font.pixelSize: theme.fontSize(11) }
                                MouseArea { id: removeRecentMa; anchors.fill: parent; hoverEnabled: true
                                    objectName: "biblioRecentRemove"; cursorShape: Qt.PointingHandCursor
                                    onClicked: search.removeRecent(modelData) } }
                        }
                    }
                }
                Item { visible: search.recent.length > 0; width: 1; height: 34 }

                Text { text: "JUMP TO"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                Item { width: 1; height: 16 }
                Flow {
                    id: jumpRow
                    width: parent.width; spacing: 12
                    property int currentIndex: 0
                    focusPolicy: Qt.TabFocus
                    Keys.onPressed: (event) => jumpKeys.handle(event)
                    KeyboardCollectionController {
                        id: jumpKeys; view: jumpRow; orientation: "horizontal"; count: 3
                        onActivated: (index) => {
                            const row = jumpRepeater.itemAt(index)
                            if (!row) return
                            if (row.modelData.a === "home") search.homeRequested(); else search.backRequested()
                        }
                    }
                    Repeater {
                        id: jumpRepeater
                        model: [ { t: "Home", a: "home" }, { t: "Top 10", a: "back" }, { t: "Genres", a: "back" } ]
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool keyboardSelected: jumpRow.activeFocus && jumpRow.currentIndex === index
                            height: 48; radius: 999; width: search.phoneNarrow ? jumpRow.width : jLbl.width + 44
                            color: jMa.containsMouse || keyboardSelected ? Qt.rgba(1,1,1,0.12) : theme.glassTint
                            border.width: keyboardSelected ? 2 : 1; border.color: keyboardSelected ? theme.gold : theme.edge
                            Text { id: jLbl; anchors.centerIn: parent; text: modelData.t; color: theme.ink
                                font.family: theme.display; font.pixelSize: theme.fontSize(15) }
                            MouseArea { id: jMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { if (modelData.a === "home") search.homeRequested(); else search.backRequested() } }
                        }
                    }
                }
                Item { width: 1; height: 34 }

                Text { text: "BROWSE A GENRE"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                Item { width: 1; height: 16 }
                Flow {
                    id: genreFlow
                    width: parent.width; spacing: 10
                    property int currentIndex: genreRepeater.count > 0 ? 0 : -1
                    focusPolicy: genreRepeater.count > 0 ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => genreKeys.handle(event)
                    KeyboardCollectionController {
                        id: genreKeys; view: genreFlow; orientation: "grid"; columns: 4
                        count: genreRepeater.count
                        onActivated: (index) => { const row = genreRepeater.itemAt(index); if (row) search.fillAndSearch(row.modelData.q) }
                    }
                    Repeater {
                        id: genreRepeater
                        model: [
                            { t: "Fiction", q: "fiction" }, { t: "Mystery & Thriller", q: "thriller" },
                            { t: "Romance", q: "romance" }, { t: "Sci-Fi & Fantasy", q: "science fiction" },
                            { t: "Biography", q: "biography" }, { t: "History", q: "history" },
                            { t: "Nonfiction", q: "nonfiction" }, { t: "Young Adult", q: "young adult" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool keyboardSelected: genreFlow.activeFocus && genreFlow.currentIndex === index
                            height: 44; radius: 999; width: search.phoneNarrow ? genreFlow.width : gLbl.width + 36
                            color: gMa.containsMouse || keyboardSelected ? Qt.rgba(1,1,1,0.10) : theme.glassTint
                            border.width: keyboardSelected ? 2 : 1; border.color: keyboardSelected ? theme.gold : theme.edge
                            Text { id: gLbl; anchors.centerIn: parent; text: modelData.t
                                color: gMa.containsMouse ? theme.ink : theme.inkDim
                                font.family: theme.ui; font.pixelSize: theme.fontSize(13) }
                            MouseArea { id: gMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: search.fillAndSearch(modelData.q) }
                        }
                    }
                }
            }

            // ───────── RESULTS STATE ─────────
            Column {
                width: parent.width; spacing: 0
                visible: !search.isEmpty

                // Top Match — a mini dust-jacket
                Text {
                    visible: search.results.length > 0
                    text: "TOP MATCH"; color: theme.gold; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: search.results.length > 0; width: 1; height: 14 }
                Rectangle {
                    id: topCard
                    visible: search.results.length > 0 && !search.phoneNarrow
                    property var m: search.results.length > 0 ? search.results[0] : ({})
                    width: parent.width; height: 196; radius: 18
                    color: theme.glassTint; border.width: 1; border.color: theme.edge

                    Item {                                   // cover-object
                        id: tmCover
                        anchors.left: parent.left; anchors.leftMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 100; height: 150
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
                            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(32)
                            elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { visible: text.length > 0; width: parent.width
                            text: topCard.m && topCard.m.tagline ? "“" + topCard.m.tagline + "”" : ""
                            color: theme.ink; opacity: 0.9; font.family: theme.display; font.italic: true
                            font.pixelSize: theme.fontSize(18); elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { width: parent.width
                            text: (topCard.m && topCard.m.genreLine ? topCard.m.genreLine : "").toUpperCase()
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                            font.letterSpacing: 1.0; elide: Text.ElideRight; maximumLineCount: 1 }
                    }
                    Rectangle {
                        id: openBtn
                        anchors.right: parent.right; anchors.rightMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 116; height: 48; radius: 12; color: theme.gold
                        Row { anchors.centerIn: parent; spacing: 7
                            Text { text: "Open"; color: "#241a05"; font.family: theme.ui; font.pixelSize: theme.fontSize(15); font.weight: Font.DemiBold }
                            Text { text: "›"; color: "#241a05"; font.pixelSize: 16 } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (topCard.m) search.openBook(topCard.m) }
                    }
                    MouseArea { anchors.fill: parent; z: -1; onClicked: if (topCard.m) search.openBook(topCard.m) }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Open top match"; focusRadius: 18
                        onTriggered: if (topCard.m) search.openBook(topCard.m) }
                }
                Rectangle {
                    id: phoneTopCard
                    visible: search.phoneNarrow && search.results.length > 0
                    width: parent.width; height: phoneTopContents.implicitHeight + 28
                    radius: 16; color: theme.glassTint
                    border.width: 1; border.color: theme.edge
                    Column {
                        id: phoneTopContents
                        x: 14; y: 14; width: parent.width - 28; spacing: 14
                        Item {
                            width: parent.width
                            height: Math.max(126, phoneTopDetails.implicitHeight)
                            Row {
                                anchors.fill: parent; spacing: 14
                                Rectangle {
                                    width: 84; height: 126; radius: 8; clip: true; color: "#14131a"
                                    Image { anchors.fill: parent; source: topCard.m.cover || ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true }
                                }
                                Column {
                                id: phoneTopDetails
                                width: Math.max(0, parent.width - 98); spacing: 8
                                Text { width: parent.width; text: topCard.m.title || ""
                                    color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(23)
                                    wrapMode: Text.Wrap }
                                Text { visible: text.length > 0; width: parent.width
                                    text: topCard.m.tagline ? "“" + topCard.m.tagline + "”" : ""
                                    color: theme.ink; font.family: theme.display; font.italic: true
                                    font.pixelSize: theme.fontSize(16); wrapMode: Text.Wrap }
                                Text { width: parent.width
                                    text: (topCard.m.genreLine || "").toUpperCase()
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                                    wrapMode: Text.Wrap }
                            }
                            }
                            MouseArea { anchors.fill: parent; onClicked: search.openTop() }
                        }
                        Rectangle {
                            width: parent.width; height: 48; radius: 12; color: theme.gold
                            Text { anchors.centerIn: parent; text: "Open  ›"
                                color: "#241a05"; font.family: theme.ui; font.pixelSize: theme.fontSize(15)
                                font.weight: Font.DemiBold }
                            MouseArea { anchors.fill: parent; onClicked: search.openTop() }
                        }
                    }
                }
                Item { visible: search.results.length > 0; width: 1; height: 38 }

                // Results grid (the rest)
                Text {
                    visible: search.searched
                    text: "BOOKS  ·  " + search.results.length + " FOUND"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { visible: search.searched; width: 1; height: 16 }
                Grid {
                    id: bookGrid
                    width: parent.width; columns: search.phoneNarrow ? 1 : 6
                    columnSpacing: search.phoneNarrow ? 0 : 22; rowSpacing: search.phoneNarrow ? 12 : 26
                    property real cellW: (width - columnSpacing * (columns - 1)) / columns
                    readonly property var visibleItems: search.booksExpanded ? search.restResults : search.restResults.slice(0, bookGrid.columns)
                    property int currentIndex: visibleItems.length > 0 ? 0 : -1
                    focusPolicy: visibleItems.length > 0 ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => bookKeys.handle(event)
                    KeyboardCollectionController {
                        id: bookKeys; view: bookGrid; orientation: "grid"; columns: bookGrid.columns; count: bookGrid.visibleItems.length
                        positionIndexFn: function(index) { search.keepSearchDelegateVisible(bookRepeater.itemAt(index)) }
                        onActivated: (index) => search.openBook(bookGrid.visibleItems[index])
                    }
                    Repeater {
                        id: bookRepeater
                        model: bookGrid.visibleItems
                        delegate: Column {
                            required property var modelData
                            required property int index
                            readonly property bool keyboardSelected: bookGrid.activeFocus && bookGrid.currentIndex === index
                            width: bookGrid.cellW; spacing: search.phoneNarrow ? 0 : 9
                            PhoneBookResult {
                                visible: search.phoneNarrow; width: parent.width
                                entry: modelData; keyboardSelected: bookGrid.activeFocus && bookGrid.currentIndex === index
                                onActivated: search.openBook(modelData)
                            }
                            Item {
                                visible: !search.phoneNarrow
                                width: parent.width; height: width * 1.5
                                Rectangle {
                                    width: parent.width; height: parent.height; radius: 8; clip: true; color: "#14131a"
                                    border.width: parent.parent.keyboardSelected ? 2 : 0
                                    border.color: theme.gold
                                    Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                    Text { visible: !modelData.cover; anchors.centerIn: parent; width: parent.width - 18
                                        text: "Cover art not available"; color: theme.inkDimmer; font.family: theme.display
                                        font.pixelSize: theme.fontSize(12); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                                    scale: cardMa.containsMouse ? 1.03 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    MouseArea { id: cardMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: search.openBook(modelData) }
                                }
                            }
                            Text { visible: !search.phoneNarrow; width: parent.width; text: modelData.title || ""
                                color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                                elide: Text.ElideRight; maximumLineCount: 1 }
                            Text { visible: !search.phoneNarrow; width: parent.width; text: modelData.author || ""
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                                elide: Text.ElideRight; maximumLineCount: 1 }
                        }
                    }
                }
                // Books cap: one row until "See more" — so audiobooks aren't buried under 20 books
                Item { visible: !search.booksExpanded && search.restResults.length > bookGrid.columns; width: 1; height: 18 }
                SeeMorePill {
                    extra: search.restResults.length - bookGrid.columns
                    expanded: search.booksExpanded
                    onToggled: search.booksExpanded = !search.booksExpanded
                }

                // ───────── AUDIOBOOKS column (paired to their book on click) ─────────
                Item { visible: search.audioResults.length > 0; width: 1; height: 44 }
                Row {
                    visible: search.audioResults.length > 0
                    spacing: 9
                    Image { source: "../assets/icons/music.svg"; width: 14; height: 14
                        anchors.verticalCenter: parent.verticalCenter; opacity: 0.7 }
                    Text {
                        text: "AUDIOBOOKS  ·  " + search.audioResults.length + " FOUND"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                        font.weight: Font.DemiBold; font.letterSpacing: 1.6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Item { visible: search.audioResults.length > 0; width: 1; height: 16 }
                Grid {
                    id: audioGrid
                    width: parent.width; columns: search.phoneNarrow ? 1 : 6
                    columnSpacing: search.phoneNarrow ? 0 : 22; rowSpacing: search.phoneNarrow ? 12 : 26
                    property real cellW: (width - columnSpacing * (columns - 1)) / columns
                    readonly property var visibleItems: search.audioExpanded ? search.audioResults : search.audioResults.slice(0, audioGrid.columns)
                    property int currentIndex: visibleItems.length > 0 ? 0 : -1
                    focusPolicy: visibleItems.length > 0 ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => audioKeys.handle(event)
                    KeyboardCollectionController {
                        id: audioKeys; view: audioGrid; orientation: "grid"; columns: audioGrid.columns; count: audioGrid.visibleItems.length
                        positionIndexFn: function(index) { search.keepSearchDelegateVisible(audioRepeater.itemAt(index)) }
                        onActivated: (index) => search.openBook(audioGrid.visibleItems[index])
                    }
                    Repeater {
                        id: audioRepeater
                        model: audioGrid.visibleItems
                        delegate: Column {
                            required property var modelData
                            required property int index
                            readonly property bool keyboardSelected: audioGrid.activeFocus && audioGrid.currentIndex === index
                            width: audioGrid.cellW; spacing: search.phoneNarrow ? 0 : 9
                            PhoneBookResult {
                                visible: search.phoneNarrow; width: parent.width
                                entry: modelData; isAudio: true
                                keyboardSelected: audioGrid.activeFocus && audioGrid.currentIndex === index
                                onActivated: search.openBook(modelData)
                            }
                            Item {
                                visible: !search.phoneNarrow
                                width: parent.width; height: width * 1.5
                                Rectangle {
                                    width: parent.width; height: parent.height; radius: 8; clip: true; color: "#14131a"
                                    border.width: parent.parent.keyboardSelected ? 2 : 0
                                    border.color: theme.gold
                                    Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                    Text { visible: !modelData.cover; anchors.centerIn: parent; width: parent.width - 18
                                        text: "Cover art not available"; color: theme.inkDimmer; font.family: theme.display
                                        font.pixelSize: theme.fontSize(12); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                                    // medium badge — headphones/music glyph marks the audiobook lane
                                    Rectangle {
                                        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 6
                                        width: 26; height: 26; radius: 13; color: Qt.rgba(0,0,0,0.55)
                                        Image { anchors.centerIn: parent; width: 13; height: 13
                                            source: "../assets/icons/music.svg"; opacity: 0.95 }
                                    }
                                    scale: abMa.containsMouse ? 1.03 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    MouseArea { id: abMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: search.openBook(modelData) }
                                }
                            }
                            Text { visible: !search.phoneNarrow; width: parent.width; text: modelData.title || ""
                                color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                                elide: Text.ElideRight; maximumLineCount: 1 }
                            Text { visible: !search.phoneNarrow; width: parent.width; text: modelData.author || ""
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                                elide: Text.ElideRight; maximumLineCount: 1 }
                        }
                    }
                }
                Item { visible: !search.audioExpanded && search.audioResults.length > audioGrid.columns; width: 1; height: 18 }
                SeeMorePill {
                    extra: search.audioResults.length - audioGrid.columns
                    expanded: search.audioExpanded
                    onToggled: search.audioExpanded = !search.audioExpanded
                }

                Text {
                    visible: search.searchError.length > 0 && !search.searching && !search.audioSearching
                    text: search.searchError
                    width: parent.width; wrapMode: Text.Wrap
                    color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: theme.fontSize(18); topPadding: 30
                }
                // no-results — only once BOTH lanes have returned empty (else it flashes
                // "No books found" while the slower audiobook lane is still loading), and
                // only when neither lane actually FAILED (a provider outage is not "no books")
                Text {
                    visible: search.searched && search.results.length === 0 && search.audioResults.length === 0
                             && !search.searching && !search.audioSearching && search.searchError.length === 0
                    text: "No books found"; color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: theme.fontSize(20); topPadding: 30
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
        visible: !search.phoneNarrow
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
                onClicked: search.minimizeRequested()
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Minimize"; focusRadius: 4
                onTriggered: search.minimizeRequested() }
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
                onTriggered: search.fullscreenRequested() }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: search.fullscreenRequested()
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
                onTriggered: { search.cancelAllRequests(); search.closeRequested() } }
            MouseArea {
                id: chromePowMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { search.cancelAllRequests(); search.closeRequested() }
            }
        }
    }
}
