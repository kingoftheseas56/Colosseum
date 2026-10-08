import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Docked chrome: the parent reserves this height outside the website viewport.
Rectangle {
    id: root
    required property var deck
    property var consent: null
    property string diagnostic: ""
    height: consent ? 100 : diagnostic.length ? 100 : 0
    visible: consent !== null || diagnostic.length > 0
    color: "#202936"
    border.color: "#637083"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8
        Label {
            Layout.fillWidth: true
            color: "white"
            text: root.consent ? root.consent.origin + " has unsaved changes. Leave this space?" : root.diagnostic
            wrapMode: Text.WordWrap
            maximumLineCount: root.consent ? 2 : 4
        }
        RowLayout {
            visible: root.consent !== null
            Label { Layout.fillWidth: true; color: "#c8d2df"; elide: Text.ElideMiddle
                text: root.consent ? root.consent.identity.spaceId : "" }
            Button { text: "Stay"; onClicked: if (root.consent) root.deck.answerClose(root.consent.identity.spaceId, root.consent.attempt, false) }
            Button { text: "Leave"; onClicked: if (root.consent) root.deck.answerClose(root.consent.identity.spaceId, root.consent.attempt, true) }
        }
    }
}
