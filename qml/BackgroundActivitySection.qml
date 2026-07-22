// qml/BackgroundActivitySection.qml
import QtQuick

// Unified rows for long-running background jobs published into
// BackgroundActivityRegistry (context property `BackgroundActivity`):
// guided comic analysis, audiobook text sync, whatever lands next.
// Prop-driven (registry injected) so harnesses can feed a fake.
Column {
    id: root
    property var registry: null
    readonly property var rows: registry ? registry.activities : []
    readonly property int rowCount: rows.length
    visible: rows.length > 0
    spacing: 6

    Text {
        text: "Background activity"
        color: "#9a9a9a"
        font.pixelSize: 12
    }

    Repeater {
        model: root.rows
        delegate: Item {
            width: root.width
            height: 44

            Column {
                anchors.left: parent.left
                anchors.right: controlT.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: modelData.title + "  ·  " + modelData.stage
                    color: "#e8e8e8"
                    font.pixelSize: 13
                }
                Rectangle {
                    width: parent.width
                    height: 3
                    radius: 1
                    color: "#2a2a2a"
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, modelData.progress))
                        height: parent.height
                        radius: parent.radius
                        color: "#c9c9c9"
                    }
                }
            }

            Text {
                id: controlT
                visible: modelData.canPause === true
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.paused ? "Resume" : "Pause"
                color: "#bdbdbd"
                font.pixelSize: 12

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    onClicked: modelData.paused
                               ? root.registry.requestResume(modelData.id)
                               : root.registry.requestPause(modelData.id)
                }
            }
        }
    }
}
