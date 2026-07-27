// UniverseTile — one work in a universe row. 150x236 r8, index numeral, optional chip,
// 56px caption block. Every value is the inherited saga contract (One Piece plan §3.0),
// not a fresh choice: a universe page must sit in the same family as SagaUniversePage.
//
// Art by kind: video → metahub poster/small (ONLY /small is reliable; /medium 404s across
// the long tail and Cinemeta URLs must never be upscaled — house doctrine). Non-video
// kinds have no universal art endpoint, so they degrade to the honest lettered plate
// rather than borrowing a stand-in.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: tile
    property var entry: ({})
    property string kind: "video"
    property int index: 0
    signal activated()

    width: 150
    height: 236 + 56

    Theme { id: theme }

    readonly property string art: {
        if (entry.poster) return entry.poster
        if (kind === "video" && entry.id)
            return "https://images.metahub.space/poster/small/" + entry.id + "/img"
        return ""
    }

    Rectangle {
        id: plate
        width: 150; height: 236; radius: 8
        color: "#12141a"
        border.width: 1; border.color: theme.edge
        clip: true

        Image {
            id: artImage
            anchors.fill: parent
            source: tile.art
            visible: source != "" && status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 300
        }
        // The honest fallback: the work's own name, never another IP's art. Visible while
        // there is no art URL at all, or while the one Image above is still loading (status
        // passes Loading → Ready with asynchronous: true) — hidden the instant it is Ready.
        Text {
            anchors.fill: parent
            anchors.margins: 12
            visible: tile.art === "" || artImage.status !== Image.Ready
            text: tile.entry.title || ""
            color: theme.inkDimmer
            font.family: theme.display; font.pixelSize: 15
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            width: 26; height: 26; radius: 6
            x: 8; y: 8
            color: Qt.rgba(0, 0, 0, 0.62)
            Text {
                anchors.centerIn: parent
                text: tile.index + 1
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13; font.bold: true
            }
        }
        Text {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 8
            visible: !!tile.entry.note
            text: (tile.entry.note || "").toUpperCase()
            color: theme.gold
            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.activated()
        }
    }

    Column {
        anchors.top: plate.bottom
        anchors.topMargin: 10
        width: parent.width
        spacing: 2
        Text {
            width: parent.width
            text: tile.entry.title || ""
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: !!tile.entry.year
            text: tile.entry.year || ""
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11
        }
    }
}
