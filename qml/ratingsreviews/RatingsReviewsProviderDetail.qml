import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "ratingsReviewsProviderDetail"
    anchors.fill: parent
    focus: true

    property var controller: null
    property var row: ({})
    property bool spoilerRevealed: false
    readonly property string providerId: String(row && row.providerId ? row.providerId : "")
    readonly property bool sourceAvailable: controller && controller.reviewSourceAvailable
        ? controller.reviewSourceAvailable(providerId) : false

    signal backRequested()

    Rectangle { anchors.fill: parent; color: "#080b0f" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 38
        spacing: 18

        Button {
            id: back
            objectName: "ratingsReviewsProviderBack"
            text: "← Ratings && Reviews"
            activeFocusOnTab: true
            onClicked: root.backRequested()
        }

        Text {
            text: String(root.row.providerName || root.providerId || "Provider")
            color: "#f5f3ee"
            font.family: "Georgia"
            font.pixelSize: 38
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: "#0f141a"
            border.width: 1
            border.color: "#2d333d"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: String(root.row.headline || "")
                    visible: text.length > 0
                    color: "#f5f3ee"
                    font.family: "Georgia"
                    font.pixelSize: 28
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }
                Text {
                    Layout.fillWidth: true
                    text: root.row.spoiler && !root.spoilerRevealed
                          ? "Spoiler review. Reveal to read."
                          : String(root.row.excerpt || "")
                    color: "#c7cdd4"
                    font.pixelSize: 15
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                }
                Button {
                    visible: !!root.row.spoiler && !root.spoilerRevealed
                    text: "Reveal spoiler review"
                    onClicked: root.spoilerRevealed = true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: String(root.row.reviewer || root.row.sourceLabel || "")
                        color: "#8e97a3"
                        font.pixelSize: 12
                        textFormat: Text.PlainText
                    }
                    Text {
                        text: String(root.row.scoreDisplay || "")
                        color: "#e9e4d8"
                        font.pixelSize: 14
                    }
                }
                Button {
                    id: original
                    objectName: "rrOriginalSource"
                    visible: root.sourceAvailable
                    enabled: root.sourceAvailable
                    text: "Original source"
                    onClicked: if (root.controller && root.controller.openReviewSource)
                        root.controller.openReviewSource(root.providerId)
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            root.backRequested()
            event.accepted = true
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        back.forceActiveFocus(Qt.TabFocusReason)
    })
}
