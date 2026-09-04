import QtQuick

// AF2 Cast row — faces where the source gives them (AniList character art),
// initialed monograms where it doesn't (Cinemeta is name-only). Hides when empty.
// people: [{name, role, image}]. Slide-in per house Behavior convention.
Column {
    id: castRow
    property var people: []
    property bool expanded: false
    visible: people.length > 0
    spacing: 16
    opacity: people.length > 0 ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: castRow.people.length > 0 ? 0 : 24   // AF2 slide-in: rows glide up as data lands
        Behavior on y { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
    }

    Theme { id: theme }

    function initials(name) {
        var p = String(name || "").trim().split(/\s+/)
        return ((p[0] ? p[0][0] : "") + (p.length > 1 ? p[p.length - 1][0] : "")).toUpperCase()
    }

    Text {
        text: "CAST"
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.5
    }
    Flow {
        width: parent.width
        spacing: 28
        Repeater {
            model: castRow.expanded ? castRow.people : castRow.people.slice(0, 8)
            Column {
                id: faceCol
                required property var modelData
                width: 96
                spacing: 9
                Rectangle {
                    width: 78; height: 78; radius: 39
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: faceCol.modelData.image || ""
                        visible: (faceCol.modelData.image || "") !== ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: (faceCol.modelData.image || "") === ""
                        text: castRow.initials(faceCol.modelData.name)
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 22; font.weight: Font.DemiBold
                    }
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: faceCol.modelData.name || ""; elide: Text.ElideRight
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: faceCol.modelData.role || ""; elide: Text.ElideRight
                    visible: (faceCol.modelData.role || "") !== ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                }
            }
        }
        Column {
            width: 96
            spacing: 9
            visible: !castRow.expanded && castRow.people.length > 8
            Rectangle {
                width: 78; height: 78; radius: 39
                anchors.horizontalCenter: parent.horizontalCenter
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                Text { anchors.centerIn: parent; text: "›"; color: theme.inkDim
                       font.family: theme.ui; font.pixelSize: 22 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: castRow.expanded = true }
                KeyboardAction {
                    id: expandKeyboard
                    anchors.fill: parent
                    pointerEnabled: false
                    accessibleName: "Show all cast"
                    focusRadius: 39
                    onTriggered: castRow.expanded = true
                }
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter
                text: "All cast"; color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 12
            }
        }
    }
}
