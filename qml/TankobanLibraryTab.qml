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
import QtQuick.Window
import QtQuick.Controls
import "TankobanLibraryApi.js" as Api

Item {
    id: root
    readonly property bool phoneLayout: {
        const window = root.Window.window
        return !!(window && window.androidHost && Math.min(window.width, window.height) < 600)
    }
    readonly property bool portraitPhoneLayout: {
        const window = root.Window.window
        return root.phoneLayout && !!window && window.width < window.height
    }

    // The retained Library tab keeps its filter state, but hidden worlds must not scan all three
    // Progress lanes or probe Downloads while the tab is not visible.
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
    readonly property alias librarySelection: selectionState
    property var removeSelectionEntry: null
    LibrarySelectionState { id: selectionState; rows: root.phoneLayout ? root.visibleRows : [] }
    LibrarySelectionHost {
        owner: root; selection: selectionState; pageActive: root.active && root.phoneLayout
        removeEntry: root.removeSelectionEntry
        onRemoveRequested: (entry) => root.removeRequested(entry)
    }
    function handleCardTap(row) {
        if (selectionState.active) { selectionState.toggle(row); return }
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
        if (selectionState.active) { selectionState.toggle(row); return }
        menuRow = row; menuX = x; menuY = y; menuFocusReturn = invoker || wall
        menuPanel.currentIndex = 0
        Qt.callLater(function() { menuPanel.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeMenu(restoreFocus) {
        const target = menuFocusReturn
        menuFocusReturn = null; menuRow = null; menuX = 0; menuY = 0
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
    function triggerMenuIndex(index) {
        if (!menuRow) return
        if (index === 0) removeRequested(menuRow.entry)
        else if (index === 1 && root.phoneLayout) selectionState.begin(menuRow)
        closeMenu(true)
    }
    function toggleFilter(key) { filter = (key === "" || filter === key) ? "" : key }

    // ── toolbar: search field + 3 filter pills + 3 sort pills ──
    Item {
        id: toolbar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: root.phoneLayout ? 116 : 44
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(48, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(38, theme.margin - 10)
        anchors.topMargin: 8

        // search field (mirrors LibraryPage.qml's searchField shape)
        TextField {
            id: searchField
            objectName: "tankobanLibrarySearch"
            anchors.left: parent.left; anchors.top: root.phoneLayout ? parent.top : undefined
            anchors.verticalCenter: root.phoneLayout ? undefined : parent.verticalCenter
            width: root.phoneLayout ? parent.width : 220; height: root.phoneLayout ? 48 : 38; leftPadding: 16; rightPadding: 16
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

        Flickable {
            objectName: "tankobanLibraryFilterViewport"
            y: root.phoneLayout ? 60 : 0
            width: parent.width
            height: root.phoneLayout ? 48 : parent.height
            contentWidth: root.phoneLayout ? filterRow.implicitWidth + sortRow.implicitWidth + 16 : width
            contentHeight: height
            clip: root.phoneLayout
            interactive: root.phoneLayout && contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            // 3 filter pills: All / In Progress / Downloaded
            Row {
                id: filterRow
                anchors.left: root.phoneLayout ? parent.left : undefined
                anchors.horizontalCenter: root.phoneLayout ? undefined : parent.horizontalCenter; anchors.verticalCenter: parent.verticalCenter
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
                anchors.left: root.phoneLayout ? filterRow.right : undefined
                anchors.leftMargin: root.phoneLayout ? 16 : 0
                anchors.right: root.phoneLayout ? undefined : parent.right; anchors.verticalCenter: parent.verticalCenter
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
    }

    // ── the wall ──
    Item {
        id: wallHost
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: toolbar.bottom; anchors.topMargin: 14
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
    GridView {
        id: wall
        objectName: "tankobanLibraryGrid"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: root.pageFlow ? undefined : parent.top
        anchors.bottom: root.pageFlow ? undefined : parent.bottom
        y: root.pageFlow ? wallHost.windowTop : 0
        height: root.pageFlow ? wallHost.windowHeight : parent.height
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(48, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(38, theme.margin - 10)
        clip: true; boundsBehavior: Flickable.StopAtBounds
        interactive: !root.pageFlow
        highlightFollowsCurrentItem: !root.pageFlow
        model: root.visibleRows
        readonly property int posterW: root.portraitPhoneLayout
            ? Math.min(94, Math.max(0, cellWidth - 12)) : Math.max(0, cellWidth - 16)
        readonly property int posterH: Math.floor(posterW * 1.5)
        readonly property int columnCount: root.portraitPhoneLayout ? Math.max(1, Math.floor(width / 106))
            : root.phoneLayout ? Math.max(1, Math.floor(width / 128)) : Math.max(2, Math.floor(width / 178))
        cellWidth: Math.floor(width / columnCount)
        cellHeight: posterH + 24 + Math.ceil(theme.fontSize(root.portraitPhoneLayout ? 54 : 32))
        cacheBuffer: cellHeight * 2
        ScrollBar.vertical: HouseScrollBar { flick: wall; visible: !root.pageFlow }
        Binding {
            target: wall; property: "contentY"
            value: wall.originY + wallHost.windowTop
            when: root.pageFlow
            restoreMode: Binding.RestoreNone
        }
        // closing the menu on scroll stops a stale-positioned popup following a flick
        onContentYChanged: if (root.menuRow) root.closeMenu(false)
        onCurrentIndexChanged: if (root.pageFlow && activeFocus) root.revealCell(currentIndex)
        focusPolicy: root.visibleRows.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => wallKeys.handle(event)
        KeyboardCollectionController {
            id: wallKeys; view: wall; orientation: "grid"; columns: Math.max(1, wall.columnCount)
            positionIndexFn: root.pageFlow ? root.revealCell : null
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
            width: wall.posterW; height: wall.cellHeight - 18
            Accessible.role: Accessible.CheckBox
            Accessible.ignored: !selectionState.active
            Accessible.name: String(host.modelData.entry.title || "Untitled")
            Accessible.checkable: true
            Accessible.checked: selectionState.contains(host.modelData)
            Accessible.onPressAction: root.handleCardTap(host.modelData)
            Accessible.onToggleAction: root.handleCardTap(host.modelData)
            CataloguePosterCard {
                id: card
                anchors.fill: parent
                item: ({ "title": host.modelData.entry.title || "", "cover": host.modelData.entry.cover || "" })
                visualProfile: root.portraitPhoneLayout ? "gallery" : "classic"
                keyboardFocused: wall.activeFocus && wall.currentIndex === host.index
                Accessible.ignored: selectionState.active
                onActivated: root.handleCardTap(host.modelData)
            }
            LibrarySelectionMark {
                objectName: "tankobanLibrarySelection_" + String(host.modelData.entry.id)
                visible: selectionState.active; selected: selectionState.contains(host.modelData)
                x: 4; y: 4; z: 50
            }
            // Stationary touch long press opens the row's ⋮ menu through the same openMenu
            // contract as the dots button below. The overlay owns the primary tap as well and
            // routes it to the exact handleCardTap branch the card's own activation used, so
            // tap/resume behavior is unchanged; real-mouse holds stay gated off for desktop
            // parity, and any drag hands the grab to the owning Flickable, cancelling the hold.
            MouseArea {
                id: cardHold
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                pressAndHoldInterval: root.phoneLayout ? 500 : 2147483647
                property bool holdMenuFired: false
                onPressed: cardHold.holdMenuFired = false
                onPressAndHold: (mouse) => {
                    if (mouse.source === Qt.MouseEventNotSynthesized) return
                    if (!root.holdPointVisible(cardHold, mouse.x, mouse.y)) return
                    cardHold.holdMenuFired = true
                    wall.currentIndex = host.index
                    wall.forceActiveFocus(Qt.MouseFocusReason)
                    const anchor = cardHold.mapToItem(root, mouse.x, mouse.y)
                    root.openMenu(host.modelData, anchor.x, anchor.y, wall)
                }
                onCanceled: cardHold.holdMenuFired = false
                onClicked: {
                    if (cardHold.holdMenuFired) { cardHold.holdMenuFired = false; return }
                    root.handleCardTap(host.modelData)
                }
            }
            // ⋮ hover button — top-right of the card. Mirrors LibraryPage.qml's dots
            // button; the menu panel lives at root level (wall.clip would clip it here).
            Rectangle {
                id: dots
                objectName: "tankobanLibraryCardMenu_" + (modelData.entry.id || "")
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                width: root.phoneLayout ? 48 : 26; height: width; radius: 8; color: Qt.rgba(0.04, 0.04, 0.075, 0.82)
                border.width: 1
                border.color: root.menuRowId === String(modelData.entry.id || "") ? theme.gold : theme.edge
                visible: root.phoneLayout || card.effectiveHovered || root.menuRowId === String(modelData.entry.id || "")
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
    ScrollGlide { flick: root.pageFlow ? null : wall }

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
            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(30)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.allRows.length === 0
                ? "Save a manga or comic series — it lands here."
                : "Try a different filter or search."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(14)
        }
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
            objectName: "tankobanLibraryMenuPanel"
            property int currentIndex: 0
            implicitHeight: menuColumn.implicitHeight + 12
            height: implicitHeight
            width: 200
            x: Math.max(8, Math.min(root.menuX - width, root.width - width - 8))
            y: Math.max(root.menuViewport.y + 8, Math.min(root.menuY, root.menuViewport.y + root.menuViewport.height - height - 8))
            radius: 12
            color: Qt.rgba(0.06, 0.07, 0.10, 0.97)
            border.width: 1; border.color: theme.edge
            visible: root.menuRow !== null
            focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    root.triggerMenuIndex(currentIndex); event.accepted = true; return
                }
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    currentIndex = root.phoneLayout ? 1 - currentIndex : 0
                    event.accepted = true; return
                }
                if (event.key === Qt.Key_Escape) { root.closeMenu(true); event.accepted = true; return }
                if (event.key === Qt.Key_Tab) { event.accepted = true }
            }
            Column {
                id: menuColumn
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6; spacing: 2
                MenuRow {
                    label: "Remove from Library"; warn: true
                    keyboardSelected: menuPanel.activeFocus && menuPanel.currentIndex === 0
                    onClicked: root.triggerMenuIndex(0)
                }
                MenuRow {
                    objectName: "tankobanLibraryMenuItem_select"
                    label: "Select"; visible: root.phoneLayout
                    keyboardSelected: menuPanel.activeFocus && menuPanel.currentIndex === 1
                    onClicked: root.triggerMenuIndex(1)
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
        implicitWidth: pillText.implicitWidth + 30; height: root.phoneLayout ? 48 : 34; radius: 14
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

    // MenuRow mirrors LibraryPage.qml's MenuItem inline component shape.
    component MenuRow: Item {
        property string label: ""
        property bool warn: false
        property bool keyboardSelected: false
        signal clicked()
        width: menuPanel.width - 12; height: root.phoneLayout ? 48 : 38
        Rectangle {
            anchors.fill: parent; radius: 8
            color: rowHover.hovered || parent.keyboardSelected ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
            Text {
                anchors.centerIn: parent; text: label
                color: warn ? "#d9534f" : theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(14)
            }
            HoverHandler { id: rowHover }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
        KeyboardAction {
            anchors.fill: parent; pointerEnabled: false
            accessibleName: parent.label
            onTriggered: parent.clicked()
        }
    }
}
