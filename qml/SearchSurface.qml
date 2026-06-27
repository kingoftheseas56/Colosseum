// SearchSurface — the reusable search overlay for the non-Biblio worlds (Tankoban, Theatre). Same
// Harbor-adapted shape as Biblio's search (field leads, Top Match hero, results grid, recent), but
// source-agnostic: it asks WorldSearch.searchFor(searchMode, …) and emits itemRequested(data) for the
// host to route to that world's detail. (Biblio keeps its own richer BiblioSearch with series + libgen.)

import QtQuick
import QtQuick.Effects
import "WorldSearch.js" as WorldSearch

Item {
    id: surf
    property Item backdrop
    property string searchMode: ""                   // "Tankoban" | "Theatre"
    property string placeholder: "Search…"
    property string primaryLabel: "Open"
    property var results: []
    property bool searching: false
    property bool searched: false
    property var recent: []

    signal backRequested()
    signal itemRequested(var data)
    signal minimizeRequested()
    signal closeRequested()

    readonly property bool isEmpty: queryInput.text.trim().length === 0

    // Biblio's empty search offers a "browse" set so the surface is never a bare void before you type.
    // These backends search by TITLE, so the chips are marquee titles (one click → a real result set),
    // not genre words that would weakly title-match.
    readonly property var popular: searchMode === "Theatre"
        ? ["Dune", "Oppenheimer", "Interstellar", "The Batman", "Breaking Bad", "Severance", "Game of Thrones", "Arcane"]
        : ["One Piece", "Berserk", "Chainsaw Man", "Jujutsu Kaisen", "Vinland Saga", "Vagabond", "Monster", "Solo Leveling"]
    readonly property string popularLabel: searchMode === "Theatre" ? "POPULAR ON THEATRE" : "POPULAR MANGA"

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

    // race guard: apply a result only if its query still matches the field
    function runSearch() {
        var q = queryInput.text.trim()
        if (q.length < 2) { surf.results = []; surf.searched = false; return }
        surf.searching = true
        WorldSearch.searchFor(surf.searchMode, q, function(items) {
            if (q !== queryInput.text.trim()) return
            surf.results = items
            surf.searching = false
            surf.searched = true
            surf.recordRecent(q)
        })
    }
    function recordRecent(q) {
        var lower = q.toLowerCase()
        var list = surf.recent.filter(function(r) { return r.toLowerCase() !== lower })
        list.unshift(q)
        surf.recent = list.slice(0, 6)
    }
    function fillAndSearch(q) { queryInput.text = q; runSearch() }
    function openTop() { if (surf.results.length > 0) surf.itemRequested(surf.results[0].data) }

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

    Shortcut { sequences: ["Return", "Enter"]; onActivated: surf.openTop() }

    // ── the search field (leads the surface) ──
    Rectangle {
        id: field
        x: theme.margin; y: 44
        width: surf.width - theme.margin * 2; height: 62; radius: 16
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
            Keys.onEscapePressed: surf.backRequested()
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
            }
            Rectangle {
                width: escTxt.width + 16; height: 24; radius: 6
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"; border.width: 1; border.color: theme.edge
                Text { id: escTxt; anchors.centerIn: parent; text: "Esc"; color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.5 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: surf.backRequested() }
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

        Column {
            id: content
            width: scroll.width
            spacing: 0

            // empty state — Recent (this session) + a "Popular" browse set (Biblio-parity)
            Column {
                width: parent.width; spacing: 0
                visible: surf.isEmpty

                // RECENT (only if there's history this session)
                Text {
                    visible: surf.recent.length > 0
                    text: "RECENT"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { visible: surf.recent.length > 0; width: 1; height: 16 }
                Flow {
                    visible: surf.recent.length > 0
                    width: parent.width; spacing: 10
                    Repeater {
                        model: surf.recent
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
                                onClicked: surf.fillAndSearch(modelData) }
                        }
                    }
                }
                Item { visible: surf.recent.length > 0; width: 1; height: 34 }

                // POPULAR — marquee titles; one click runs a real search (Biblio's "browse a genre" twin)
                Text {
                    text: surf.popularLabel; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { width: 1; height: 16 }
                Flow {
                    width: parent.width; spacing: 10
                    Repeater {
                        model: surf.popular
                        delegate: Rectangle {
                            required property var modelData
                            height: 44; radius: 999; width: pLbl.width + 36
                            color: pMa.containsMouse ? Qt.rgba(1,1,1,0.10) : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Text { id: pLbl; anchors.centerIn: parent; text: modelData
                                color: pMa.containsMouse ? theme.ink : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13 }
                            MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: surf.fillAndSearch(modelData) }
                        }
                    }
                }
            }

            // results state
            Column {
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
                            onClicked: if (topCard.m) surf.itemRequested(topCard.m.data) }
                    }
                    MouseArea { anchors.fill: parent; z: -1; onClicked: if (topCard.m) surf.itemRequested(topCard.m.data) }
                }
                Item { visible: surf.results.length > 0; width: 1; height: 38 }

                // grouped sections (Movies / Series / Manga)
                Repeater {
                    model: surf.groupedResults()
                    delegate: Column {
                        required property var modelData          // { group, items }
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
                            Repeater {
                                model: modelData.items
                                delegate: Column {
                                    required property var modelData
                                    width: secGrid.cellW; spacing: 9
                                    Rectangle {
                                        width: parent.width; height: width * 1.5; radius: 8; clip: true; color: "#14131a"
                                        Image { anchors.fill: parent; source: modelData.cover ? modelData.cover : ""
                                            fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true }
                                        scale: cardMa.containsMouse ? 1.03 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        MouseArea { id: cardMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: surf.itemRequested(modelData.data) }
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
                        Item { width: 1; height: 34 }
                    }
                }

                Text {
                    visible: surf.searched && surf.results.length === 0 && !surf.searching
                    text: "No results"; color: theme.inkDimmer; font.family: theme.display
                    font.pixelSize: 20; topPadding: 30
                }
            }
        }
    }
}
