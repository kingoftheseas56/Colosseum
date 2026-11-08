import QtQuick

Rectangle {
    id: chip
    property bool branded: false
    readonly property alias text: label.text
    implicitWidth: label.implicitWidth + 16
    implicitHeight: 21
    radius: 6
    color: chip.branded ? Qt.alpha(theme.nuvioAccent, 0.16) : Qt.alpha(theme.ink, 0.08)
    border.color: chip.branded ? theme.nuvioAccent : theme.edge
    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text

    Theme { id: theme }

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.branded ? qsTr("NUVIO") : qsTr("DIRECT")
        color: chip.branded ? theme.nuvioAccent : theme.ink
        font.family: theme.ui
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        Accessible.ignored: true
    }
}
