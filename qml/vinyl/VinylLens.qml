import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    objectName: "vinylLens"
    required property var worldData
    required property Item backdrop
    readonly property string title: worldData.lensTitle
    property string openFilter: ""
    implicitHeight: filterRow.y + filterRow.height + 24
    Theme { id: theme }
    Text {
        id: heading
        objectName: "vinylLensTitle"
        anchors.right: parent.right
        text: root.title
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 44
        width: parent.width * 0.67
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
    Rectangle { id: divider; y: Math.max(80,heading.height+18); width: parent.width; height: 1; color: theme.edge; opacity: 0.5 }
    Row {
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
                    menu.open()
                }
            }
        }
    }
    Popup {
        id: menu
        y: filterRow.y + filterRow.height + 8
        width: 250
        height: Math.min(350,options.implicitHeight+24)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.forceActiveFocus()
        background: Glass { backdrop: root.backdrop; radius: 16; tint: 0.12; scrim: 0.64 }
        contentItem: Flickable {
            clip: true
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
            ScrollBar.vertical: HouseScrollBar { flick: parent }
        }
    }
}
