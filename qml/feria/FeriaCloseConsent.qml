import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Minimal docked reference presentation; app skins may replace this item.
// It reserves viewport space and never paints on top of the website.
Item {
    id: pane
    required property var controller
    visible: controller.consent !== null
    width: visible ? Math.min(260, parent ? parent.width * 0.4 : 260) : 0
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        Label { text: qsTr("Leave this page?"); color: "white"; font.bold: true }
        Label {
            Layout.fillWidth: true
            text: pane.controller.consent ? pane.controller.consent.origin : ""
            textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: "#ccd3dd"
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Changes on this page may not be saved.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: "white"
        }
        Button {
            text: qsTr("Stay")
            onClicked: if (pane.controller.consent) pane.controller.answer(pane.controller.consent.id, false)
        }
        Button {
            text: qsTr("Leave")
            onClicked: if (pane.controller.consent) pane.controller.answer(pane.controller.consent.id, true)
        }
        Item { Layout.fillHeight: true }
    }
}
