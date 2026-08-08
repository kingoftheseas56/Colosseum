import QtQuick
import "../"   // Theme (the shared token kit lives in qml/)

Item {
    id: hero
    property var release: ({})
    property string stateLabel: ""
    property string primaryLabel: "Check for updates"
    property bool primaryEnabled: true
    property bool cancelVisible: false
    property bool reducedMotion: false
    property real progress: 0
    signal primaryClicked()
    signal cancelClicked()

    implicitHeight: heroCol.implicitHeight + 56

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: Qt.rgba(0.035, 0.04, 0.06, 0.78)
        border.width: 1
        border.color: theme.edge
    }

    Column {
        id: heroCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 28
        spacing: 12

        Text {
            text: String(hero.release.eyebrow || "COLOSSEUM UPDATE")
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 11
            font.letterSpacing: 2.6
            font.weight: Font.DemiBold
        }
        Text {
            text: String(hero.release.title || "Your Colosseum chronicle")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 42
            font.letterSpacing: -0.8
            wrapMode: Text.WordWrap
            width: parent.width
        }
        Text {
            text: String(hero.release.summary || "Colosseum will keep the house current here.")
            color: theme.inkDim
            font.family: theme.display
            font.italic: true
            font.pixelSize: 17
            lineHeight: 1.2
            wrapMode: Text.WordWrap
            width: parent.width
        }

        Row {
            spacing: 10
            Text {
                text: "RUNNING " + String(hero.release.version || "")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.letterSpacing: 1.2
            }
            Text {
                text: "→  " + String(hero.stateLabel || "")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            width: parent.width
            height: 7
            radius: 4
            color: Qt.rgba(1, 1, 1, 0.10)
            visible: hero.progress > 0 || hero.cancelVisible
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, hero.progress))
                height: parent.height
                radius: 4
                color: theme.gold
                Behavior on width {
                    enabled: !hero.reducedMotion
                    NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                }
            }
        }
        Text {
            id: progressText
            objectName: "colosseumUpdateProgress"
            text: Math.round(Math.max(0, Math.min(1, hero.progress)) * 100) + "%"
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 12
            visible: hero.progress > 0 || hero.cancelVisible
        }

        Rectangle {
            id: primary
            objectName: "colosseumUpdatePrimaryAction"
            width: Math.max(180, primaryText.implicitWidth + 48)
            height: 44
            radius: 12
            color: hero.primaryEnabled ? theme.gold : Qt.rgba(1, 1, 1, 0.10)
            border.width: hero.primaryEnabled ? 0 : 1
            border.color: theme.edge
            Accessible.role: Accessible.Button
            Accessible.name: hero.primaryLabel
            Text {
                id: primaryText
                anchors.centerIn: parent
                text: hero.primaryLabel
                color: hero.primaryEnabled ? "#141207" : theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            MouseArea {
                anchors.fill: parent
                enabled: hero.primaryEnabled
                cursorShape: Qt.PointingHandCursor
                onClicked: hero.primaryClicked()
            }
        }
        Text {
            text: "Cancel download"
            color: cancelMa.containsMouse ? theme.gold : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 12
            visible: hero.cancelVisible
            Accessible.role: Accessible.Button
            Accessible.name: text
            MouseArea {
                id: cancelMa
                objectName: "colosseumUpdateCancel"
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                onClicked: hero.cancelClicked()
            }
        }
    }
}
