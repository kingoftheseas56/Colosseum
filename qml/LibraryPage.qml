// LibraryPage — Theatre's Library (Stage 2), the fifth tab. The "Shelf Ledger" design
// (agents/colosseum-library-tab-mock.html, approved by Hemanth): the ledger leads (every
// fragment IS a filter), the quiet bar (sort / type / airing pills), and a life-marked
// poster wall with a floating ⋮ menu. Renders as tab content inside TheatreWorld (no
// standalone chrome). All derivations are LibraryApi (headless-proven); the page only
// paints and wires. Context properties (Collection/Progress/LocalDownloads) are typeof-
// guarded so it constructs offscreen for the harness.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "LibraryApi.js" as Api
import "CollectionBackfill.js" as CB

Item {
    id: root

    // Retained Theatre worlds keep this page instance for filter/scroll state, but the saved
    // library model is only needed while its tab is active. A hidden page therefore keeps its
    // shell state without synchronously scanning Collection/Progress/LocalDownloads.
    property bool active: true

    // ── page state (the ledger + quiet bar drive these) ──
    property string sortMode: "lastWatched"   // lastWatched | added | az | year
    property string typeFilter: ""            // "" | movie | series
    property string airingFilter: ""          // "" | ongoing | ended
    property string stateFilter: ""           // "" | inProgress | unwatched | watched | newEpisodes | downloaded
    property string query: ""

    // ── reactive data (recompute on Collection / Progress change) ──
    property int collRev: (typeof Collection !== "undefined" ? Collection.revision : 0)
    property int progRev: (typeof Progress !== "undefined" ? Progress.revision : 0)
    property var allRows: computeRows(collRev, progRev, active)
    property var counts: Api.ledgerCounts(allRows)
    property var visibleRows: Api.sortRows(
        Api.applyFilters(allRows, { stateFilter: stateFilter, typeFilter: typeFilter,
                                    airingFilter: airingFilter, query: query }),
        sortMode)

    // ── the floating ⋮ menu (rendered at root level; the wall GridView clips) ──
    property var menuRow: null
    property string menuRowId: ""
    property real menuX: 0
    property real menuY: 0
    property Item menuFocusReturn: null

    // card menu → owning surface (spec §4.2)
    signal resumeRequested(var entry)
    signal detailRequested(var entry)
    signal dismissRequested(var entry)
    signal markWatchedRequested(var entry, bool watched)
    signal removeRequested(var entry)

    Theme { id: theme }

    function computeRows(cr, pr, isActive) {
        if (!isActive || typeof Collection === "undefined" || typeof Progress === "undefined") return []
        var entries = Collection.items("theatre")
        var plist = Progress.recent("video", 0)
        return Api.buildRows(entries, plist,
            function (id) { return Progress.watchedMark(id) },
            function (entry) {
                if (!entry || entry.type === "series") return false
                if (typeof ProfileHistory === "undefined" || !ProfileHistory) return false
                return ProfileHistory.completed("movie", String(entry.id))
            },
            downloadedIds(), Date.now())
    }

    // Collection ids with >=1 episode/movie on disk, via CollectionBackfill's id mapping.
    function downloadedIds() {
        if (typeof LocalDownloads === "undefined") return []
        var out = []
        var series = LocalDownloads.series("theatre") || []
        for (var i = 0; i < series.length; i++) {
            var s = series[i]
            var items = LocalDownloads.items("theatre", s.key)
            var e = CB.entryForTheatreSeries(s, items)
            if (e && e.id) out.push(String(e.id))
        }
        return out
    }

    function toggleStateFilter(key) {
        stateFilter = (key === "" || stateFilter === key) ? "" : key
    }
    function openMenu(row, x, y, invoker) {
        if (menuRowId === row.entry.id) { closeMenu(true); return }
        menuRow = row; menuRowId = row.entry.id; menuX = x; menuY = y
        menuFocusReturn = invoker || wall; menuPanel.currentIndex = 0
        Qt.callLater(function() { menuPanel.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeMenu(restoreFocus) {
        const target = menuFocusReturn
        menuFocusReturn = null; menuRow = null; menuRowId = ""
        if (restoreFocus !== false && target)
            Qt.callLater(function() { if (target.visible && target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : "" }
    function menuActions() {
        var out = ["resume", "detail"]
        if (menuRow && menuRow.progress > 0) out.push("dismiss")
        out.push("watch"); out.push("remove")
        return out
    }
    function menuActionIndex(action) { return menuActions().indexOf(action) }
    function triggerMenuAction(action) {
        if (!menuRow) return
        if (action === "resume") resumeRequested(menuRow.entry)
        else if (action === "detail") detailRequested(menuRow.entry)
        else if (action === "dismiss") dismissRequested(menuRow.entry)
        else if (action === "watch") markWatchedRequested(menuRow.entry, menuRow.state !== "watched")
        else if (action === "remove") removeRequested(menuRow.entry)
        closeMenu(true)
    }
    function triggerMenuIndex(index) { const a = menuActions(); if (index >= 0 && index < a.length) triggerMenuAction(a[index]) }

    // one tinted Lucide glyph (mirrors PlayerIcon's Image+MultiEffect technique)
    component Glyph: Item {
        property string file: ""
        property color ink: theme.inkDim
        Image {
            id: g; anchors.fill: parent
            source: Qt.resolvedUrl("../assets/icons/lucide/" + parent.file + ".svg")
            sourceSize.width: Math.max(2, Math.round(width * 2))
            sourceSize.height: Math.max(2, Math.round(height * 2))
            fillMode: Image.PreserveAspectFit; smooth: true; cache: true; visible: false
        }
        MultiEffect { anchors.fill: g; source: g; colorization: 1.0; colorizationColor: parent.ink }
    }

    // ── header: the shelf ledger leads (the active "Library" tab already names it) + search ──
    Item {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: Math.max(64, theme.margin); anchors.rightMargin: Math.max(64, theme.margin)
        height: 76; z: 20

        // THE SHELF LEDGER — every fragment is the filter itself (gold + underline when active)
        Flow {
            anchors.left: parent.left; anchors.right: searchField.left; anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            LedgerFrag { label: "saved"; value: root.counts.saved; fragKey: "" }
            LedgerDot {}
            LedgerFrag { label: "in progress"; value: root.counts.inProgress; fragKey: "inProgress" }
            LedgerDot {}
            LedgerFrag { label: "unwatched"; value: root.counts.unwatched; fragKey: "unwatched" }
            LedgerDot {}
            LedgerFrag { label: "watched"; value: root.counts.watched; fragKey: "watched" }
            LedgerDot {}
            LedgerFrag { label: "new episodes"; value: root.counts.newEpisodes; fragKey: "newEpisodes" }
            LedgerDot {}
            LedgerFrag { label: "downloaded"; value: root.counts.downloaded; fragKey: "downloaded" }
        }

        TextField {
            id: searchField
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 250; height: 38; leftPadding: 16; rightPadding: 16
            placeholderText: "Search your library"; placeholderTextColor: theme.inkDimmer
            color: theme.ink; selectionColor: theme.gold; selectedTextColor: "#111111"
            font.family: theme.ui; font.pixelSize: 14
            background: Rectangle {
                radius: 19; color: Qt.rgba(0.03, 0.04, 0.06, 0.62)
                border.width: 1; border.color: searchField.activeFocus ? theme.gold : theme.edge
            }
            onTextEdited: root.query = text
            Keys.onEscapePressed: { text = ""; root.query = "" }
        }
    }

    // ledger fragment: bold number + label, gold+underline when its filter is active
    component LedgerFrag: Item {
        property string label: ""
        property int value: 0
        property string fragKey: ""
        readonly property bool active: fragKey === "" ? root.stateFilter === "" : root.stateFilter === fragKey
        implicitWidth: fragRow.implicitWidth
        implicitHeight: 22
        Row {
            id: fragRow; anchors.verticalCenter: parent.verticalCenter; spacing: 0
            Text {
                text: parent.parent.value; color: parent.parent.active ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Text {
                text: " " + parent.parent.label
                color: parent.parent.active ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
            }
        }
        Rectangle {
            anchors.left: fragRow.left; anchors.right: fragRow.right
            anchors.top: fragRow.bottom; anchors.topMargin: 1
            height: 1; visible: parent.active || fragHover.hovered
            color: parent.active ? theme.gold : theme.inkDimmer
        }
        HoverHandler { id: fragHover }
        MouseArea {
            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleStateFilter(parent.fragKey)
        }
        KeyboardAction { anchors.fill: parent; pointerEnabled: false
            accessibleName: parent.label; focusRadius: 4; onTriggered: root.toggleStateFilter(parent.fragKey) }
    }
    component LedgerDot: Item {
        implicitWidth: 12; implicitHeight: 22
        Rectangle { anchors.centerIn: parent; width: 3; height: 3; radius: 1.5; color: theme.inkDimmer }
    }

    // ── the quiet filter bar: sorts | type | airing ──
    Item {
        id: filterBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: header.bottom
        height: 54; z: 19

        component FilterPill: Rectangle {
            property string label: ""
            property bool active: false
            signal picked()
            implicitWidth: pillText.implicitWidth + 30; height: 34; radius: 14
            color: active ? theme.gold : (pillHover.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
            border.width: active ? 0 : 1; border.color: Qt.rgba(1, 1, 1, 0.14)
            Behavior on color { ColorAnimation { duration: 130 } }
            Text {
                id: pillText; anchors.centerIn: parent; text: parent.label
                color: parent.active ? "#17120a" : theme.ink
                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            }
            HoverHandler { id: pillHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: parent.label; focusRadius: parent.radius
                onTriggered: parent.picked() }
        }

        Row {
            anchors.left: parent.left; anchors.leftMargin: Math.max(64, theme.margin)
            anchors.verticalCenter: parent.verticalCenter; spacing: 8

            Repeater {
                model: [ { key: "lastWatched", label: "Last watched" }, { key: "added", label: "Recently added" },
                         { key: "az", label: "A–Z" }, { key: "year", label: "Year" } ]
                delegate: FilterPill {
                    required property var modelData
                    label: modelData.label; active: root.sortMode === modelData.key
                    onPicked: root.sortMode = modelData.key
                }
            }
            Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: theme.edge }
            Repeater {
                model: [ { key: "", label: "All" }, { key: "movie", label: "Movies" }, { key: "series", label: "Series" } ]
                delegate: FilterPill {
                    required property var modelData
                    label: modelData.label; active: root.typeFilter === modelData.key
                    onPicked: root.typeFilter = modelData.key
                }
            }
            Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: theme.edge }
            Repeater {
                model: [ { key: "", label: "All" }, { key: "ongoing", label: "Ongoing" }, { key: "ended", label: "Ended" } ]
                delegate: FilterPill {
                    required property var modelData
                    label: modelData.label; active: root.airingFilter === modelData.key
                    onPicked: root.airingFilter = modelData.key
                }
            }
        }
    }

    // ── the wall ──
    GridView {
        id: wall
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: filterBar.bottom; anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(48, theme.margin); anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 22; anchors.bottomMargin: 18
        clip: true; boundsBehavior: Flickable.StopAtBounds
        model: root.visibleRows
        // fixed gallery poster size (148×222) matches ContinueTile / discover shelves — the
        // deliberate, consistent card size that reads as one family with the rest of the app.
        readonly property int posterW: 148
        readonly property int posterH: 222   // posterW × gallery.posterRatio (1.5)
        readonly property int columnCount: Math.max(2, Math.floor((width + 20) / (posterW + 20)))
        cellWidth: Math.floor(width / columnCount)
        cellHeight: posterH + 60
        cacheBuffer: cellHeight * 2
        ScrollBar.vertical: HouseScrollBar { flick: wall }
        onContentYChanged: if (root.menuRow) root.closeMenu(false)
        focusPolicy: root.visibleRows.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => wallKeys.handle(event)
        KeyboardCollectionController {
            id: wallKeys; view: wall; orientation: "grid"; columns: Math.max(1, wall.columnCount)
            count: root.visibleRows.length; contextEnabled: true
            onActivated: (index) => root.detailRequested(root.visibleRows[index].entry)
            onContextRequested: (index) => {
                const card = wall.currentItem
                if (!card) return
                const pt = card.mapToItem(root, card.width, 0)
                root.openMenu(card.modelData, pt.x, pt.y, wall)
            }
        }

        delegate: Item {
            id: card
            required property var modelData
            required property int index
            width: wall.posterW; height: wall.cellHeight - 18
            x: (wall.cellWidth - width) / 2
            readonly property real coverH: wall.posterH
            readonly property bool hovered: cardHover.hovered || root.menuRowId === card.modelData.entry.id
            readonly property bool keyboardSelected: wall.activeFocus && wall.currentIndex === card.index
            readonly property bool watched: modelData.state === "watched"

            // ── two offset depth plates behind the poster (ContinueTile world grammar) ──
            // plate 1: subtle drop, darkens on hover
            Rectangle {
                x: 0; y: 3; width: card.width; height: card.coverH; radius: 13
                color: Qt.rgba(0, 0, 0, card.hovered ? 0.42 : 0.28)
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            // plate 2: deeper, slides down on hover (the lift)
            Rectangle {
                x: -2; y: card.hovered ? 11 : 7
                width: card.width + 4; height: card.coverH; radius: 15
                color: Qt.rgba(0, 0, 0, card.hovered ? 0.20 : 0.10)
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 200 } }
            }

            // ── the poster (cover art + scrim + badges + hairline), one rounded mask ──
            Item {
                id: worldContent
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: card.coverH
                layer.enabled: true
                layer.effect: MultiEffect { maskEnabled: true; maskSource: worldMask; maskThresholdMin: 0.5 }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#33405c" }
                        GradientStop { position: 1; color: "#12161f" }
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 24
                        text: card.modelData.entry.title || "Untitled"
                        color: Qt.rgba(1, 1, 1, 0.8); font.family: theme.display
                        font.pixelSize: 17; font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        maximumLineCount: 4; elide: Text.ElideRight
                    }
                }
                Image {
                    anchors.fill: parent
                    source: card.modelData.entry.cover || card.modelData.entry.art || ""
                    fillMode: Image.PreserveAspectCrop; verticalAlignment: Image.AlignTop
                    asynchronous: true; cache: true
                    opacity: status === Image.Ready ? (card.watched ? 0.45 : 1) : 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                }

                // new-episode "+N" (Library page ONLY) — top-left
                Rectangle {
                    visible: card.modelData.newCount > 0
                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                    radius: 7; height: 20; width: newLabel.implicitWidth + 16; color: theme.gold
                    Text {
                        id: newLabel; anchors.centerIn: parent; text: "+" + card.modelData.newCount
                        color: "#17120a"; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                    }
                }
                // downloaded ↓ chip — bottom-right
                Rectangle {
                    visible: card.modelData.downloaded
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
                    width: 20; height: 20; radius: 6; color: Qt.rgba(0.04, 0.04, 0.055, 0.72)
                    border.width: 1; border.color: theme.edge
                    Glyph { anchors.centerIn: parent; width: 11; height: 11; file: "download"; ink: theme.inkDim }
                }
                // watched ✓ — top-right (hidden while hovering so the ⋮ can take the corner)
                Rectangle {
                    visible: card.watched && !card.hovered
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                    width: 22; height: 22; radius: 11; color: Qt.rgba(0.04, 0.04, 0.055, 0.72)
                    border.width: 1; border.color: theme.edge
                    Text { anchors.centerIn: parent; text: "✓"; color: theme.inkDim; font.pixelSize: 12 }
                }
                // in-progress hairline — cover base
                Rectangle {
                    visible: card.modelData.state === "progress" && card.modelData.progress > 0
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 3; color: Qt.rgba(1, 1, 1, 0.16)
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, card.modelData.progress)); color: theme.gold
                    }
                }
            }
            // stable rounded mask source (no animation) — a texture provider, not drawn directly
            Item {
                id: worldMask
                visible: false
                anchors.fill: worldContent
                layer.enabled: true
                Rectangle { anchors.fill: parent; radius: 12; color: "black" }
            }

            // ── the hover film + gold edge (ContinueTile shared grammar) ──
            Rectangle {
                anchors.fill: worldContent
                radius: 12; color: "transparent"
                border.width: card.keyboardSelected ? 3 : 2
                border.color: card.keyboardSelected ? Qt.rgba(240/255,196/255,74/255,0.78) : (card.hovered ? theme.gold : "transparent")
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }
            Rectangle {
                anchors.fill: worldContent
                radius: 12
                color: card.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            // ⋮ button — top-right, fades in on hover (ContinueTile shared grammar)
            Rectangle {
                id: dots
                anchors.right: worldContent.right; anchors.top: worldContent.top; anchors.margins: 8
                width: 26; height: 26; radius: 8; color: Qt.rgba(0.04, 0.04, 0.075, 0.82)
                border.width: 1; border.color: root.menuRowId === card.modelData.entry.id ? theme.gold : theme.edge
                opacity: card.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
                Text { anchors.centerIn: parent; text: "⋮"; color: theme.ink; font.pixelSize: 15 }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        wall.currentIndex = card.index
                        wall.forceActiveFocus(Qt.MouseFocusReason)
                        var pt = dots.mapToItem(root, dots.width, dots.height)
                        root.openMenu(card.modelData, pt.x, pt.y, wall)
                    }
                }
            }

            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: worldContent.bottom; anchors.topMargin: 9
                text: card.modelData.entry.title || "Untitled"
                color: card.watched ? theme.inkDim : theme.ink
                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 1
            }
            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: worldContent.bottom; anchors.topMargin: 30
                text: {
                    var parts = []
                    if (card.modelData.year > 0) parts.push(card.modelData.year)
                    parts.push(card.modelData.isSeries ? "Series" : "Movie")
                    if (card.modelData.isSeries && card.modelData.airing) parts.push(root.cap(card.modelData.airing))
                    return parts.join(" · ")
                }
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight
            }

            HoverHandler { id: cardHover }
            MouseArea {
                anchors.fill: worldContent; cursorShape: Qt.PointingHandCursor
                onClicked: root.detailRequested(card.modelData.entry)
            }
        }
    }
    ScrollGlide { flick: wall }

    // ── empty states ──
    Column {
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Your library is empty"
            color: theme.ink; font.family: theme.display; font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Press play on anything, or tap + Library — it lands here."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
        }
    }
    Column {
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing matches these filters"
            color: theme.ink; font.family: theme.display; font.pixelSize: 30
        }
    }

    // ── the floating ⋮ menu (root level — the wall clips) ──
    MouseArea {
        anchors.fill: parent; z: 55; visible: root.menuRow !== null
        onClicked: root.closeMenu(true)
    }
    Rectangle {
        id: menuPanel
        z: 56; visible: root.menuRow !== null
        width: 200; radius: 13
        color: Qt.rgba(0.043, 0.047, 0.075, 0.98); border.width: 1; border.color: theme.edge
        implicitHeight: menuCol.implicitHeight + 12
        height: implicitHeight
        property int currentIndex: 0
        focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => {
            const n = root.menuActions().length
            if (n <= 0) return
            var delta = 0
            if (event.key === Qt.Key_Down || (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier))) delta = 1
            else if (event.key === Qt.Key_Up || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) delta = -1
            if (delta !== 0) { currentIndex = (currentIndex + delta + n) % n; event.accepted = true; return }
            if (event.key === Qt.Key_Home) { currentIndex = 0; event.accepted = true; return }
            if (event.key === Qt.Key_End) { currentIndex = n - 1; event.accepted = true; return }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { root.triggerMenuIndex(currentIndex); event.accepted = true; return }
            if (event.key === Qt.Key_Escape) { root.closeMenu(true); event.accepted = true }
        }
        x: Math.max(8, Math.min(root.menuX - width, root.width - width - 8))
        y: Math.max(8, Math.min(root.menuY, root.height - height - 8))

        MouseArea { anchors.fill: parent }   // click-swallower body

        component MenuItem: Item {
            property string label: ""
            property string file: ""
            property string actionId: ""
            property bool warn: false
            readonly property bool keyboardSelected: menuPanel.activeFocus && root.menuActionIndex(actionId) === menuPanel.currentIndex
            signal picked()
            width: parent ? parent.width : 0; height: 38
            Rectangle {
                anchors.fill: parent; anchors.margins: 2; radius: 9
                color: miHover.hovered || parent.keyboardSelected ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
                border.width: parent.keyboardSelected ? 2 : 0; border.color: theme.gold
            }
            Row {
                anchors.left: parent.left; anchors.leftMargin: 11
                anchors.verticalCenter: parent.verticalCenter; spacing: 10
                Glyph {
                    width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter
                    file: parent.parent.file; ink: parent.parent.warn ? "#e08a8a" : theme.inkDim
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter; text: parent.parent.label
                    color: parent.parent.warn ? "#e08a8a" : theme.ink
                    font.family: theme.ui; font.pixelSize: 13
                }
            }
            HoverHandler { id: miHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
        }

        Column {
            id: menuCol
            anchors.top: parent.top; anchors.topMargin: 6
            anchors.left: parent.left; anchors.right: parent.right
            MenuItem {
                actionId: "resume"; file: "play"
                label: {
                    if (!root.menuRow) return "Play"
                    var resume = root.menuRow.progress > 0 || root.menuRow.state === "progress"
                    if (resume) return "Resume" + (root.menuRow.sub ? " " + root.menuRow.sub : "")
                    return "Play"
                }
                onPicked: root.triggerMenuAction("resume")
            }
            MenuItem {
                actionId: "detail"; file: "info"; label: "Details"
                onPicked: root.triggerMenuAction("detail")
            }
            Rectangle { width: parent.width - 16; x: 8; height: 1; color: theme.edge }
            MenuItem {
                actionId: "dismiss"; file: "rotate-ccw"; label: "Dismiss progress"
                visible: root.menuRow && root.menuRow.progress > 0
                height: visible ? 38 : 0
                onPicked: root.triggerMenuAction("dismiss")
            }
            MenuItem {
                actionId: "watch"; file: "circle-check"
                label: (root.menuRow && root.menuRow.state === "watched") ? "Mark unwatched" : "Mark watched"
                onPicked: root.triggerMenuAction("watch")
            }
            Rectangle { width: parent.width - 16; x: 8; height: 1; color: theme.edge }
            MenuItem {
                actionId: "remove"; file: "x"; label: "Remove from Library"; warn: true
                onPicked: root.triggerMenuAction("remove")
            }
        }
    }

}
