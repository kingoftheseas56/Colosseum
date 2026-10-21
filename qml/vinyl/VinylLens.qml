import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    objectName: "vinylLens"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property string title: worldData.lensTitle
    readonly property bool onlineMode: worldData.onlineMode === true
    property string openFilter: ""
    implicitHeight: filterRow.visible
        ? filterRow.y + filterRow.height + 24
        : divider.y + divider.height + 24
    onWidthChanged: if (menu.opened) Qt.callLater(menu.clampToViewport)
    Connections {
        target: root.pageFlick
        function onHeightChanged() { if (menu.opened) Qt.callLater(menu.clampToViewport) }
    }
    Connections {
        target: root.worldData
        ignoreUnknownSignals: true
        function onOnlineModeChanged() {
            if (root.onlineMode && menu.opened) menu.close();
            else if (!root.onlineMode && root.worldData.lens.kind === "playlists"
                    && typeof root.worldData.setKind === "function")
                root.worldData.setKind("albums");
        }
    }
    Theme { id: theme }
    Text {
        id: heading
        y: root.width<800 ? 62 : 0
        objectName: "vinylLensTitle"
        anchors.right: parent.right
        text: root.title
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 44
        width: root.width<800 ? parent.width : parent.width * 0.67
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
    Row {
        y: 14
        spacing: 8
        Repeater {
            model: root.onlineMode ? ["albums","artists","songs","playlists"] : ["albums","artists","songs"]
            delegate: VinylWorldButton {
                required property string modelData
                objectName: "vinylKind_" + modelData
                label: modelData.charAt(0).toUpperCase()+modelData.slice(1)
                selected: root.worldData.lens.kind === modelData
                backdrop: root.backdrop
                enabled: root.worldData.catalogueReachable
                onTriggered: root.worldData.setKind(modelData)
            }
        }
    }
    Rectangle { id: divider; y: Math.max(80,heading.y+heading.height+18); width: parent.width; height: 1; color: theme.edge; opacity: 0.5 }
    Flow {
        width: parent.width
        id: filterRow
        y: divider.y + 22
        spacing: 12
        visible: !root.onlineMode
        Repeater {
            model: ["genre","decade","country","language"]
            delegate: VinylWorldButton {
                id: filter
                required property string modelData
                objectName: "vinylFilter_" + modelData
                label: modelData.charAt(0).toUpperCase()+modelData.slice(1)+" · "+(root.worldData.lens[modelData] || "All")
                selected: !!root.worldData.lens[modelData]
                backdrop: root.backdrop
                enabled: root.worldData.catalogueReachable
                onTriggered: {
                    root.openFilter=modelData
                    menu.focusReturn=filter
                    menu.x=filter.mapToItem(root,0,0).x
                    menu.clampToViewport()
                    menu.open()
                }
            }
        }
    }
    Popup {
        id: menu
        objectName: "vinylFilterMenu"
        y: opensAbove ? filterRow.y - 8 - height : filterRow.y + filterRow.height + 8
        width: 250
        // Stays inside the visible page above the player bar: the height is
        // clamped to the space left in the page viewport when the menu opens.
        height: Math.min(350, options.implicitHeight + 24, menu.maxVisibleHeight)
        property real maxVisibleHeight: 350
        property bool opensAbove: false
        property Item focusReturn: null
        property bool keyboardEntry: false
        focus: true
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: keyboardEntry=!!focusReturn && focusReturn.activeFocus
        onOpened: {
            if (!keyboardEntry) { options.forceActiveFocus(Qt.PopupFocusReason); return; }
            focusOption();
        }
        function focusOption() {
            for (var i=0;i<optionButtons.count;i++) {
                var option=optionButtons.itemAt(i)
                if (option && option.enabled && option.selected) {
                    option.forceActiveFocus(Qt.TabFocusReason)
                    return
                }
            }
            var first=optionButtons.itemAt(0)
            if (first) first.forceActiveFocus(Qt.TabFocusReason)
        }
        onClosed: {
            if (keyboardEntry && focusReturn && focusReturn.visible && focusReturn.enabled)
                focusReturn.forceActiveFocus(Qt.TabFocusReason)
            else root.forceActiveFocus(Qt.OtherFocusReason)
        }
        function clampToViewport() {
            if (!root.pageFlick) { menu.maxVisibleHeight = 350; return }
            // A popup's own y is not settled before its first open, so the
            // top is taken from the filter row the menu hangs from.
            var sceneTop = root.mapToItem(null, 0, filterRow.y + filterRow.height + 8).y
            var viewportTop = root.pageFlick.mapToItem(null, 0, 0).y
            var viewportBottom = viewportTop + root.pageFlick.height
            var below = Math.max(0, viewportBottom - sceneTop - 8)
            var above = Math.max(0, root.mapToItem(null,0,filterRow.y).y - viewportTop - 8)
            // Keep the existing below-trigger layout unless it cannot show one
            // complete option; then use the larger space above, still scrollable.
            menu.opensAbove = below < 38 + 2*menu.padding && above > below
            menu.maxVisibleHeight = menu.opensAbove ? above : below
        }
        background: Glass { backdrop: root.backdrop; radius: 16; tint: 0.12; scrim: 0.64 }
        contentItem: Flickable {
            id: menuScroll
            objectName: "vinylFilterMenuScroll"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: options.implicitHeight
            Column {
                id: options
                Keys.onDownPressed: menu.focusOption()
                Keys.onUpPressed: menu.focusOption()
                width: parent.width
                spacing: 5
                Repeater {
                    id: optionButtons
                    model: root.openFilter ? root.worldData.filterOptions(root.openFilter) : []
                    delegate: VinylWorldButton {
                        required property var modelData
                        required property int index
                        width: options.width
                        KeyNavigation.up: index>0 ? optionButtons.itemAt(index-1) : null
                        KeyNavigation.down: index+1<optionButtons.count ? optionButtons.itemAt(index+1) : null
                        onActiveFocusChanged: if (activeFocus) {
                            if (y < menuScroll.contentY) menuScroll.contentY=y
                            else if (y+height > menuScroll.contentY+menuScroll.height)
                                menuScroll.contentY=y+height-menuScroll.height
                        }
                        objectName: "vinylFilterValue_" + root.openFilter + "_" + modelData.value
                        label: modelData.label
                        enabled: modelData.enabled && root.worldData.catalogueReachable
                        selected: root.worldData.lens[root.openFilter] === modelData.value
                        backdrop: root.backdrop
                        onTriggered: { root.worldData.setLens(root.openFilter,modelData.value); menu.close() }
                    }
                }
            }
        }
    }
}
