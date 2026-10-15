import QtQuick

Item {
    id: root

    required property var controller
    readonly property real u: controller.unit
    readonly property bool searchActive: controller.viewState === "search"
    readonly property int resultCount: searchActive ? controller.visibleResults().length : 0
    readonly property bool hasSelection: searchActive && controller.searchResultIndex >= 0
    readonly property bool selectedResult: hasSelection && controller.searchResultIndex < resultCount

    PorticoCombinedParityBottomChrome {
        anchors.fill: parent
        controller: root.controller
    }

    Rectangle {
        visible: root.searchActive
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1.35 * u
        height: 2.6 * u
        width: hintRow.implicitWidth + 2.2 * u
        radius: height / 2
        color: Qt.rgba(5/255, 5/255, 8/255, 0.98)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        z: 90

        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: 1.05 * u

            Repeater {
                model: root.hasSelection
                       ? [{k:"↑↓", l:"Choose"},
                          {k:"Enter", l:root.selectedResult ? "Open result" : "Search app"},
                          {k:"Esc", l:"Back"}]
                       : [{k:"↑↓", l:"Choose"},
                          {k:"Esc", l:"Back"}]

                delegate: Row {
                    required property var modelData
                    spacing: 0.4 * u

                    Rectangle {
                        width: keyText.implicitWidth + 0.7 * u
                        height: 1.45 * u
                        radius: 0.35 * u
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.22)

                        Text {
                            id: keyText
                            anchors.centerIn: parent
                            text: modelData.k
                            color: controller.mist
                            font.family: controller.uiFont
                            font.pixelSize: 0.72 * u
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.l
                        color: controller.mist
                        font.family: controller.uiFont
                        font.pixelSize: 0.82 * u
                    }
                }
            }
        }
    }
}
