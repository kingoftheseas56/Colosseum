// BiblioSearch — Biblio's search overlay. Owner: A2. Harbor-adapted (function/feel, not a clone):
// the field leads (no chrome bar, Esc closes), the best hit blooms into a Top Match "mini dust-jacket",
// the rest fall into a cover grid, and an empty state offers Recent · Jump to · Browse-a-genre.
// Live as you type (180ms). Apple Books is the source; clicking any result opens its BiblioBook detail.

import QtQuick
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi
import "BiblioSeriesFold.js" as Fold

Item {
    id: search
    property Item backdrop
    property var results: []
    property bool searching: false
    property bool searched: false
    property var recent: []                          // in-session recent queries

    // Fold the results (minus the Top Match) so a searched series shows as ONE stack, not loose books.
    readonly property var foldedRest: Fold.foldSeries(search.results.length > 1 ? search.results.slice(1) : [], SeriesIndex)

    signal backRequested()
    signal homeRequested()
    signal bookRequested(var book)
    signal seriesRequested(string series, string author)
    signal minimizeRequested()
    signal closeRequested()

    readonly property bool isEmpty: queryInput.text.trim().length === 0

    Theme { id: theme }
    MouseArea { anchors.fill: parent }
    Component.onCompleted: queryInput.forceActiveFocus()

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // ── behaviour ──
    // Race guard: a result is applied ONLY if its query still matches the current input — slow
    // FictionDB replies for half-typed queries can land last and would otherwise clobber the answer.
    function runAppleSearch() {
        var q = queryInput.text.trim()
        if (q.length < 2) { search.results = []; search.searched = false; return }
        search.searching = true
        BiblioApi.search(q, function(books) {
            if (q !== queryInput.text.trim()) return        // stale — input moved on
            search.results = books
            search.searching = false
            search.searched = true
            search.recordRecent(q)
        })
    }
    function recordRecent(q) {
        var lower = q.toLowerCase()
        var list = search.recent.filter(function(r) { return r.toLowerCase() !== lower })
        list.unshift(q)
        search.recent = list.slice(0, 6)
    }
    function fillAndSearch(q) { queryInput.text = q; runAppleSearch() }
    function openTop() { if (search.results.length > 0) search.bookRequested(search.results[0]) }

    Timer { id: debounce; interval: 200; onTriggered: search.runAppleSearch() }

    Shortcut { sequences: ["Return", "Enter"]; onActivated: search.openTop() }

    // ── the search field (leads the surface) ──
    Rectangle {
        id: field
        x: theme.margin; y: 44
        width: search.width - theme.margin * 2; height: 62; radius: 16
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
            anchors.left: glass.right; anchors.leftMargin: 15
            anchors.right: rightCluster.left; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            color: theme.ink; font.family: theme.display; font.pixelSize: 22
            clip: true; focus: true; selectByMouse: true
            onTextChanged: debounce.restart()
            // the field holds keyboard focus, so close on Esc here rather than relying on the
            // window shortcut reaching past it
            Keys.onEscapePressed: search.backRequested()
        }
        Text {
            visible: queryInput.text.length === 0
            anchors.left: glass.right; anchors.leftMargin: 15
            anchors.verticalCenter: parent.verticalCenter
            text: "Search books…"; color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 22
        }
        Row {
            id: rightCluster
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Rectangle {                              // clear
                visible: queryInput.text.length > 0
                width: 26; height: 26; radius: 13
                anchors.verticalCenter: parent.verticalCenter
                color: clearMa.containsMouse ? Qt.rgba(1,1,1,0.14) : Qt.rgba(1,1,1,0.06)
                Text { anchors.centerIn: parent; text: "✕"; color: theme.inkDimmer; font.pixelSize: 12 }
                MouseArea { id: clearMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { queryInput.text = ""; queryInput.forceActiveFocus() } }
            }
            Rectangle {                              // Esc hint
                width: escTxt.width + 16; height: 24; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"; border.width: 1; border.color: theme.edge
                Text { id: escTxt; anchors.centerIn: parent; text: "Esc"; color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.5 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: search.backRequested() }
            }
        }
    }

    // ── content ──
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
                    text: "RECENT"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: search.recent.length > 0; width: 1; height: 16 }
                Flow {
                    visible: search.recent.length > 0
                    width: parent.width; spacing: 10
                    Repeater {
                        model: search.recent
                        delegate: Rectangle {
                            required property var modelData
                            height: 40; radius: 999; width: rcRow.width + 34
                            color: rcMa.containsMouse ? Qt.rgba(1,1,1,0.12) : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Row { id: rcRow; anchors.centerIn: parent; spacing: 9
                                Text { text: modelData; color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "✕"; color: theme.inkDimmer; font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter } }
                            MouseArea { id: rcMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: search.fillAndSearch(modelData) }
                        }
                    }
                }
                Item { visible: search.recent.length > 0; width: 1; height: 34 }

                Text { text: "JUMP TO"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                Item { width: 1; height: 16 }
                Row {
                    spacing: 12
                    Repeater {
                        model: [ { t: "Home", a: "home" }, { t: "Top 10", a: "back" }, { t: "Genres", a: "back" } ]
                        delegate: Rectangle {
                            required property var modelData
                            height: 48; radius: 999; width: jLbl.width + 44
                            color: jMa.containsMouse ? Qt.rgba(1,1,1,0.12) : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Text { id: jLbl; anchors.centerIn: parent; text: modelData.t; color: theme.ink
                                font.family: theme.display; font.pixelSize: 15 }
                            MouseArea { id: jMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { if (modelData.a === "home") search.homeRequested(); else search.backRequested() } }
                        }
                    }
                }
                Item { width: 1; height: 34 }

                Text { text: "BROWSE A GENRE"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                Item { width: 1; height: 16 }
                Flow {
                    width: parent.width; spacing: 10
                    Repeater {
                        model: [
                            { t: "Fiction", q: "fiction" }, { t: "Mystery & Thriller", q: "thriller" },
                            { t: "Romance", q: "romance" }, { t: "Sci-Fi & Fantasy", q: "science fiction" },
                            { t: "Biography", q: "biography" }, { t: "History", q: "history" },
                            { t: "Nonfiction", q: "nonfiction" }, { t: "Young Adult", q: "young adult" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            height: 44; radius: 999; width: gLbl.width + 36
                            color: gMa.containsMouse ? Qt.rgba(1,1,1,0.10) : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Text { id: gLbl; anchors.centerIn: parent; text: modelData.t
                                color: gMa.containsMouse ? theme.ink : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13 }
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
                    text: "TOP MATCH"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: search.results.length > 0; width: 1; height: 14 }
                Rectangle {
                    id: topCard
                    visible: search.results.length > 0
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
                            color: theme.ink; font.family: theme.display; font.pixelSize: 32
                            elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { visible: text.length > 0; width: parent.width
                            text: topCard.m && topCard.m.tagline ? "“" + topCard.m.tagline + "”" : ""
                            color: theme.ink; opacity: 0.9; font.family: theme.display; font.italic: true
                            font.pixelSize: 18; elide: Text.ElideRight; maximumLineCount: 1 }
                        Text { width: parent.width
                            text: (topCard.m && topCard.m.genreLine ? topCard.m.genreLine : "").toUpperCase()
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                            font.letterSpacing: 1.0; elide: Text.ElideRight; maximumLineCount: 1 }
                    }
                    Rectangle {
                        id: openBtn
                        anchors.right: parent.right; anchors.rightMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 116; height: 48; radius: 12; color: theme.gold
                        Row { anchors.centerIn: parent; spacing: 7
                            Text { text: "Open"; color: "#241a05"; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { text: "›"; color: "#241a05"; font.pixelSize: 16 } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (topCard.m) search.bookRequested(topCard.m) }
                    }
                    MouseArea { anchors.fill: parent; z: -1; onClicked: if (topCard.m) search.bookRequested(topCard.m) }
                }
                Item { visible: search.results.length > 0; width: 1; height: 38 }

                // Results grid (the rest), folded: series collapse into one stack, standalones stay single
                Text {
                    visible: search.searched
                    text: "BOOKS  ·  " + search.results.length + " FOUND"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { visible: search.searched; width: 1; height: 16 }
                Grid {
                    id: bookGrid
                    width: parent.width; columns: 6
                    columnSpacing: 22; rowSpacing: 26
                    property real cellW: (width - columnSpacing * (columns - 1)) / columns
                    Repeater {
                        model: search.foldedRest
                        delegate: Column {
                            required property var modelData
                            readonly property bool isSeries: modelData.kind === "series"
                            width: bookGrid.cellW; spacing: 9
                            Item {
                                width: parent.width; height: width * 1.5
                                // stacked cards behind = "this is a series, not a single book"
                                Rectangle { visible: isSeries; width: parent.width; height: parent.height; radius: 8
                                    x: 6; y: 8; rotation: 1.4; color: "#15131c"; border.width: 1; border.color: Qt.rgba(1,1,1,0.05) }
                                Rectangle { visible: isSeries; width: parent.width; height: parent.height; radius: 8
                                    x: 3; y: 4; rotation: -1.0; color: "#1b1822"; border.width: 1; border.color: Qt.rgba(1,1,1,0.06) }
                                Rectangle {
                                    width: parent.width; height: parent.height; radius: 8; clip: true; color: "#14131a"
                                    Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                    Text { visible: !modelData.cover && !isSeries; anchors.centerIn: parent; width: parent.width - 18
                                        text: "Cover art not available"; color: theme.inkDimmer; font.family: theme.display
                                        font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                                    Rectangle {                       // gold "N books" count chip (series only)
                                        visible: isSeries
                                        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8
                                        radius: 7; height: 22; width: scnt.implicitWidth + 14
                                        color: Qt.rgba(0.04, 0.035, 0.028, 0.80)
                                        border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
                                        Row { id: scnt; anchors.centerIn: parent; spacing: 4
                                            Text { text: modelData.count > 0 ? modelData.count : "•"
                                                color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold }
                                            Text { text: "books"; color: theme.gold; font.family: theme.ui; font.pixelSize: 9 } }
                                    }
                                    scale: cardMa.containsMouse ? 1.03 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    MouseArea { id: cardMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: isSeries ? search.seriesRequested(modelData.series, modelData.author)
                                                            : search.bookRequested(modelData) }
                                }
                            }
                            Text { width: parent.width; text: isSeries ? (modelData.series || "") : (modelData.title || "")
                                color: theme.ink; font.family: isSeries ? theme.display : theme.ui; font.pixelSize: 13
                                elide: Text.ElideRight; maximumLineCount: 1 }
                            Text { width: parent.width; text: isSeries ? ("Series · " + (modelData.author || "")) : (modelData.author || "")
                                color: isSeries ? theme.inkDim : theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                elide: Text.ElideRight; maximumLineCount: 1 }
                        }
                    }
                }
                // no-results
                Text {
                    visible: search.searched && search.results.length === 0 && !search.searching
                    text: "No books found"; color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: 20; topPadding: 30
                }
            }
        }
    }
}
