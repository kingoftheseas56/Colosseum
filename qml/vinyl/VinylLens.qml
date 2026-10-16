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
    property string openFilter: ""
    implicitHeight: filterRow.y + filterRow.height + 24
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
            model: ["albums","artists","songs"]
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
        y: filterRow.y + filterRow.height + 8
        width: 250
        // Stays inside the visible page above the player bar: the height is
        // clamped to the space left in the page viewport when the menu opens.
        height: Math.min(350, options.implicitHeight + 24, menu.maxVisibleHeight)
        property real maxVisibleHeight: 350
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.forceActiveFocus()
        function clampToViewport() {
            if (!root.pageFlick) { menu.maxVisibleHeight = 350; return }
            // A popup's own y is not settled before its first open, so the
            // top is taken from the filter row the menu hangs from.
            var sceneTop = root.mapToItem(null, 0, filterRow.y + filterRow.height + 8).y
            var viewportBottom = root.pageFlick.mapToItem(null, 0, 0).y + root.pageFlick.height
            menu.maxVisibleHeight = Math.max(0, viewportBottom - sceneTop - 8)
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
                width: parent.width
                spacing: 5
                Repeater {
                    model: root.openFilter ? root.worldData.filterOptions(root.openFilter) : []
                    delegate: VinylWorldButton {
                        required property var modelData
                        width: options.width
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
