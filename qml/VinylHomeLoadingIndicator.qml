import QtQuick
import QtQuick.Controls

// Shared face for the host's continuous loader and the in-page loading/Retry state.
Column {
    id: root
    property bool indeterminate: true
    property bool showIndicator: true
    property string error: ""
    width: Math.min(420, parent.width)
    x: (parent.width - width) / 2
    spacing: 8

    Theme { id: theme }
    Text {
        objectName: "vinylHomeLoadingMessage"
        visible: root.showIndicator
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.indeterminate ? qsTr("Loading your music…") : root.error || qsTr("Could not load your music.")
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 13
    }
    ProgressBar {
        objectName: "vinylHomeLoadingBar"
        width: parent.width
        height: 6
        indeterminate: root.indeterminate
        visible: root.showIndicator && indeterminate
        palette.dark: theme.gold
        palette.midlight: "#1affffff"
        Accessible.name: qsTr("Loading your music")
    }
}
