import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylPlayerBar"
    required property var pageData
    required property Item backdrop
    property var songPageData: null
    readonly property var songData: songPageData || pageData
    property bool videoFullscreen: false
    property bool showSongPlay: false
    property bool allowDismiss: false
    readonly property int playingTrackIndex: pageData.playingIndex === undefined ? -1 : pageData.playingIndex
    readonly property bool hasPlayingSong: pageData.playingIndex === undefined || pageData.playingIndex >= 0
    readonly property var displayedSong: pageData
    property string page: "album"
    property bool queueEnabled: true
    property bool trackNavigationEnabled: true
    readonly property bool androidBar: Qt.platform.os === "android"
    readonly property real transportCenter: androidBar ? width / 2
        : narrow ? width - 18 - transport.width / 2 : width / 2
    readonly property bool busy: pageData.sourceState === "finding" || pageData.sourceState === "retrying" || pageData.sourceState === "resolving"
    readonly property bool narrow: width < 900
    readonly property bool torrentBusy: pageData.torrentBusy === true
    readonly property bool videoBusy: pageData.videoBusy === true
    readonly property bool videoShown: pageData.videoShown === true
    readonly property bool localSong: pageData.sourceKind === "local"
        || pageData.playingEdition.origin === "local" || !!pageData.playingTrack.localPath
    readonly property bool defaultYouTubeChoice: hasPlayingSong && !localSong && !videoShown
        && !videoBusy && !pageData.sourceKind
    readonly property bool sourceReady: pageData.sourceState === "playing" && pageData.loaded !== false && !videoShown && !videoBusy
    readonly property bool sourceSelectionEnabled: hasPlayingSong && !localSong && !videoShown && !videoBusy && (sourceReady || torrentBusy)
    readonly property bool showDismiss: allowDismiss && page !== "song" && hasPlayingSong
    readonly property real toolsLeft: androidBar ? 18 : width - 281 - (showDismiss ? 32 : 0)
    readonly property real toolsTop: androidBar ? 129 : narrow ? 74 : 14
    readonly property string androidSourceStatus: {
        if (!root.hasPlayingSong) return "";
        var error = String(pageData.torrentError || pageData.videoError || "");
        if (error) return error;
        if (videoBusy) return qsTr("Switching playback mode");
        if (pageData.sourceState === "finding") return qsTr("Finding a source");
        if (pageData.sourceState === "retrying") return qsTr("Trying another source");
        if (pageData.sourceState === "resolving") return qsTr("Resolving audio");
        if (busy || pageData.loaded === false) return qsTr("Waiting for source");
        if (showSongPlay) return qsTr("Play viewed song");
        if (localSong) return qsTr("Local file");
        var source = String(pageData.sourceKind || "");
        var quality = String(pageData.qualityLabel || "");
        if (source === "torrent") return qsTr("Torrent") + (quality ? " · " + quality : "");
        if (source === "youtube") return qsTr("YouTube") + (quality ? " · " + quality : "");
        if (root.defaultYouTubeChoice) return qsTr("YouTube (default, unconfirmed)");
        return qsTr("Audio source");
    }
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
    signal closeRequested()
    signal songRequested
    signal videoRequested()
    signal videoFullscreenRequested()
    signal selectedSongPlayRequested()
    Theme {
        id: theme
    }
    height: androidBar ? 236 : narrow ? 164 : 104
    Glass {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: root.narrow ? 18 : 21
        tint: 0.10
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
        tooltipText: accessibleName
        androidTooltipPolicy: "longPress"
        id: control
        property string glyph
        property string symbol: ""
        property bool loading: false
        property bool selected: false
        focusOnPointer: false
        width: 40
        height: 40
        opacity: enabled ? 1 : 0.34
        Rectangle {
            anchors.fill: parent
            radius: 10
            color: control.interactionActive || control.selected ? theme.glassHi : "transparent"
            border.color: "transparent"
        }
        Image {
            anchors.centerIn: parent
            width: 17
            height: 17
            visible: !control.symbol.length && !control.loading
            source: visible ? root.iconSvg(control.glyph, control.selected) : ""
        }
        VinylControlIcon {
            objectName: control.objectName + "Glyph"
            anchors.centerIn: parent
            width: 18; height: 18
            visible: control.symbol.length > 0 && !control.loading
            symbol: control.symbol
            color: control.selected ? theme.gold : theme.inkDim
        }
    }
    KeyboardAction {
        objectName: "vinylBarOpenSong"
        focusOnPointer: false
        x: 18
        y: 13
        width: 44
        height: 44
        focusRadius: 6
        enabled: root.hasPlayingSong
        accessibleName: "Open playing song"
        onTriggered: root.songRequested()
        RoundedPosterImage {
            anchors.fill: parent
            radius: 7
            sources: [root.displayedSong.artwork(root.displayedSong.playingEdition.cover)]
        }
    }
    Item {
        id: nowPlaying
        objectName: "vinylBarNowPlaying"
        readonly property string accessibleName: qsTr("Now playing: %1").arg(root.displayedSong.playingTrack.title)
        Accessible.role: Accessible.StaticText
        Accessible.name: accessibleName
        x: 74
        y: root.androidBar ? 9 : 13
        width: root.androidBar
            ? Math.max(0, root.width - x - (root.showDismiss ? 55 : 18))
            : Math.max(0, root.transportCenter - transport.width / 2 - 16 - x)
        height: root.androidBar ? 54 : 44
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            spacing: 3
            Text {
                width: parent.width
                text: root.displayedSong.playingTrack.title
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.displayedSong.playingEdition.artist + (root.page === "song" ? "" : " · " + root.displayedSong.playingEdition.name)
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            Text {
                objectName: "vinylBarSourceStatus"
                visible: root.androidBar && text.length > 0
                width: parent.width
                text: root.androidSourceStatus
                textFormat: Text.PlainText
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 10
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }
        }
    }
    Row {
        id: transport
        objectName: "vinylBarTransport"
        x: root.transportCenter - width / 2
        y: root.androidBar ? 64 : 15.5
        spacing: 12
        KeyboardAction {
            objectName: "vinylBarPrev"
            tooltipText: accessibleName
            androidTooltipPolicy: "caption"
            androidCaptionText: qsTr("Previous track")
            focusOnPointer: false
            enabled: root.trackNavigationEnabled && (root.pageData.canPrevious === undefined || root.pageData.canPrevious)
            opacity: enabled ? 1 : 0.35
            width: root.androidBar ? 96 : 32
            height: 39
            accessibleName: "Previous track"
            onTriggered: root.pageData.next(-1)
            VinylControlIcon { anchors.centerIn: parent; width: 21; height: 21; symbol: "previous"; color: theme.ink }
        }
        KeyboardAction {
            objectName: "vinylBarPlay"
            tooltipText: root.showSongPlay ? qsTr("Play viewed song") : root.busy ? qsTr("Waiting for source") : root.pageData.paused ? qsTr("Play") : qsTr("Pause")
            androidTooltipPolicy: "drop"
            focusOnPointer: false
            focusRadius: 18
            focusPrimary: true
            width: 39
            height: 39
            accessibleName: root.busy ? "Waiting for source" : "Play or pause"
            enabled: !root.busy && root.hasPlayingSong
            onTriggered: root.pageData.paused = !root.pageData.paused
            Rectangle {
                anchors.fill: parent
                radius: 19.5
                color: theme.gold
            }
            VinylControlIcon {
                anchors.centerIn: parent
                width: 18; height: 18
                symbol: root.pageData.paused ? "play" : "pause"
                visible: !root.busy
                color: theme.biblioWashBottom
            }
            BusyIndicator {
                objectName: "vinylBarLoading"
                anchors.fill: parent
                running: root.busy
                visible: running
                palette.dark: "#000000"
            }
        }
        KeyboardAction {
            objectName: "vinylBarNext"
            tooltipText: accessibleName
            androidTooltipPolicy: "caption"
            androidCaptionText: qsTr("Next track")
            focusOnPointer: false
            enabled: root.trackNavigationEnabled && (root.pageData.canNext === undefined || root.pageData.canNext)
            opacity: enabled ? 1 : 0.35
            width: root.androidBar ? 96 : 32
            height: 39
            accessibleName: "Next track"
            onTriggered: root.pageData.next(1)
            VinylControlIcon { anchors.centerIn: parent; width: 21; height: 21; symbol: "playNext"; color: theme.ink }
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
        x: 18 + Math.max(26, elapsedLabel.implicitWidth) + 11
        y: root.androidBar ? 205 : root.narrow ? 128 : 69
        width: Math.max(0, root.width - x - 18 - Math.max(26, totalTimeLabel.implicitWidth) - 11)
        height: 22
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 3
            radius: 1.5
            color: theme.edge
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * root.seekFraction
            height: 3
            radius: 1.5
            color: theme.gold
        }
        Rectangle {
            x: parent.width * root.seekFraction - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8
            radius: 4
            color: theme.gold
            Accessible.ignored: true
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
            id: elapsedLabel
            objectName: "vinylBarElapsed"
            anchors.right: parent.left
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            text: root.pageData.formatTime(root.displayedPosition)
            font.family: theme.ui
            font.pixelSize: 10
            color: theme.inkDimmer
        }
        Text {
            id: totalTimeLabel
            objectName: "vinylBarDuration"
            anchors.left: parent.right
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            text: root.pageData.formatTime(root.pageData.duration)
            font.family: theme.ui
            font.pixelSize: 10
            color: theme.inkDimmer
        }
    }
    Tool {
        objectName: "vinylBarVideo"
        x: root.toolsLeft
        y: root.toolsTop
        enabled: !root.torrentBusy && (root.videoBusy ? root.pageData.videoSwitchCancellable === true
            : root.pageData.hasVideo === true || root.videoShown)
        selected: root.videoShown
        loading: root.videoBusy && root.pageData.videoSwitchCancellable !== true
        symbol: root.videoBusy && root.pageData.videoSwitchCancellable === true ? "cancel" : "video"
        accessibleName: root.videoBusy
            ? (root.pageData.videoSwitchCancellable === true ? qsTr("Cancel audio/video switch") : qsTr("Switching playback mode"))
            : root.videoShown ? qsTr("Turn video off and return to original song audio") : qsTr("Switch to video")
        onTriggered: root.videoRequested()
        BusyIndicator {
            anchors.centerIn: parent
            width: 22; height: 22
            running: root.videoBusy && root.pageData.videoSwitchCancellable !== true
            visible: running
        }
    }
    Tool {
        objectName: "vinylBarQueue"
        x: root.toolsLeft + 45
        y: root.toolsTop
        width: root.androidBar ? 100 : 40
        androidTooltipPolicy: "caption"
        androidCaptionText: qsTr("Open queue")
        enabled: root.queueEnabled
        symbol: "queueOpen"
        accessibleName: qsTr("Open queue")
        onTriggered: root.pageData.queueRequested()
    }
    Item {
        id: sourcePill
        objectName: "vinylBarSources"
        x: root.androidBar ? Math.max(0, root.width - 168 - 18) : root.toolsLeft + 95
        y: root.toolsTop + 3
        width: 168
        height: 34
        Rectangle {
            objectName: "vinylBarSourceCapsule"
            anchors.fill: parent
            radius: 17
            color: "transparent"
            border.color: theme.edge
        }
        component Source: KeyboardAction {
            id: sourceControl
            required property string kind
            required property string label
            required property url logo
            readonly property bool selected: (root.sourceReady && root.pageData.sourceKind === kind)
                || (kind === "youtube" && root.defaultYouTubeChoice)
            readonly property bool pending: kind === "torrent" ? root.torrentBusy || (root.busy && root.pageData.sourceKind === "torrent")
                : root.busy && !root.torrentBusy && root.pageData.sourceKind !== "torrent"
            x: kind === "youtube" ? 0 : width
            width: 84; height: 34
            focusOnPointer: false
            enabled: root.sourceSelectionEnabled && (kind === "torrent"
                ? !root.torrentBusy && root.pageData.torrentAvailable === true && typeof root.pageData.findTorrent === "function"
                : typeof root.pageData.selectYouTube === "function")
            accessibleName: kind === "torrent" ? qsTr("Use Tankorent audio") : qsTr("Use YouTube audio")
            androidTooltipPolicy: "longPress"
            accessibleDescription: root.localSong ? qsTr("Local audio is playing")
                : root.videoShown || root.videoBusy ? qsTr("Audio source selection is unavailable during video")
                : root.defaultYouTubeChoice && kind === "youtube" ? qsTr("YouTube is the default choice; current playback source is not confirmed")
                : pending ? qsTr("Finding this source; selection changes only when audio is ready")
                : selected ? qsTr("Current audio source") : qsTr("Audio source is not selected")
            Accessible.checkable: true
            Accessible.checked: selected
            opacity: enabled || selected || pending ? 1 : 0.4
            Rectangle {
                objectName: sourceControl.objectName + "Selection"
                anchors.fill: parent
                radius: 17
                color: theme.gold
                border.width: 0
                visible: sourceControl.selected
            }
            Rectangle {
                x: sourceControl.kind === "youtube" ? parent.width / 2 : 0
                width: parent.width / 2
                height: parent.height
                color: theme.gold
                visible: sourceControl.selected
            }
            Image {
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: sourceControl.kind === "youtube" ? 18 : 16
                height: 16
                sourceSize: Qt.size(36, 32)
                source: sourceControl.logo
                visible: !sourceControl.pending
                fillMode: Image.PreserveAspectFit
            }
            BusyIndicator {
                x: 6; anchors.verticalCenter: parent.verticalCenter
                width: 24; height: 24
                running: sourceControl.pending
                visible: running
            }
            Text {
                objectName: sourceControl.objectName + "Label"
                x: 29; anchors.verticalCenter: parent.verticalCenter
                text: sourceControl.label
                color: sourceControl.selected ? theme.biblioWashBottom : theme.inkDim
                font.family: theme.ui
                font.pixelSize: 10
            }
            onTriggered: {
                if (kind === "youtube") root.pageData.selectYouTube()
                else root.pageData.findTorrent()
            }
        }
        Source {
            objectName: "vinylBarYouTube"
            kind: "youtube"; label: "YouTube"
            logo: "../../assets/icons/providers/youtube.svg"
        }
        Source {
            objectName: "vinylBarTankorent"
            kind: "torrent"; label: "Tankorent"
            logo: "../../assets/addon-logos/tankorent.png"
        }
    }
    Tool {
        objectName: "vinylBarClose"
        visible: root.showDismiss
        x: root.width - width - 8
        y: root.androidBar ? 13 : root.toolsTop
        androidTooltipPolicy: "drop"
        width: 36
        symbol: "cancel"
        accessibleName: qsTr("Close player and stop playback")
        onTriggered: root.closeRequested()
    }
    Text {
        objectName: "vinylBarSourceError"
        visible: !root.androidBar
        x: root.toolsLeft
        y: root.toolsTop + 42
        width: 263; height: 12
        text: root.pageData.torrentError || root.pageData.videoError || ""
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 10
        elide: Text.ElideRight
        Accessible.name: text
    }
}
