// TankobanLibraryTab — Tankoban's Library tab. TB-001 shipped the mixed manga+comic wall
// + Details routing; TB-002 added the manga-chapter progress join + resume-vs-Details tap
// branch + the Collection identity fix; TB-003 added the volume-lane + comic joins and the
// most-recent-lane rule; TB-004 added the download badge (resume-target chapter on disk);
// TB-005 adds search + 3 filters (All / In Progress / Downloaded) + 3 sorts (Last Read /
// Recently Added / A-Z) + the card ⋮ menu with Remove from Library. The pure row-derivation
// lives in TankobanLibraryApi.js; this page owns the live singletons (Collection / Progress /
// Downloads), the toolbar state, and the menu. Mirrors LibraryPage.qml's construction
// discipline (every singleton typeof-guarded so this constructs offscreen for the harness)
// but carries Tankoban's own vocabulary. Renders as tab content inside TankobanWorld (no
// standalone chrome), retained across tab switches like TankobanDiscoverPage and Theatre's
// LibraryPage — same fixed-viewport-height + self-scrolling GridView pattern both ship.
import QtQuick
import QtQuick.Controls
import "TankobanLibraryApi.js" as Api

Item {
    id: root

    // The retained Library tab keeps its filter state, but hidden worlds must not scan all three
    // Progress lanes or probe Downloads while the tab is not visible.
    property bool active: true

    // ── reactive data (recompute on Collection OR Progress OR Downloads change).
    //    Progress.revision is one global counter across all kinds, so naming it in the
    //    binding is enough to pick up manga progress writes. Downloads has no revision
    //    counter — only per-chapter finished/removed signals — so dlRev is a LOCAL bump
    //    counter those handlers increment to invalidate the badge map. All three
    //    singletons are typeof-guarded so this constructs offscreen in the harness the
    //    same way TB-001 guarded Collection.) ──
    property int collRev: (typeof Collection !== "undefined" ? Collection.revision : 0)
    property int progRev: (typeof Progress !== "undefined" ? Progress.revision : 0)
    property int dlRev: 0
    property var allRows: computeRows(collRev, progRev, dlRev, active)

    // ── TB-005 toolbar state: filter chip + sort mode + search needle ──
    // Filter values: "" (All) | "inProgress" | "downloaded". Sort values: "lastRead"
    // (default) | "added" | "az". query is the search field text. visibleRows is the
    // filter+sort result the wall binds; it depends on allRows + filter + sort + query,
    // so naming each in the binding keeps the wall reactive to all of them.
    property string filter: ""
    property string sortMode: "lastRead"
    property string query: ""
    property var visibleRows: Api.sortRows(
        Api.applyFilters(allRows, { filter: root.filter, query: root.query }),
        root.sortMode)

    // Downloads has no revision property — only per-chapter finished/removed signals.
    // Either one changes the on-disk set, so both bump dlRev and the binding recomputes.
    // ignoredSignals lets the harness construct without a real Downloads singleton.
    Connections {
        target: (typeof Downloads !== "undefined") ? Downloads : null
        function onFinished(cid) { if (root.active) root.dlRev = root.dlRev + 1 }
        function onRemoved(cid)  { if (root.active) root.dlRev = root.dlRev + 1 }
    }

    // Card tap (no menu yet). TB-001 routed every tap to Details; TB-002 branches: a
    // started row (state "inProgress" with a resumeTarget) resumes, every other tap opens
    // Details. handleCardTap carries the logic so the offscreen harness can drive it
    // directly against a synthetic row without a real delegate.
    signal detailRequested(var entry)
    signal resumeRequested(var record)
    // TB-005: the ⋮ menu's Remove action asks the owner (TankobanWorld) to drop the row's
    // entry from the Collection. The page never calls Collection.remove itself — same
    // separation as detailRequested/resumeRequested, and same as Theatre's LibraryPage.
    signal removeRequested(var entry)
    function handleCardTap(row) {
        if (!row) return
        if (row.state === "inProgress" && row.resumeTarget)
            root.resumeRequested(row.resumeTarget)
        else
            root.detailRequested(row.entry)
    }

    Theme { id: theme }

    function computeRows(cr, pr, dr, isActive) {
        if (!isActive || typeof Collection === "undefined") return []
        var entries = Collection.items("tankoban")
        var mp = (typeof Progress !== "undefined") ? Progress.recent("manga", 0) : []
        var vp = (typeof Progress !== "undefined") ? Progress.recent("tankoban", 0) : []
        var cp = (typeof Progress !== "undefined") ? Progress.recent("comic", 0) : []
        // TB-004 on-disk map: one Downloads.isDownloaded probe per distinct resume
        // chapter id across the three progress lanes. The map is rebuilt on every
        // recompute (cheap — bounded by the recent-set size, not the library size) so it
        // stays honest after a download finishes or a chapter is removed. `dr` (dlRev)
        // is named here only to wire the binding dependency; it carries no payload.
        var onDisk = {}
        if (typeof Downloads !== "undefined") {
            for (var lane = 0; lane < 3; lane++) {
                var list = lane === 0 ? mp : (lane === 1 ? vp : cp)
                for (var i = 0; i < list.length; i++) {
                    var rec = list[i]
                    if (!rec || !rec.resume) continue
                    var chId = String(rec.resume.chapterId || "")
                    if (!chId.length || onDisk.hasOwnProperty(chId)) continue
                    if (Downloads.isDownloaded(chId)) onDisk[chId] = true
                }
            }
        }
        return Api.buildRows(entries, mp, vp, cp, onDisk)
    }

    // ── TB-005 menu state: the row the ⋮ menu is open for, plus its anchor coords in
    //    root space. The menu panel is a root-level child (not inside the wall) because
    //    the wall's clip:true would clip a popup rendered inside a delegate. ──
    property var menuRow: null
    property real menuX: 0
    property real menuY: 0
    property Item menuFocusReturn: null
    readonly property string menuRowId: menuRow ? String(menuRow.entry && menuRow.entry.id || "") : ""
    function openMenu(row, x, y, invoker) {
        menuRow = row; menuX = x; menuY = y; menuFocusReturn = invoker || wall
        Qt.callLater(function() { menuPanel.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeMenu(restoreFocus) {
        const target = menuFocusReturn
        menuFocusReturn = null; menuRow = null; menuX = 0; menuY = 0
        if (restoreFocus !== false && target)
            Qt.callLater(function() { if (target.visible && target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function triggerMenuIndex(index) {
        if (index !== 0 || !menuRow) return
        removeRequested(menuRow.entry); closeMenu(true)
    }
    function toggleFilter(key) { filter = (key === "" || filter === key) ? "" : key }

    // ── toolbar: search field + 3 filter pills + 3 sort pills ──
    Item {
        id: toolbar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 44
        anchors.leftMargin: Math.max(48, theme.margin); anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 8

        // search field (mirrors LibraryPage.qml's searchField shape)
        TextField {
            id: searchField
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            width: 220; height: 38; leftPadding: 16; rightPadding: 16
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

        // 3 filter pills: All / In Progress / Downloaded
        Row {
            id: filterRow
            anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: [
                    { label: "All", key: "" },
                    { label: "In Progress", key: "inProgress" },
                    { label: "Downloaded", key: "downloaded" }
                ]
                FilterPill {
                    label: modelData.label
                    active: root.filter === modelData.key
                    onPicked: root.toggleFilter(modelData.key)
                }
            }
        }

        // 3 sort pills: Last Read / Recently Added / A-Z
        Row {
            id: sortRow
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: [
                    { label: "Last Read", key: "lastRead" },
                    { label: "Recently Added", key: "added" },
                    { label: "A-Z", key: "az" }
                ]
                FilterPill {
                    label: modelData.label
                    active: root.sortMode === modelData.key
                    onPicked: root.sortMode = modelData.key
                }
            }
        }
    }

    // ── the wall ──
    GridView {
        id: wall
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: toolbar.bottom; anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(48, theme.margin); anchors.rightMargin: Math.max(38, theme.margin - 10)
        anchors.topMargin: 14; anchors.bottomMargin: 18
        clip: true; boundsBehavior: Flickable.StopAtBounds
        model: root.visibleRows
        readonly property int columnCount: Math.max(2, Math.floor(width / 178))
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.floor((cellWidth - 16) * 1.5) + 56
        cacheBuffer: cellHeight * 2
        ScrollBar.vertical: HouseScrollBar { flick: wall }
        // closing the menu on scroll stops a stale-positioned popup following a flick
        onContentYChanged: if (root.menuRow) root.closeMenu(false)
        focusPolicy: root.visibleRows.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => wallKeys.handle(event)
        KeyboardCollectionController {
            id: wallKeys; view: wall; orientation: "grid"; columns: Math.max(1, wall.columnCount)
            count: root.visibleRows.length; contextEnabled: true
            onActivated: (index) => root.handleCardTap(root.visibleRows[index])
            onContextRequested: (index) => {
                const host = wall.currentItem
                if (!host) return
                const pt = host.mapToItem(root, host.width, 0)
                root.openMenu(host.modelData, pt.x, pt.y, wall)
            }
        }

        delegate: Item {
            id: host
            required property var modelData
            required property int index
            width: wall.cellWidth - 16; height: wall.cellHeight - 18
            CataloguePosterCard {
                id: card
                anchors.fill: parent
                item: ({ "title": host.modelData.entry.title || "", "cover": host.modelData.entry.cover || "" })
                keyboardFocused: wall.activeFocus && wall.currentIndex === host.index
                onActivated: root.handleCardTap(host.modelData)
            }
            // ⋮ hover button — top-right of the card. Mirrors LibraryPage.qml's dots
            // button; the menu panel lives at root level (wall.clip would clip it here).
            Rectangle {
                id: dots
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                width: 26; height: 26; radius: 8; color: Qt.rgba(0.04, 0.04, 0.075, 0.82)
                border.width: 1
                border.color: root.menuRowId === String(modelData.entry.id || "") ? theme.gold : theme.edge
                visible: card.effectiveHovered || root.menuRowId === String(modelData.entry.id || "")
                Text { anchors.centerIn: parent; text: "⋮"; color: theme.ink; font.pixelSize: 15 }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        wall.currentIndex = host.index
                        wall.forceActiveFocus(Qt.MouseFocusReason)
                        var pt = dots.mapToItem(root, dots.width, dots.height)
                        root.openMenu(host.modelData, pt.x, pt.y, wall)
                    }
                }
            }
        }
    }
    ScrollGlide { flick: wall }

    // ── empty states ──
    // TB-001: empty Collection ("Your library is empty"). TB-005 adds the no-match state
    // (Collection non-empty but the filter/search narrowed the wall to nothing) — same
    // Column, branch on whether allRows itself is empty.
    Column {
        anchors.centerIn: wall; spacing: 12
        visible: root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.allRows.length === 0 ? "Your library is empty" : "No matches"
            color: theme.ink; font.family: theme.display; font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.allRows.length === 0
                ? "Save a manga or comic series — it lands here."
                : "Try a different filter or search."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
        }
    }

    // ── TB-005 ⋮ menu panel (root-level so wall.clip doesn't clip it) ──
    // A click-swallower behind the panel closes it on outside click. The single action is
    // Remove from Library, which emits removeRequested(entry) — the owner (TankobanWorld)
    // does the actual Collection.remove. Same shape as Theatre's LibraryPage menu.
    Item {
        id: menuLayer
        anchors.fill: parent
        visible: root.menuRow !== null
        MouseArea {
            anchors.fill: parent
            onClicked: root.closeMenu(true)      // outside click swallows + closes
        }
        Rectangle {
            id: menuPanel
            width: 200
            x: Math.max(8, Math.min(root.menuX - width, root.width - width - 8))
            y: Math.max(8, Math.min(root.menuY, root.height - height - 8))
            radius: 12
            color: Qt.rgba(0.06, 0.07, 0.10, 0.97)
            border.width: 1; border.color: theme.edge
            visible: root.menuRow !== null
            focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    root.triggerMenuIndex(0); event.accepted = true; return
                }
                if (event.key === Qt.Key_Escape) { root.closeMenu(true); event.accepted = true; return }
                if (event.key === Qt.Key_Tab) { event.accepted = true }
            }
            Column {
                anchors.fill: parent; anchors.margins: 6; spacing: 2
                MenuRow {
                    label: "Remove from Library"; warn: true
                    onClicked: root.triggerMenuIndex(0)
                }
            }
        }
    }

    // ── inline components (house style: copy-pasted per page, no shared file) ──
    // FilterPill mirrors LibraryPage.qml's FilterPill inline component verbatim.
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

    // MenuRow mirrors LibraryPage.qml's MenuItem inline component shape.
    component MenuRow: Item {
        property string label: ""
        property bool warn: false
        signal clicked()
        width: menuPanel.width - 12; height: 38
        Rectangle {
            anchors.fill: parent; radius: 8
            color: rowHover.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
            Text {
                anchors.centerIn: parent; text: label
                color: warn ? "#d9534f" : theme.ink
                font.family: theme.ui; font.pixelSize: 14
            }
            HoverHandler { id: rowHover }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
}
