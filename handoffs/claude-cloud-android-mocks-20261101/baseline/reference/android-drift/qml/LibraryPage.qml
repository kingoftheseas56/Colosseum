// LibraryPage â€” Theatre's Library (Stage 2), the fifth tab. The "Shelf Ledger" design
// (agents/colosseum-library-tab-mock.html, approved by Hemanth): the ledger leads (every
// fragment IS a filter), the quiet bar (sort / type / airing pills), and a life-marked
// poster wall with a floating â‹® menu. Renders as tab content inside TheatreWorld (no
// standalone chrome). All derivations are LibraryApi (headless-proven); the page only
// paints and wires. Context properties (Collection/Progress/LocalDownloads) are typeof-
// guarded so it constructs offscreen for the harness.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Effects
import "LibraryApi.js" as Api
import "CollectionBackfill.js" as CB

Item {
    id: root
    AdaptiveLayout { id: layout; page: root; viewportHeight: root.pageFlick ? root.pageFlick.height : root.height }
    readonly property bool phoneLayout: layout.phoneLayout
    readonly property bool portraitPhoneLayout: layout.touch && layout.phone && layout.portrait

    // Retained Theatre worlds keep this page instance for filter/scroll state, but the saved
    // library model is only needed while its tab is active. A hidden page therefore keeps its
    // shell state without synchronously scanning Collection/Progress/LocalDownloads.
    property bool active: true
    property Flickable pageFlick: null
    readonly property bool pageFlow: pageFlick !== null
    readonly property rect menuViewport: {
        if (!root.phoneLayout || !root.pageFlick)
            return Qt.rect(0, 0, root.width, root.height)
        // The page can be taller than its clipped WorldPage scroll viewport.
        const origin = (root.pageFlick.contentY, root.y, root.pageFlick.mapToItem(root, 0, 0))
        const top = Math.max(0, origin.y)
        const bottom = Math.min(root.height, origin.y + root.pageFlick.height)
        return Qt.rect(0, top, root.width, Math.max(0, bottom - top))
    }
    readonly property real flowHeight: wallHost.y + wallHost.flowContentHeight + 18
    NumberAnimation {
        id: pageParkAnim
        target: root.pageFlick; property: "contentY"
        duration: 220; easing.type: Easing.OutCubic
    }
    function revealCell(index) {
        if (!pageFlow || index < 0) return
        var row = Math.floor(index / Math.max(1, wall.columnCount))
        var rowTop = wallHost.mapToItem(pageFlick.contentItem, 0, 0).y + row * wall.cellHeight
        var target = Math.max(0, Math.min(pageFlick.contentHeight - pageFlick.height,
                                          rowTop - 64 - 14))
        if (Math.abs(target - pageFlick.contentY) < 1) return
        pageParkAnim.stop()
        pageParkAnim.from = pageFlick.contentY
        pageParkAnim.to = target
        pageParkAnim.start()
    }

    // â”€â”€ page state (the ledger + quiet bar drive these) â”€â”€
    property string sortMode: "lastWatched"   // lastWatched | added | az | year
    property string typeFilter: ""            // "" | movie | series
    property string airingFilter: ""          // "" | ongoing | ended
    property string stateFilter: ""           // "" | inProgress | unwatched | watched | newEpisodes | downloaded
    property string query: ""

    // â”€â”€ reactive data (recompute on Collection / Progress change) â”€â”€
    property int collRev: (typeof Collection !== "undefined" ? Collection.revision : 0)
    property int progRev: (typeof Progress !== "undefined" ? Progress.revision : 0)
    property var allRows: computeRows(collRev, progRev, active)
    property var counts: Api.ledgerCounts(allRows)
    property var visibleRows: Api.sortRows(
        Api.applyFilters(allRows, { stateFilter: stateFilter, typeFilter: typeFilter,
                                    airingFilter: airingFilter, query: query }),
        sortMode)

    onSortModeChanged: if (wallKeys) wallKeys.invalidateLane()
    onTypeFilterChanged: if (wallKeys) wallKeys.invalidateLane()
    onAiringFilterChanged: if (wallKeys) wallKeys.invalidateLane()
    onStateFilterChanged: if (wallKeys) wallKeys.invalidateLane()
    onQueryChanged: if (wallKeys) wallKeys.invalidateLane()

    // â”€â”€ the floating â‹® menu (rendered at root level; the wall GridView clips) â”€â”€
    property var menuRow: null
    property string menuRowId: ""
    property real menuX: 0
    property real menuY: 0
    property Item menuFocusReturn: null

    // card menu â†’ owning surface (spec Â§4.2)
    signal resumeRequested(var entry)
    signal detailRequested(var entry)
    signal dismissRequested(var entry)
    signal markWatchedRequested(var entry, bool watched)
    signal removeRequested(var entry)
    readonly property alias librarySelection: wall.selection
    property var removeSelectionEntry: null
    readonly property var selectionState: wall.selection

    function activateRow(row) {
        if (selectionState.active) selectionState.toggle(row)
        else detailRequested(row.entry)
    }

    Theme { id: theme }

    function computeRows(cr, pr, isActive) {
        if (!isActive || typeof Collection === "undefined" || typeof Progress === "undefined") return []
        var entries = Collection.items("theatre")
        var plist = Progress.recent("video", 0)
        return Api.buildRows(entries, plist,
            function (id) {
                return ({ mark: Progress.watchedMark(id),
                          manual: Progress.watchedMarkIsManual(id),
                          actionAt: Progress.watchedMarkActionAt(id) })
            },
            function (entry) {
                if (!entry || entry.type === "series") return false
                if (typeof ProfileHistory === "undefined" || !ProfileHistory) return false
                return ProfileHistory.get("movie", String(entry.id))
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

    function wallIdentityAt(index) {
        var row = root.visibleRows[index]
        return row && row.entry && row.entry.id !== undefined
            ? String(row.entry.id) : ""
    }

    function wallIndexForIdentity(identity) {
        var wanted = String(identity || "")
        for (var i = 0; i < root.visibleRows.length; ++i) {
            if (root.wallIdentityAt(i) === wanted)
                return i
        }
        return -1
    }

    function toggleStateFilter(key) {
        stateFilter = (key === "" || stateFilter === key) ? "" : key
    }
    function openMenu(row, x, y, invoker) {
        if (selectionState.active) { selectionState.toggle(row); return }
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
    // A stationary touch hold may only open the card menu while its press point still sits
    // inside the wall viewport (and, in page-flow, the clipped page Flickable) — a flick's
    // momentum can carry the card out from under a stationary finger mid-hold.
    function holdPointVisible(area, x, y) {
        if (!area || !area.visible || !root.visible)
            return false
        const wallPoint = area.mapToItem(wall, x, y)
        if (wallPoint.x < 0 || wallPoint.y < 0
                || wallPoint.x > wall.width || wallPoint.y > wall.height)
            return false
        if (root.pageFlick) {
            const flowPoint = area.mapToItem(root.pageFlick, x, y)
            if (flowPoint.x < 0 || flowPoint.y < 0
                    || flowPoint.x > root.pageFlick.width || flowPoint.y > root.pageFlick.height)
                return false
        }
        return true
    }
    function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : "" }
    function menuActions() {
        var out = ["resume", "detail"]
        if (layout.touch) out.push("select")
        if (menuRow && menuRow.progress > 0) out.push("dismiss")
        out.push("watch"); out.push("remove")
        return out
    }
    function menuActionIndex(action) { return menuActions().indexOf(action) }
    function triggerMenuAction(action) {
        if (!menuRow) return
        if (action === "resume") resumeRequested(menuRow.entry)
        else if (action === "select" && layout.touch) selectionState.begin(menuRow)
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

    // â”€â”€ header: the shelf ledger leads (the active "Library" tab already names it) + search â”€â”€
    Item {
        id: header
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(64, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(64, theme.margin)
        height: layout.targetSize(root.phoneLayout ? searchField.height + 12 + ledger.implicitHeight : 76); z: 20

        // THE SHELF LEDGER â€” every fragment is the filter itself (gold + underline when active)
        Flow {
            id: ledger
            anchors.left: parent.left; anchors.right: root.phoneLayout ? parent.right : searchField.left
            anchors.rightMargin: root.phoneLayout ? 0 : 24
            anchors.top: root.phoneLayout ? searchField.bottom : undefined
            anchors.topMargin: root.phoneLayout ? 12 : 0
            anchors.verticalCenter: root.phoneLayout ? undefined : parent.verticalCenter
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

        TouchTextField {
            id: searchField
            objectName: "theatreLibrarySearch"
            anchors.right: parent.right; anchors.top: root.phoneLayout ? parent.top : undefined
            anchors.verticalCenter: root.phoneLayout ? undefined : parent.verticalCenter
            width: root.phoneLayout ? parent.width : 250; height: layout.touch ? layout.touchTargetSize : 38; leftPadding: 16; rightPadding: 16
            placeholderText: "Search your library"; placeholderTextColor: theme.inkDimmer
            color: theme.ink; selectionColor: theme.gold; selectedTextColor: "#111111"
            font.family: theme.ui; font.pixelSize: theme.fontSize(14)
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
        implicitWidth: layout.targetSize(fragRow.implicitWidth + (root.phoneLayout ? 12 : 0))
        implicitHeight: layout.targetSize(root.phoneLayout ? 48 : 22)
        Row {
            id: fragRow; x: root.phoneLayout ? 6 : 0; anchors.verticalCenter: parent.verticalCenter; spacing: 0
            Text {
                text: parent.parent.value; color: parent.parent.active ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(14); font.weight: Font.DemiBold
            }
            Text {
                text: " " + parent.parent.label
                color: parent.parent.active ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: theme.fontSize(14)
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
        visible: !root.phoneLayout
        implicitWidth: 12; implicitHeight: 22
        Rectangle { anchors.centerIn: parent; width: 3; height: 3; radius: 1.5; color: theme.inkDimmer }
    }

    // â”€â”€ the quiet filter bar: sorts | type | airing â”€â”€
    Item {
        id: filterBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: header.bottom
        height: root.phoneLayout ? 56 : 54; z: 19

        component FilterPill: Rectangle {
            property string label: ""
            property bool active: false
            signal picked()
            implicitWidth: layout.targetSize(pillText.implicitWidth + 30); height: layout.touch ? layout.touchTargetSize : 34; radius: 14
            color: active ? theme.gold : (pillHover.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
            border.width: active ? 0 : 1; border.color: Qt.rgba(1, 1, 1, 0.14)
            Behavior on color { ColorAnimation { duration: 130 } }
            Text {
                id: pillText; anchors.centerIn: parent; text: parent.label
                color: parent.active ? "#17120a" : theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold
            }
            HoverHandler { id: pillHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: parent.label; focusRadius: parent.radius
                onTriggered: parent.picked() }
        }

        Flickable {
            objectName: "theatreLibraryFilterViewport"
            anchors.fill: parent
            anchors.leftMargin: root.phoneLayout ? 0 : Math.max(64, theme.margin)
            contentWidth: filterRow.implicitWidth
            contentHeight: height
            clip: root.phoneLayout
            interactive: root.phoneLayout && contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            Row {
                id: filterRow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter; spacing: 8

                Repeater {
                    model: [ { key: "lastWatched", label: "Last watched" }, { key: "added", label: "Recently added" },
                             { key: "az", label: "A–Z" }, { key: "year", label: "Year" } ]
                    delegate: FilterPill {
                        required property var modelData
                        objectName: "theatreLibrarySort_" + modelData.key
                        label: modelData.label; active: root.sortMode === modelData.key
                        onPicked: root.sortMode = modelData.key
                    }
                }
                Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: theme.edge }
                Repeater {
                    model: [ { key: "", label: "All" }, { key: "movie", label: "Movies" }, { key: "series", label: "Series" } ]
                    delegate: FilterPill {
                        required property var modelData
                        objectName: "theatreLibraryType_" + (modelData.key || "all")
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
    }

    // â”€â”€ the wall â”€â”€
    Item {
        id: wallHost
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: filterBar.bottom; anchors.topMargin: 22
        anchors.bottom: root.pageFlow ? undefined : parent.bottom
        anchors.bottomMargin: 18
        height: root.pageFlow ? flowContentHeight : implicitHeight
        readonly property real flowContentHeight: Math.max(420,
            Math.ceil(root.visibleRows.length / Math.max(1, wall.columnCount)) * wall.cellHeight)
        readonly property real visibleTop: {
            if (!root.pageFlow) return 0
            root.pageFlick.contentY; root.pageFlick.contentHeight; wallHost.y
            return root.pageFlick.contentY - wallHost.mapToItem(root.pageFlick.contentItem, 0, 0).y
        }
        readonly property real windowHeight: root.pageFlow
            ? Math.min(flowContentHeight, root.pageFlick.height) : height
        readonly property real windowTop: root.pageFlow
            ? Math.max(0, Math.min(visibleTop, flowContentHeight - windowHeight)) : 0
    CollectionGrid {
        id: wall
        pageOwner: root
        pageActive: root.active
        pageFlow: root.pageFlow
        removeEntry: root.removeSelectionEntry
        onRemoveRequested: (entry) => root.removeRequested(entry)
        allowWatched: true
        onWatchedRequested: (entry, watched) => root.markWatchedRequested(entry, watched)
        objectName: "theatreLibraryGrid"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: root.pageFlow ? undefined : parent.top
        anchors.bottom: root.pageFlow ? undefined : parent.bottom
        y: root.pageFlow ? wallHost.windowTop : 0
        height: root.pageFlow ? wallHost.windowHeight : parent.height
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(48, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(38, theme.margin - 10)
        model: root.visibleRows
        // fixed gallery poster size (148Ã—222) matches ContinueTile / discover shelves â€” the
        // deliberate, consistent card size that reads as one family with the rest of the app.
        labelHeight: theme.fontSize(root.portraitPhoneLayout ? 54 : 36)
        ScrollBar.vertical: HouseScrollBar { flick: wall; visible: !root.pageFlow }
        Binding {
            target: wall; property: "contentY"
            value: wall.originY + wallHost.windowTop
            when: root.pageFlow
            restoreMode: Binding.RestoreNone
        }
        onContentYChanged: if (root.menuRow) root.closeMenu(false)
        onCurrentIndexChanged: if (root.pageFlow && activeFocus) root.revealCell(currentIndex)
        focusPolicy: root.visibleRows.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => wallKeys.handle(event)
        KeyboardCollectionController {
            id: wallKeys; view: wall; orientation: "grid"; columns: Math.max(1, wall.columnCount)
            positionIndexFn: root.pageFlow ? root.revealCell : null
            count: root.visibleRows.length; contextEnabled: true
            modelRevision: root.collRev * 100000 + root.progRev
            identityForIndex: root.wallIdentityAt
            indexForIdentity: root.wallIndexForIdentity
            onActivated: (index) => root.activateRow(root.visibleRows[index])
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
            Accessible.role: selectionState.active ? Accessible.CheckBox : Accessible.Button
            Accessible.name: String(card.modelData.entry.title || "Untitled")
            Accessible.checkable: selectionState.active
            Accessible.checked: selectionState.contains(card.modelData)
            Accessible.onPressAction: root.activateRow(card.modelData)
            Accessible.onToggleAction: root.activateRow(card.modelData)
            LibrarySelectionMark {
                objectName: "theatreLibrarySelection_" + String(card.modelData.entry.id)
                visible: selectionState.active; selected: selectionState.contains(card.modelData)
                x: 4; y: 4; z: 50
            }

            // â”€â”€ two offset depth plates behind the poster (ContinueTile world grammar) â”€â”€
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

            // â”€â”€ the poster (cover art + scrim + badges + hairline), one rounded mask â”€â”€
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
                        font.pixelSize: theme.fontSize(17); font.weight: Font.DemiBold
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

                // new-episode "+N" (Library page ONLY) â€” top-left
                Rectangle {
                    visible: card.modelData.newCount > 0
                    anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                    radius: 7; height: 20; width: newLabel.implicitWidth + 16; color: theme.gold
                    Text {
                        id: newLabel; anchors.centerIn: parent; text: "+" + card.modelData.newCount
                        color: "#17120a"; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                    }
                }
                // downloaded â†“ chip â€” bottom-right
                Rectangle {
                    visible: card.modelData.downloaded
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
                    width: 20; height: 20; radius: 6; color: Qt.rgba(0.04, 0.04, 0.055, 0.72)
                    border.width: 1; border.color: theme.edge
                    Glyph { anchors.centerIn: parent; width: 11; height: 11; file: "download"; ink: theme.inkDim }
                }
                // watched âœ“ â€” top-right (hidden while hovering so the â‹® can take the corner)
                Rectangle {
                    visible: card.watched && !card.hovered
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                    width: 22; height: 22; radius: 11; color: Qt.rgba(0.04, 0.04, 0.055, 0.72)
                    border.width: 1; border.color: theme.edge
                    Text { anchors.centerIn: parent; text: "✓"; color: theme.inkDim; font.pixelSize: 12 }
                }
                // in-progress hairline â€” cover base
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
            // stable rounded mask source (no animation) â€” a texture provider, not drawn directly
            Item {
                id: worldMask
                visible: false
                anchors.fill: worldContent
                layer.enabled: true
                Rectangle { anchors.fill: parent; radius: 12; color: "black" }
            }

            // â”€â”€ the hover film + gold edge (ContinueTile shared grammar) â”€â”€
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

            // â‹® button â€” top-right, fades in on hover (ContinueTile shared grammar)
            CollectionCardMenu {
                id: dots
                objectName: "theatreLibraryCardMenu_" + (modelData.entry.id || "")
                z: root.phoneLayout ? 1 : 0
                anchors.right: worldContent.right; anchors.top: worldContent.top; anchors.margins: 8
                touch: layout.touch
                highlighted: root.menuRowId === String(card.modelData.entry.id || "")
                opacity: root.phoneLayout || card.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
                onTriggered: {
                    wall.currentIndex = card.index
                    wall.forceActiveFocus(Qt.MouseFocusReason)
                    const pt = dots.mapToItem(root, dots.width, dots.height)
                    root.openMenu(card.modelData, pt.x, pt.y, wall)
                }
            }

            Text {
                id: cardTitle
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: worldContent.bottom; anchors.topMargin: 9
                text: card.modelData.entry.title || "Untitled"
                color: card.watched ? theme.inkDim : theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold
                wrapMode: root.portraitPhoneLayout ? Text.WordWrap : Text.NoWrap
                elide: Text.ElideRight; maximumLineCount: root.portraitPhoneLayout ? 2 : 1
            }
            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: root.portraitPhoneLayout ? cardTitle.bottom : worldContent.bottom
                anchors.topMargin: root.portraitPhoneLayout ? 4 : 30
                text: {
                    var parts = []
                    if (card.modelData.year > 0) parts.push(card.modelData.year)
                    parts.push(card.modelData.isSeries ? "Series" : "Movie")
                    if (card.modelData.isSeries && card.modelData.airing) parts.push(root.cap(card.modelData.airing))
                    return parts.join(" · ")
                }
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12); elide: Text.ElideRight
            }

            HoverHandler { id: cardHover }
            // Primary tap stays the poster's own action; a STATIONARY TOUCH long press opens
            // the same ⋮ menu the dots button owns (identical openMenu row/anchor/focus-return
            // contract). Real-mouse holds stay gated off so desktop click semantics are
            // untouched, and any drag hands the grab to the owning Flickable, cancelling the hold.
            MouseArea {
                id: cardTap
                anchors.fill: worldContent; cursorShape: Qt.PointingHandCursor
                pressAndHoldInterval: root.phoneLayout ? 500 : 2147483647
                property bool holdMenuFired: false
                onPressed: cardTap.holdMenuFired = false
                onPressAndHold: (mouse) => {
                    if (mouse.source === Qt.MouseEventNotSynthesized) return
                    if (!root.holdPointVisible(cardTap, mouse.x, mouse.y)) return
                    cardTap.holdMenuFired = true
                    wall.currentIndex = card.index
                    wall.forceActiveFocus(Qt.MouseFocusReason)
                    const anchor = cardTap.mapToItem(root, mouse.x, mouse.y)
                    root.openMenu(card.modelData, anchor.x, anchor.y, wall)
                }
                onCanceled: cardTap.holdMenuFired = false
                onClicked: {
                    if (cardTap.holdMenuFired) { cardTap.holdMenuFired = false; return }
                    root.activateRow(card.modelData)
                }
            }
        }
    }
    ScrollGlide { flick: root.pageFlow ? null : wall }

    // â”€â”€ empty states â”€â”€
    Column {
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Your library is empty"
            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(30)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Press play on anything, or tap + Library — it lands here."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(14)
        }
    }
    Column {
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing matches these filters"
            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(30)
        }
    }
    }

    // â”€â”€ the floating â‹® menu (root level â€” the wall clips) â”€â”€
    MouseArea {
        anchors.fill: parent; z: 55; visible: root.menuRow !== null
        onClicked: root.closeMenu(true)
    }
    CollectionMenu {
        id: menuPanel
        objectName: "theatreLibraryMenuPanel"
        z: 56
        visible: root.menuRow !== null
        touch: layout.touch
        viewport: root.menuViewport
        anchorX: root.menuX
        anchorY: root.menuY
        itemPrefix: "theatreLibraryMenuItem_"
        scrollObjectName: "theatreLibraryMenuScroll"
        items: {
            const row = root.menuRow
            const out = [{id:"resume", icon:"play", label:row && (row.progress > 0 || row.state === "progress") ? "Resume" + (row.sub ? " " + row.sub : "") : "Play"},
                {id:"detail", icon:"info", label:"Details"}]
            if (layout.touch) out.push({id:"select", icon:"circle-check", label:"Select"})
            out.push({separator:true})
            if (row && row.progress > 0) out.push({id:"dismiss", icon:"rotate-ccw", label:"Dismiss progress"})
            out.push({id:"watch", icon:"circle-check", label:row && row.state === "watched" ? "Mark unwatched" : "Mark watched"},
                {separator:true}, {id:"remove", icon:"x", label:"Remove from Library", warn:true})
            return out
        }
        onPicked: (action) => root.triggerMenuAction(action)
        onCloseRequested: (restoreFocus) => root.closeMenu(restoreFocus)
    }

}
