import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylPlayerBar"
    required property var pageData
    required property Item backdrop
    property string page: "album"
    property bool queueEnabled: true
    property bool trackNavigationEnabled: true
    readonly property int toolCount: page === "song" ? 6 : 1
    readonly property bool busy: pageData.sourceState === "finding" || pageData.sourceState === "retrying"
    property bool scrubbing: false
    property real previewPosition: 0
    property string scrubTrackKey: ""
    readonly property string currentSeekTrackKey: {
        var active = pageData.activeQueueRow || ({})
        var edition = pageData.playingEdition || ({})
        var track = pageData.playingTrack || ({})
        var occurrence = active.queueId !== undefined ? active.queueId
            : (pageData.queueIndex !== undefined ? pageData.queueIndex : "")
        var editionKey = edition.id !== undefined ? edition.id : pageData.playingEditionId
        var trackKey = track.id !== undefined ? track.id : pageData.playingIndex
        return [String(occurrence), String(editionKey || ""), String(trackKey || "")].join("\u001f")
    }
    readonly property real displayedPosition: scrubbing ? clampSeek(previewPosition) : clampSeek(pageData.position)
    readonly property real seekFraction: pageData.duration > 0 ? displayedPosition / pageData.duration : 0
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
            "☷": "<path d=\"M3.400 5.400h9.200M3.400 10h9.200M3.400 14.600h5.600M13.600 12v5.200l3.800-2.600z\"/>",
            "torrent": "<path d=\"M4 3v7a6 6 0 0 0 12 0V3h-4v7a2 2 0 0 1-4 0V3z\"/><path d=\"M4 6h4M12 6h4\"/>",
            "youtube": "<rect x=\"2\" y=\"4\" width=\"16\" height=\"12\" rx=\"3\"/><path d=\"m8 7 5 3-5 3z\"/>",
            "local": "<circle cx=\"10\" cy=\"10\" r=\"7\"/><circle cx=\"10\" cy=\"10\" r=\"1.7\"/><path d=\"M5 10a5 5 0 0 1 5-5\"/>",
            "unknown": "<path d=\"M8 14V4l8-1v9M8 7l8-1\"/><ellipse cx=\"5.5\" cy=\"14\" rx=\"2.5\" ry=\"2\"/><ellipse cx=\"13.5\" cy=\"12\" rx=\"2.5\" ry=\"2\"/>"
        };
        return "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" stroke="' + (selected ? theme.gold : theme.inkDim) + '" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">' + paths[key] + '</svg>');
    }
    function clampSeek(seconds) {
        var duration = Number(pageData.duration)
        var target = Number(seconds)
        if (!isFinite(duration) || duration <= 0) return 0
        if (!isFinite(target)) target = 0
        return Math.max(0, Math.min(duration, target))
    }
    function positionAt(x) {
        var duration = Number(pageData.duration)
        return seek.width > 0 && isFinite(duration) && duration > 0
            ? clampSeek(Number(x) / seek.width * duration) : 0
    }
    function beginScrub(x) {
        if (!seek.enabled) return
        scrubTrackKey = currentSeekTrackKey
        previewPosition = positionAt(x)
        scrubbing = true
    }
    function moveScrub(x) {
        if (!scrubbing) return
        if (!seek.enabled || scrubTrackKey !== currentSeekTrackKey) {
            cancelScrub()
            return
        }
        previewPosition = positionAt(x)
    }
    function cancelScrub() {
        scrubbing = false
        scrubTrackKey = ""
    }
    function commitScrub(x) {
        if (!scrubbing) return
        moveScrub(x)
        if (!scrubbing) return
        var target = clampSeek(previewPosition)
        var accepted = seek.enabled && scrubTrackKey === currentSeekTrackKey
        cancelScrub()
        if (accepted) pageData.seek(target)
    }
    onCurrentSeekTrackKeyChanged: if (scrubbing && scrubTrackKey !== currentSeekTrackKey) cancelScrub()
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.scrubbing
        onActivated: root.cancelScrub()
    }
    component Tool: KeyboardAction {
        id: control
        property string glyph
        property bool selected: false
        focusOnPointer: false
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
    }
    KeyboardAction {
        objectName: "vinylBarOpenSong"
        focusOnPointer: false
        x: 12
        y: 13
        width: 46
        height: 46
        focusRadius: 6
        enabled: root.page !== "song" && (root.pageData.playingIndex === undefined || root.pageData.playingIndex >= 0)
        accessibleName: "Open playing song"
        onTriggered: root.songRequested()
        Image {
            id: cover
            anchors.fill: parent
            source: root.pageData.artwork(root.pageData.playingEdition.cover)
            fillMode: Image.PreserveAspectCrop
        }
    }
    Item {
        objectName: "vinylBarNowPlaying"
        x: 70
        y: 12
        width: Math.max(120, root.width * 0.25 - 75)
        height: 48
        Accessible.role: Accessible.StaticText
        Accessible.name: root.pageData.playingTrack.title
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
            focusOnPointer: false
            enabled: root.trackNavigationEnabled && (root.pageData.canPrevious === undefined || root.pageData.canPrevious)
            opacity: enabled ? 1 : 0.35
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
            focusOnPointer: false
            focusRadius: 18
            focusPrimary: true
            width: 36
            height: 36
            accessibleName: root.busy ? "Waiting for source" : "Play or pause"
            enabled: !root.busy && (root.pageData.playingIndex === undefined || root.pageData.playingIndex >= 0)
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
            focusOnPointer: false
            enabled: root.trackNavigationEnabled && (root.pageData.canNext === undefined || root.pageData.canNext)
            opacity: enabled ? 1 : 0.35
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
        activeFocusOnTab: true
        enabled: !root.busy && root.pageData.duration > 0 && root.pageData.sourceState === "playing"
        onEnabledChanged: if (!enabled) root.cancelScrub()
        Accessible.role: Accessible.Slider
        Accessible.name: "Playback position"
        Accessible.description: "Left and right seek five seconds; Home and End seek to the beginning or end."
        Accessible.focusable: enabled
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape && root.scrubbing) {
                root.cancelScrub()
                event.accepted = true
                return
            }
            var target = root.pageData.position;
            if (event.key === Qt.Key_Left) target -= 5;
            else if (event.key === Qt.Key_Right) target += 5;
            else if (event.key === Qt.Key_Home) target = 0;
            else if (event.key === Qt.Key_End) target = root.pageData.duration;
            else return;
            root.pageData.seek(Math.max(0, Math.min(root.pageData.duration, target)));
            event.accepted = true;
        }
        FocusRing { anchors.fill: parent; shown: seek.activeFocus; radius: 5 }
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
            objectName: "vinylBarSeekPointer"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onEnabledChanged: if (!enabled) root.cancelScrub()
            onPressed: mouse => root.beginScrub(mouse.x)
            onPositionChanged: mouse => root.moveScrub(mouse.x)
            onReleased: mouse => root.commitScrub(mouse.x)
            onCanceled: root.cancelScrub()
        }
        Text {
            objectName: "vinylBarElapsed"
            anchors.right: parent.left
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: root.pageData.formatTime(root.displayedPosition)
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
            enabled: root.queueEnabled
            glyph: "☷"
            accessibleName: "Queue"
            onTriggered: root.pageData.queueRequested()
        }
        Item {
            id: sourceIndicator
            // Passive provenance symbol; labels are accessibility-only.
            objectName: "vinylBarSourceIndicator"
            readonly property string kind: root.pageData.sourceKind || ""
            readonly property string sourceName: kind==="torrent" ? "Torrent" : kind==="youtube" ? "YouTube" : kind==="local" ? "Local file" : "Audio source"
            readonly property string description: root.busy ? (root.pageData.sourceState==="retrying" ? "Trying another source" : "Finding a source") : sourceName+(kind==="youtube" && root.pageData.qualityLabel ? " · "+root.pageData.qualityLabel : "")
            width: 28
            height: 34
            Accessible.role: Accessible.StaticText
            Accessible.name: sourceIndicator.description
            Image {
                anchors.centerIn: parent
                width: 18;height:18
                source: root.iconSvg(["torrent","youtube","local"].indexOf(sourceIndicator.kind)>=0 ? sourceIndicator.kind : "unknown",false)
                opacity:0.8;visible:!root.busy
            }
            BusyIndicator {anchors.centerIn:parent;width:22;height:22;running:root.busy;visible:root.busy}
        }
    }
}
