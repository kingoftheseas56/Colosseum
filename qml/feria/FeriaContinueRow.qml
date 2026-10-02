import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property var items: controller.continueItems.filter(function(row) {
        return controller.lens === "all" || (controller.lens === "read" ? row.kind === "book"
            : controller.lens === "listen" ? row.kind === "audio" : row.kind === "video")
    })
    visible: items.length > 0
    spacing: u
    height: visible ? implicitHeight + 2 * u : 0
    Text {
        text: "Continue"
        color: controller.ink; font.family: controller.displayFont; font.pixelSize: 2 * root.u
    }
    ListView {
        id: list
        width: parent.width; height: 10 * root.u
        orientation: ListView.Horizontal; spacing: root.u; clip: true
        model: root.items
        ScrollBar.horizontal: ScrollBar {}
        delegate: Rectangle {
            required property var modelData
            width: 24 * root.u; height: 8.8 * root.u; radius: root.u
            color: Qt.rgba(1,1,1,0.05)
            border.color: Qt.rgba(1,1,1,0.13)
            ColumnLayout {
                anchors.fill: parent; anchors.margins: root.u; spacing: 0.4 * root.u
                Text {
                    Layout.fillWidth: true; text: modelData.title; elide: Text.ElideRight
                    color: controller.ink; font.family: controller.uiFont; font.pixelSize: root.u
                }
                Text {
                    text: controller.providerName(modelData.pk) + " · " + (modelData.kind === "book" ? (modelData.duration > 0 ? Math.round(modelData.position) + "% · Saved reading place" : "Saved reading place")
                        : controller.durationText(modelData.position / 60) + " / " + controller.durationText(modelData.duration / 60))
                    color: controller.mist; font.pixelSize: 0.8 * root.u
                }
                ProgressBar {
                    Layout.fillWidth: true; visible: modelData.duration > 0
                    value: modelData.duration > 0 ? modelData.position / modelData.duration : 0
                }
                RowLayout {
                    Button {
                        objectName: "feriaContinueResume-" + modelData.id
                        text: modelData.kind === "book" ? "Continue reading" : modelData.kind === "audio" ? "Continue listening" : "Continue watching"
                        onClicked: controller.resumeSession(modelData)
                    }
                    ToolButton {
                        text: "×"; Accessible.name: "Remove " + modelData.title + " from Continue"
                        onClicked: controller.accountStore.dismissContinue(modelData.id)
                    }
                }
            }
        }
    }
}
