import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylPlayerBar"
    required property var pageData
    required property Item backdrop
    property string page: "album"
    readonly property int toolCount: page === "song" ? 6 : 1
    readonly property bool busy: pageData.sourceState === "finding" || pageData.sourceState === "retrying"
    readonly property real seekFraction: pageData.duration > 0 ? pageData.position / pageData.duration : 0
    signal songRequested
    signal creditsRequested
    Theme {
        id: theme
    }
    height: 72
    Glass {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: 18
        tint: 0.09
        scrim: 0.08
    }
    function iconSvg(key, selected) {
        var paths = {
            "♩": "<rect x=\"7.2\" y=\"2.4\" width=\"5.6\" height=\"9.2\" rx=\"2.8\"/><path d=\"M4.4 9.4a5.6 5.6 0 0 0 11.2 0M10 15v2.8M7.2 17.8h5.6\"/>",
            "▹": "<rect x=\"2.4\" y=\"4.2\" width=\"15.2\" height=\"11.6\" rx=\"2.4\"/><path d=\"M8.4 7.6v4.8l4-2.4z\"/>",
            "≡": "<path d=\"M3 4.6h14v9H9.4L6 16.8v-3.2H3z\"/><path d=\"M6.4 7.8h7.2M6.4 10.4h4.6\"/>",
            "♙": "<circle cx=\"10\" cy=\"7\" r=\"3.2\"/><path d=\"M3.8 17c.6-3.2 3-4.8 6.2-4.8s5.600 1.600 6.200 4.800\"/>",
            "☷": "<path d=\"M3.400 5.400h9.200M3.400 10h9.200M3.400 14.600h5.600M13.600 12v5.200l3.800-2.600z\"/>"
        };
        return "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" stroke="' + (selected ? theme.gold : theme.inkDim) + '" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">' + paths[key] + '</svg>');
    }
    component Tool: KeyboardAction {
        id: control
        property string glyph
        property bool selected: false
        width: 34
        height: 34
        opacity: enabled ? 1 : 0.34
        Rectangle {
            anchors.fill: parent
            radius: 10
            color: control.interactionActive || control.selected ? theme.glassHi : theme.glassTint
            border.color: theme.edge
        }
        Image {
            anchors.centerIn: parent
            width: 17
            height: 17
            source: root.iconSvg(control.glyph, control.selected)
        }
        ToolTip.visible: hovered
        ToolTip.text: accessibleName
    }
    KeyboardAction {
        objectName: "vinylBarOpenSong"
        anchors.fill: parent
        enabled: root.page === "album"
        accessibleName: "Open playing song"
        onTriggered: root.songRequested()
    }
    Image {
        id: cover
        x: 12
        y: 13
        width: 46
        height: 46
        source: root.pageData.artwork(root.pageData.playingEdition.cover)
        fillMode: Image.PreserveAspectCrop
    }
    KeyboardAction {
        objectName: "vinylBarNowPlaying"
        x: 70
        y: 12
        width: Math.max(120, root.width * 0.25 - 75)
        height: 48
        accessibleName: "Open playing song"
        onTriggered: root.songRequested()
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            spacing: 3
            Text {
                width: parent.width
                text: root.pageData.playingTrack.title
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.pageData.playingEdition.artist + " · " + root.pageData.playingEdition.name
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        spacing: 12
        KeyboardAction {
            objectName: "vinylBarPrev"
            width: 32
            height: 36
            accessibleName: "Previous track"
            onTriggered: root.pageData.next(-1)
            Text {
                anchors.centerIn: parent
                text: "‹‹"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 17
            }
        }
        KeyboardAction {
            objectName: "vinylBarPlay"
            width: 36
            height: 36
            accessibleName: root.busy ? "Waiting for source" : "Play or pause"
            enabled: !root.busy
            onTriggered: root.pageData.paused = !root.pageData.paused
            Rectangle {
                anchors.fill: parent
                radius: 18
                color: theme.gold
            }
            Text {
                anchors.centerIn: parent
                text: root.pageData.paused ? "▶" : "Ⅱ"
                visible: !root.busy
                color: theme.biblioWashBottom
                font.family: theme.ui
                font.pixelSize: 17
            }
            BusyIndicator {
                anchors.fill: parent
                running: root.busy
                visible: root.busy
            }
        }
        KeyboardAction {
            objectName: "vinylBarNext"
            width: 32
            height: 36
            accessibleName: "Next track"
            onTriggered: root.pageData.next(1)
            Text {
                anchors.centerIn: parent
                text: "››"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 17
            }
        }
    }
    Item {
        id: seek
        objectName: "vinylBarSeek"
        anchors.horizontalCenter: parent.horizontalCenter
        y: 48
        width: root.width * 0.31
        height: 22
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 3
            color: theme.edge
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * root.seekFraction
            height: 3
            color: theme.gold
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => root.pageData.seek(mouse.x / width * root.pageData.duration)
        }
        Text {
            anchors.right: parent.left
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: root.pageData.formatTime(root.pageData.position)
            font.family: theme.ui
            font.pixelSize: 9
            color: theme.inkDimmer
        }
        Text {
            anchors.left: parent.right
            anchors.leftMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: root.pageData.formatTime(root.pageData.duration)
            font.family: theme.ui
            font.pixelSize: 9
            color: theme.inkDimmer
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        Tool {
            objectName: "vinylBarLyrics"
            visible: root.page === "song"
            glyph: "♩"
            accessibleName: "Lyrics"
            enabled: root.pageData.hasLyrics
            selected: root.pageData.lyricsShown
            onTriggered: root.pageData.toggleLyrics()
        }
        Tool {
            objectName: "vinylBarVideo"
            visible: root.page === "song"
            glyph: "▹"
            accessibleName: "Video"
            enabled: root.pageData.hasVideo
            selected: root.pageData.videoShown
            onTriggered: root.pageData.toggleVideo()
        }
        Tool {
            objectName: "vinylBarNotes"
            visible: root.page === "song"
            glyph: "≡"
            accessibleName: "Annotations"
            enabled: root.pageData.hasNotes
            selected: root.pageData.autoNotes
            onTriggered: root.pageData.toggleNotes()
        }
        Item {
            width: 2
            height: 1
            visible: root.page === "song"
        }
        Tool {
            objectName: "vinylBarCredits"
            visible: root.page === "song"
            glyph: "♙"
            accessibleName: "Credits"
            enabled: root.pageData.playingCredits === undefined || root.pageData.playingCredits.length > 0
            onTriggered: root.creditsRequested()
        }
        Tool {
            objectName: "vinylBarQueue"
            glyph: "☷"
            accessibleName: "Queue"
            onTriggered: root.pageData.queueRequested()
        }
        Rectangle {
            width: root.busy ? 144 : 99
            height: 34
            radius: 10
            color: theme.glassTint
            border.color: theme.edge
            Text {
                id: chip
                objectName: "vinylBarChip"
                anchors.centerIn: parent
                text: root.pageData.sourceState === "finding" ? "Finding a source" : root.pageData.sourceState === "retrying" ? "Trying another source" : (root.pageData.qualityLabel !== undefined ? root.pageData.qualityLabel : "AAC · 256 kbps")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 10
            }
        }
    }
}
