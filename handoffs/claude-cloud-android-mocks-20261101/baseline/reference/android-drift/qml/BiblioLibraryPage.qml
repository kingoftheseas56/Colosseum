// BiblioLibraryPage â€” Biblio's Library tab (the Theatre-parity Library page, plan
// 2026-08-06-biblio-library-tab-theatre-parity.md, Slice 2). The book-domain mirror of
// LibraryPage.qml MINUS Theatre's video concepts: no watched/airing/finale/new-episode
// ledger â€” just search, a small filter (All | In Progress), sort (Recently added | Last read |
// Aâ€“Z), a wall of saved Collection entries, empty/no-match states, and a per-card â‹® menu.
//
// One Collection entry â†’ one card. Conservative Progress matching: a reliable match enables
// Resume; no match â†’ the card's primary action is Details. Remove affects Collection
// membership only (never Progress, never files) â€” handled inline by the owner (BiblioWorld),
// not here. All derivations are BiblioLibraryApi (headless-proven in Slice 1); this page only
// paints and wires. Context properties (Collection/Progress) are typeof-guarded so it
// constructs offscreen for the harness.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Effects
import "BiblioLibraryApi.js" as Api

Item {
    id: root
    AdaptiveLayout { id: layout; page: root; viewportHeight: root.pageFlick ? root.pageFlick.height : root.height }
    readonly property bool phoneLayout: layout.phoneLayout
    readonly property bool portraitPhoneLayout: layout.touch && layout.phone && layout.portrait

    // Keep the retained page's filters and scroll position, but defer its potentially large
    // Collection/Progress derivation until the Library tab is actually shown.
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

    // â”€â”€ page state â”€â”€
    property string sortMode: "added"          // added | lastRead | az
    property string stateFilter: ""            // "" | inProgress
    property string query: ""

    // â”€â”€ reactive data (recompute on Collection / Progress change) â”€â”€
    property int collRev: (typeof Collection !== "undefined" ? Collection.revision : 0)
    property int progRev: (typeof Progress !== "undefined" ? Progress.revision : 0)
    property var allRows: computeRows(collRev, progRev, active)
    property int rowCount: allRows.length
    property var visibleRows: Api.sortBiblioRows(
        Api.applyBiblioFilters(allRows, { stateFilter: stateFilter, query: query }),
        sortMode)
    property int visibleCount: visibleRows.length
    readonly property Item _keyboardWallForTest: wall
    readonly property Item _keyboardControllerForTest: wallKeys

    // The retained Library page is reached below the shared Biblio chrome. Once its real
    // Collection rows exist, make the wall the keyboard owner so ordinary Tab/arrows enter the
    // same focus surface that Main.qml snapshots and restores across a book detail route.
    function _isFocusDescendant(item) {
        var cursor = item
        while (cursor) {
            if (cursor === root)
                return true
            cursor = cursor.parent
        }
        return false
    }
    function _hasActiveFocusDescendant(item) {
        if (!item)
            return false
        if (item.activeFocus)
            return true
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            if (_hasActiveFocusDescendant(children[i]))
                return true
        }
        return false
    }
    function _isFullyVisibleInClipChain(item) {
        if (!item)
            return false
        var windowItem = root.Window.window
        var sceneTarget = windowItem ? windowItem.contentItem : null
        if (!sceneTarget)
            return false
        var sceneTopLeft = item.mapToItem(sceneTarget, 0, 0)
        var sceneBottomRight = item.mapToItem(sceneTarget, item.width, item.height)
        if (windowItem && (sceneTopLeft.x < -0.5 || sceneTopLeft.y < -0.5
                || sceneBottomRight.x > windowItem.width + 0.5
                || sceneBottomRight.y > windowItem.height + 0.5))
            return false
        for (var ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
            if (!ancestor.clip)
                continue
            var topLeft = item.mapToItem(ancestor, 0, 0)
            var bottomRight = item.mapToItem(ancestor, item.width, item.height)
            if (topLeft.x < -0.5 || topLeft.y < -0.5
                    || bottomRight.x > ancestor.width + 0.5
                    || bottomRight.y > ancestor.height + 0.5)
                return false
        }
        return true
    }
    function _automationGeometryRevision(item) {
        var revision = 0
        for (var cursor = item; cursor; cursor = cursor.parent) {
            revision += Number(cursor.x || 0) + Number(cursor.y || 0)
            revision += Number(cursor.width || 0) + Number(cursor.height || 0)
            if (cursor.contentX !== undefined)
                revision += Number(cursor.contentX || 0)
            if (cursor.contentY !== undefined)
                revision += Number(cursor.contentY || 0)
        }
        return revision
    }
    function focusKeyboardWall() {
        if (!root.visible || root.visibleRows.length === 0 || wall.activeFocus)
            return
        if (!_isFullyVisibleInClipChain(wall))
            return
        if (_hasActiveFocusDescendant(root))
            return
        var windowItem = root.Window.window
        if (windowItem && _isFocusDescendant(windowItem.activeFocusItem))
            return
        wall.forceActiveFocus(Qt.TabFocusReason)
    }
    onSortModeChanged: if (wallKeys) wallKeys.invalidateLane()
    onStateFilterChanged: if (wallKeys) wallKeys.invalidateLane()
    onQueryChanged: if (wallKeys) wallKeys.invalidateLane()

    // â”€â”€ the floating â‹® menu (rendered at root level; the wall GridView clips) â”€â”€
    property var menuRow: null
    property string menuRowId: ""
    property real menuX: 0
    property real menuY: 0
    property Item menuFocusReturn: null

    // card menu â†’ owning surface. resumeRequested carries the matched progress record;
    // detailRequested + removeRequested carry the original Collection entry.
    signal resumeRequested(var entry)
    signal detailRequested(var entry)
    signal removeRequested(var entry)
    readonly property alias librarySelection: wall.selection
    property var removeSelectionEntry: null
    readonly property var selectionState: wall.selection


    Theme { id: theme }

    function computeRows(cr, pr, isActive) {
        if (!isActive || typeof Collection === "undefined" || typeof Progress === "undefined") return []
        var entries = Collection.items("biblio")
        var plist = Progress.recent("book", 200)
        return Api.buildBiblioRows(entries, plist)
    }

    function keyboardIdentityAt(index) {
        var row = root.visibleRows[index]
        return row && row.entry && row.entry.id !== undefined
            ? String(row.entry.id) : ""
    }

    function keyboardIndexForIdentity(identity) {
        var wanted = String(identity || "")
        for (var i = 0; i < root.visibleRows.length; ++i) {
            if (root.keyboardIdentityAt(i) === wanted)
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
        menuFocusReturn = invoker || wall
        menuPanel.currentIndex = 0
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

    // Test seam (mirrors TankobanLibraryTab.handleCardTap): the menu items AND a direct card
    // primary-click both route through here, so the offscreen harness can exercise the action
    // logic without a real delegate/menu. action âˆˆ {"resume","detail","remove"}.
    function handleCardAction(row, action) {
        if (!row) return
        if (action === "select" && layout.touch) { selectionState.begin(row); return }
        if (selectionState.active) { selectionState.toggle(row); return }
        if (action === "resume" && row.canResume) { resumeRequested(row.progressRecord); return }
        if (action === "remove") { removeRequested(row.entry); return }
        detailRequested(row.entry)
    }
    function menuActions() {
        const out = menuRow && menuRow.canResume ? ["resume", "detail"] : ["detail"]
        if (layout.touch) out.push("select")
        out.push("remove")
        return out
    }
    function menuActionIndex(action) { return menuActions().indexOf(action) }
    function triggerMenuAction(action) {
        if (!menuRow) return
        handleCardAction(menuRow, action)
        closeMenu(true)
    }
    function triggerMenuIndex(index) {
        const actions = menuActions()
        if (index >= 0 && index < actions.length) triggerMenuAction(actions[index])
    }

    // â”€â”€ header: search + the small filter/sort bar â”€â”€
    Item {
        id: header
        objectName: "biblioLibraryHeader"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(64, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(64, theme.margin)
        height: root.phoneLayout ? 64 : 76; z: 20

        TouchTextField {
            id: searchField
            objectName: "biblioLibrarySearch"
            anchors.right: parent.right; anchors.top: root.phoneLayout ? parent.top : undefined
            anchors.verticalCenter: root.phoneLayout ? undefined : parent.verticalCenter
            width: root.phoneLayout ? parent.width : 250; height: layout.touch ? layout.touchTargetSize : 38; leftPadding: 16; rightPadding: 16
            placeholderText: "Search by title or author"; placeholderTextColor: theme.inkDimmer
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

    // â”€â”€ the quiet filter/sort bar â”€â”€
    Item {
        id: filterBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: header.bottom
        height: root.phoneLayout ? 56 : 54; z: 19

        component BiblioFilterPill: Rectangle {
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
            objectName: "biblioLibraryFilterViewport"
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

                // sort
                Repeater {
                    model: [ { key: "added", label: "Recently added" }, { key: "lastRead", label: "Last read" },
                             { key: "az", label: "A–Z" } ]
                    delegate: BiblioFilterPill {
                        required property var modelData
                        label: modelData.label; active: root.sortMode === modelData.key
                        onPicked: root.sortMode = modelData.key
                    }
                }
                Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: theme.edge }
                // state filter (All | In Progress). "Downloaded" omitted in v1 â€” no honest availability source.
                Repeater {
                    model: [ { key: "", label: "All" }, { key: "inProgress", label: "In Progress" } ]
                    delegate: BiblioFilterPill {
                        required property var modelData
                        label: modelData.label; active: root.stateFilter === modelData.key
                        onPicked: root.toggleStateFilter(modelData.key)
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
        objectName: "biblioLibraryGrid"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: root.pageFlow ? undefined : parent.top
        anchors.bottom: root.pageFlow ? undefined : parent.bottom
        y: root.pageFlow ? wallHost.windowTop : 0
        height: root.pageFlow ? wallHost.windowHeight : parent.height
        anchors.leftMargin: root.phoneLayout ? 0 : Math.max(48, theme.margin); anchors.rightMargin: root.phoneLayout ? 0 : Math.max(38, theme.margin - 10)
        model: root.visibleRows
        property bool keyboardReturnOwner: true
        property var keyboardIdentityForIndex: root.keyboardIdentityAt
        property var keyboardIndexForIdentity: root.keyboardIndexForIdentity
        // Read-only Lanista observables for the real GridView. These deliberately derive
        // from the production model/currentItem/contentItem so runtime acceptance can prove
        // virtualization and semantic focus without introducing a test-only collection.
        readonly property int automationModelCount: root.visibleRows.length
        readonly property int automationLiveDelegateCount: {
            var children = contentItem && contentItem.children ? contentItem.children : []
            var realized = 0
            for (var i = 0; i < children.length; ++i) {
                var child = children[i]
                if (child && String(child.objectName || "").indexOf("biblioLibraryCard_") === 0)
                    ++realized
            }
            return realized
        }
        readonly property string automationCurrentIdentity: root.keyboardIdentityAt(currentIndex)
        readonly property string automationCurrentItemObjectName:
            currentItem ? String(currentItem.objectName || "") : ""
        readonly property bool automationCurrentItemFullyVisible: {
            var item = currentItem
            if (!item || !item.visible || Number(item.opacity) <= 0)
                return false
            var geometryRevision = root._automationGeometryRevision(item)
            if (!isFinite(geometryRevision))
                return false
            var topLeft = item.mapToItem(wall, 0, 0)
            var bottomRight = item.mapToItem(wall, item.width, item.height)
            return topLeft.x >= -0.5 && topLeft.y >= -0.5
                && bottomRight.x <= wall.width + 0.5
                && bottomRight.y <= wall.height + 0.5
        }
        property var keyboardRevealIndex: function(index) {
            if (index < 0 || index >= root.visibleRows.length)
                return false
            if (root.pageFlow) root.revealCell(index)
            else wall.positionViewAtIndex(index, GridView.Contain)
            return true
        }
        // fixed gallery poster size (148Ã—222) matches ContinueTile / discover shelves â€” the
        // deliberate, consistent card size that reads as one family with the rest of the app.
        labelHeight: theme.fontSize(root.portraitPhoneLayout ? 54 : 48)
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
            id: wallKeys; objectName: "biblioLibraryKeyboard"; view: wall; orientation: "grid"; columns: Math.max(1, wall.columnCount)
            positionIndexFn: root.pageFlow ? root.revealCell : null
            count: root.visibleRows.length; contextEnabled: true
            identityForIndex: root.keyboardIdentityAt
            indexForIdentity: root.keyboardIndexForIdentity
            modelRevision: root.collRev * 100000 + root.progRev
            onActivated: (index) => root.handleCardAction(root.visibleRows[index], root.visibleRows[index].canResume ? "resume" : "detail")
            onContextRequested: (index) => {
                const card = wall.currentItem
                if (!card) return
                const pt = card.mapToItem(root, card.width, 0)
                root.openMenu(card.modelData, pt.x, pt.y, wall)
            }
        }

        delegate: Item {
            id: card
            objectName: "biblioLibraryCard_" + (modelData.entry.id || "")
            required property var modelData
            required property int index
            width: wall.posterW; height: wall.cellHeight - 18
            x: (wall.cellWidth - width) / 2
            readonly property real coverH: wall.posterH
            readonly property bool hovered: cardHover.hovered || root.menuRowId === card.modelData.entry.id
            readonly property bool keyboardSelected: wall.activeFocus && wall.currentIndex === card.index
            Accessible.role: selectionState.active ? Accessible.CheckBox : Accessible.Button
            Accessible.name: String(card.modelData.entry.title || "Untitled")
            Accessible.checkable: selectionState.active
            Accessible.checked: selectionState.contains(card.modelData)
            Accessible.onPressAction: root.handleCardAction(card.modelData, card.modelData.canResume ? "resume" : "detail")
            Accessible.onToggleAction: root.handleCardAction(card.modelData, "detail")
            LibrarySelectionMark {
                objectName: "biblioLibrarySelection_" + String(card.modelData.entry.id)
                visible: selectionState.active; selected: selectionState.contains(card.modelData)
                x: 4; y: 4; z: 50
            }

            // â”€â”€ two offset depth plates behind the poster (ContinueTile world grammar) â”€â”€
            Rectangle {
                x: 0; y: 3; width: card.width; height: card.coverH; radius: 13
                color: Qt.rgba(0, 0, 0, card.hovered ? 0.42 : 0.28)
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            Rectangle {
                x: -2; y: card.hovered ? 11 : 7
                width: card.width + 4; height: card.coverH; radius: 15
                color: Qt.rgba(0, 0, 0, card.hovered ? 0.20 : 0.10)
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 200 } }
            }

            // â”€â”€ the poster (cover art + hairline), one rounded mask â”€â”€
            Item {
                id: worldContent
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: card.coverH
                layer.enabled: true
                layer.effect: MultiEffect { maskEnabled: true; maskSource: worldMask; maskThresholdMin: 0.5 }

                // placeholder (title text) under any art â€” keeps the card legible before cover decodes
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
                // cover art via the shared fallback-stack primitive â€” never a raw page-local Image
                Image {
                    anchors.fill: parent
                    source: card.modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop; verticalAlignment: Image.AlignTop
                    asynchronous: true; cache: true
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                }

                // in-progress hairline â€” cover base
                Rectangle {
                    visible: card.modelData.progress > 0
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

            // â‹® button â€” addressable by name (objectName) for Lanista ui-click + harness, faded in
            // on hover like ContinueTile's remove control. The bridge addresses it by name, not
            // visibility, so the fade is purely cosmetic â€” the control stays clickable when hovered.
            CollectionCardMenu {
                id: dots
                objectName: "biblioLibraryCardMenu_" + (card.modelData.entry.id || "")
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

            // title (primary line)
            Text {
                id: cardTitle
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: worldContent.bottom; anchors.topMargin: 9
                text: card.modelData.entry.title || "Untitled"
                color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold
                height: root.portraitPhoneLayout ? Math.max(30, implicitHeight) : implicitHeight
                wrapMode: root.portraitPhoneLayout ? Text.WordWrap : Text.NoWrap
                elide: Text.ElideRight; maximumLineCount: root.portraitPhoneLayout ? 2 : 1
            }
            // author (secondary line â€” the biblio-specific card difference vs Theatre's video card)
            Text {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: root.portraitPhoneLayout ? cardTitle.bottom : worldContent.bottom
                anchors.topMargin: root.portraitPhoneLayout ? 3 : 28
                text: card.modelData.author || ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                elide: Text.ElideRight; maximumLineCount: 1
            }

            HoverHandler { id: cardHover }
            // Primary tap keeps its Resume/Details branch; a STATIONARY TOUCH long press opens
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
                    // primary click: Resume when a reliable match exists, else Details (plan §8)
                    root.handleCardAction(card.modelData,
                                         card.modelData.canResume ? "resume" : "detail")
                }
            }
        }
    }
    ScrollGlide { flick: root.pageFlow ? null : wall }

    // â”€â”€ empty states â”€â”€
    Column {
        objectName: "biblioLibraryEmptyState"
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Your library is empty"
            color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(30)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Save a book with + Library — it lands here."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(14)
        }
    }
    Column {
        objectName: "biblioLibraryNoMatchState"
        anchors.centerIn: wall; spacing: 12
        visible: root.allRows.length > 0 && root.visibleRows.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing matches"
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
        objectName: "biblioLibraryMenuPanel"
        z: 56
        visible: root.menuRow !== null
        touch: layout.touch
        viewport: root.menuViewport
        anchorX: root.menuX
        anchorY: root.menuY
        labelLeft: 14
        itemPrefix: "biblioLibraryMenuItem_"
        scrollObjectName: "biblioLibraryMenuScroll"
        items: {
            const out = []
            if (root.menuRow && root.menuRow.canResume) out.push({id:"resume",label:"Resume"})
            out.push({id:"detail",objectId:"details",label:"Details"})
            if (layout.touch) out.push({id:"select",label:"Select"})
            out.push({separator:true},{id:"remove",label:"Remove from Library",warn:true})
            return out
        }
        onPicked: (action) => root.triggerMenuAction(action)
        onCloseRequested: (restoreFocus) => root.closeMenu(restoreFocus)
    }
}
