import QtQuick
import ".."

KeyboardAction {
    id: root
    required property var pageData
    required property int trackIndex
    readonly property var track: pageData.tracks[trackIndex] || ({
            title: "",
            seconds: 0
        })
    readonly property string artistLine: pageData.trackArtists(trackIndex)
    readonly property bool playing: pageData.isPlayingTrack(trackIndex)
    objectName: "vinylTrackRow_" + trackIndex
    accessibleName: track.title
    enabled: track.available !== false
    opacity: enabled ? 1 : 0.4
    height: artistLine ? 62 : 57
    onTriggered: pageData.playTrack(trackIndex)
    Theme {
        id: theme
    }
    Rectangle {
        anchors.fill: parent
        color: theme.glassTint
        visible: root.interactionActive
        radius: 6
    }
    Text {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        text: ("0" + (root.trackIndex + 1)).slice(-2)
        color: root.playing ? theme.gold : theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 12
    }
    Column {
        x: 44
        width: parent.width - 100
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        Text {
            width: parent.width
            text: root.track.title
            color: root.playing ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 15
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: root.artistLine !== ""
            text: root.artistLine
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
    Text {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: root.pageData.formatTime(root.track.seconds)
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 12
    }
    Rectangle {
        y: parent.height - 1
        width: parent.width
        height: 1
        color: theme.edge
        opacity: 0.5
    }
    Rectangle {
        x: 44
        y: parent.height - 2
        height: 2
        width: root.pageData.duration > 0 ? (parent.width - 44) * root.pageData.position / root.pageData.duration : 0
        color: theme.gold
        visible: root.playing
    }
}
