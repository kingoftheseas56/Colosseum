import QtQuick
import QtQuick.Controls
import ".."

// One presentation surface for AudiobookSession. No playback, history or timer ownership here.
Rectangle {
    id: root
    required property var session
    property bool compact: false
    property bool awake: true
    property bool showBookLine: !compact
    property bool showRemaining: true
    property real nowMs: Date.now()
    signal scrubPreviewed(real chapterFraction)
    signal scrubCommitted(real chapterFraction)
    signal chaptersRequested()

    implicitWidth: compact ? 380 : 780
    implicitHeight: compact ? 50 : 126
    radius: compact ? 25 : 20
    color: "#d911151e"
    border.width: 1
    border.color: "#38ffffff"
    opacity: !compact || awake ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Theme { id: theme }

    readonly property real chapterDuration: Math.max(0, Number(session.chapterDuration) || 0)
    readonly property real chapterPosition: Math.max(0, Math.min(chapterDuration, Number(session.chapterPosition) || 0))
    readonly property real chapterFraction: chapterDuration > 0 ? chapterPosition / chapterDuration : 0
    readonly property real shownPosition: scrubbing ? previewFraction * chapterDuration : chapterPosition
    readonly property real remaining: Math.max(0, chapterDuration - chapterPosition)
    readonly property string chapterName: {
        var chapter = session.chapterModel[session.chapterIndex]
        return chapter ? String(chapter.label || "") : ""
    }
    function fmt(seconds) {
        var s = Math.max(0, Math.floor(Number(seconds) || 0))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + s % 60
    }
    function clamp(f) { return Math.max(0, Math.min(1, Number(f) || 0)) }
    function skip(seconds) {
        if (Math.abs(seconds) === 60) session.markJumpOrigin()
        session.skipBook(seconds)
    }
    function chapter(delta) { session.markJumpOrigin(); session.goToChapter(session.chapterIndex + delta) }
    function openMenu() { if (!compact) moreMenu.popup() }

    // Vinyl's preview/commit transaction, keyed by chapter rather than track. Even a change away
    // and back during a drag cancels immediately; release cannot seek into a different chapter.
    property bool scrubbing: false
    property real previewFraction: 0
    property int scrubChapter: -1
    readonly property int currentChapter: session.chapterIndex
    onCurrentChapterChanged: cancelScrub()
    onSessionChanged: cancelScrub()
    onCompactChanged: cancelScrub()
    onChapterDurationChanged: if (chapterDuration <= 0) cancelScrub()
    onVisibleChanged: if (!visible) cancelScrub()
    function beginScrub(x) {
        if (compact || chapterDuration <= 0) return
        scrubChapter = currentChapter
        scrubbing = true
        moveScrub(x)
    }
    function moveScrub(x) {
        if (!scrubbing) return
        if (scrubChapter !== currentChapter || chapterDuration <= 0) { cancelScrub(); return }
        previewFraction = clamp(x / Math.max(1, chapterLine.width))
        scrubPreviewed(previewFraction)
    }
    function cancelScrub() { scrubbing = false; scrubChapter = -1 }
    function commitFraction(f) {
        f = clamp(f)
        scrubCommitted(f)
        session.markJumpOrigin()
        session.seekInChapter(f)
    }
    function commitScrub(x) {
        if (!scrubbing) return
        moveScrub(x)
        if (!scrubbing) return
        var fraction = previewFraction
        cancelScrub()
        commitFraction(fraction)
    }
    Shortcut { sequence: "Escape"; enabled: root.scrubbing; onActivated: root.cancelScrub() }
    Timer { interval: 15000; repeat: true; running: root.visible && !root.compact; onTriggered: root.nowMs = Date.now() }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        enabled: !root.compact
        onClicked: function(mouse) { moreMenu.popup(root, mouse.x, mouse.y) }
    }

    // TransportBar's bare-button + KeyboardAction pattern. Player2Icon's shader tint does not
    // render on the software backend, and its vocabulary lacks moon/return/list. These small SVG
    // paths follow the approved mock directly, using a tinted Image (also works offscreen).
    component BarButton: KeyboardAction {
        id: button
        property string icon: ""
        property string seconds: ""
        property string label: ""
        property string badge: ""
        property bool hero: false
        property bool returnDot: false
        property real size: root.compact ? 36 : 40
        property color ink: hero ? "#201a0d" : returnDot || badge !== "" ? "#f3d47b" : "#e3e2e8"
        width: size; height: size
        anchors.verticalCenter: parent.verticalCenter
        tooltipText: accessibleName
        spaceActivates: true
        focusRadius: hero ? size / 2 : 11
        contextEnabled: !root.compact
        onContextRequested: root.openMenu()
        readonly property var paths: ({
            play: 'm8 5 11 7-11 7Z', pause: 'M8 5v14M16 5v14',
            prev: 'M5 4v16M19 5l-11 7 11 7Z', next: 'M19 4v16M5 5l11 7-11 7Z',
            back: 'M4.5 12a7.5 7.5 0 1 0 2.2-5.3M4 4v4h4',
            forward: 'M19.5 12a7.5 7.5 0 1 1-2.2-5.3M20 4v4h-4',
            undo: 'M9 7 5 11l4 4M6 11h9a5 5 0 0 1 0 10h-2',
            moon: 'M20 15.1A8.5 8.5 0 0 1 8.9 4 8.5 8.5 0 1 0 20 15.1Z',
            list: 'M9 6h11M9 12h11M9 18h11M4 6h.2M4 12h.2M4 18h.2',
            volume: 'M4 10v4h4l5 4V6l-5 4ZM17 9a5 5 0 0 1 0 6M19 6a9 9 0 0 1 0 12',
            mute: 'M4 10v4h4l5 4V6l-5 4ZM17 9l5 6M22 9l-5 6'
        })
        Rectangle { anchors.fill: parent; radius: button.hero ? width/2 : 11; color: button.hero ? theme.gold : button.interactionActive ? "#1affffff" : "transparent" }
        Image {
            anchors.centerIn: parent
            width: button.hero ? (root.compact ? 16 : 20) : button.seconds !== "" ? (root.compact ? 23 : 26) : 21
            height: width
            sourceSize.width: width * 2; sourceSize.height: height * 2
            visible: button.icon !== ""
            source: button.icon === "" ? "" : "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + button.ink + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="' + button.paths[button.icon] + '"/></svg>')
        }
        Text { anchors.centerIn: parent; anchors.verticalCenterOffset: button.seconds !== "" ? 1 : 0; text: button.seconds || button.label; color: button.ink; font.family: theme.ui; font.pixelSize: button.seconds !== "" ? (root.compact ? 8 : 8.5) : 11.5; font.weight: Font.DemiBold }
        Text { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: 1; anchors.bottomMargin: 2; text: button.badge; color: "#f0d486"; font.family: theme.ui; font.pixelSize: 9; font.weight: Font.Bold }
        Rectangle { visible: button.returnDot; x: parent.width - (root.compact ? 11 : 13); y: root.compact ? 6 : 8; width: 5; height: 5; radius: 2.5; color: theme.gold }
        opacity: enabled ? 1 : 0.35
    }
    component TimeLabel: Text {
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 12
        font.features: ({"tnum": 1})
    }
    Item {
        id: status
        visible: !root.compact
        x: 18; y: 8; width: parent.width - 36; height: 24
        Text {
            objectName: "abBarChapterLabel"
            anchors.left: parent.left; anchors.right: endsAt.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: "CHAPTER " + (root.session.chapterIndex + 1) + (root.chapterName ? " · " + root.chapterName.toUpperCase() : "")
            elide: Text.ElideRight
            color: "#f2d27e"; font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: .8
        }
        TimeLabel {
            id: endsAt
            objectName: "abBarEndsAt"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            visible: root.chapterDuration > 0
            font.pixelSize: 11; font.letterSpacing: .6; color: theme.inkDimmer
            text: "chapter ends " + Qt.formatTime(new Date(root.nowMs + root.remaining / Math.max(.01, root.session.speed) * 1000), "h:mm AP")
        }
    }
    TimeLabel { visible: !root.compact; x: 18; y: 41; text: root.fmt(root.shownPosition) }
    TimeLabel {
        id: time
        objectName: "abBarTime"
        visible: !root.compact
        anchors.right: parent.right; anchors.rightMargin: 18; y: 41
        text: root.showRemaining ? "−" + root.fmt(root.chapterDuration - root.shownPosition) : root.fmt(root.chapterDuration)
        KeyboardAction {
            objectName: "abBarTimeAction"
            anchors.fill: parent
            accessibleName: root.showRemaining ? "Show total duration" : "Show remaining time"
            tooltipText: accessibleName
            onTriggered: root.showRemaining = !root.showRemaining
        }
    }
    // In compact mode this same rail is strictly display-only and cannot take pointer/Tab input.
    Item {
        id: chapterLine
        objectName: "abBarChapterLine"
        x: root.compact ? 22 : 70
        y: root.compact ? 0 : 39
        width: parent.width - (root.compact ? 44 : 140)
        height: root.compact ? 2 : 20
        enabled: root.chapterDuration > 0
        focusPolicy: !root.compact && enabled ? Qt.TabFocus : Qt.NoFocus
        Accessible.role: root.compact ? Accessible.ProgressBar : Accessible.Slider
        Accessible.name: "Chapter progress"
        Rectangle {
            x: 0; y: root.compact ? 0 : 10; width: parent.width; height: root.compact ? 2 : 3; radius: 2; color: "#35ffffff"
            Rectangle { width: parent.width * (root.scrubbing ? root.previewFraction : root.chapterFraction); height: parent.height; radius: 2; color: theme.gold }
        }
        Rectangle { visible: !root.compact; x: chapterLine.width * (root.scrubbing ? root.previewFraction : root.chapterFraction) - 5.5; y: 6; width: 11; height: 11; radius: 5.5; color: theme.gold; border.width: 1; border.color: "#090a0f" }
        MouseArea {
            anchors.fill: parent; enabled: !root.compact; cursorShape: Qt.PointingHandCursor
            onPressed: function(mouse) { chapterLine.forceActiveFocus(Qt.MouseFocusReason); root.beginScrub(mouse.x) }
            onPositionChanged: function(mouse) { if (pressed) root.moveScrub(mouse.x) }
            onReleased: function(mouse) { root.commitScrub(mouse.x) }
            onCanceled: root.cancelScrub()
        }
        Keys.onPressed: function(event) {
            if (root.compact || root.chapterDuration <= 0) return
            var target = root.chapterPosition
            if (event.key === Qt.Key_Left) target -= 5
            else if (event.key === Qt.Key_Right) target += 5
            else if (event.key === Qt.Key_Home) target = 0
            else if (event.key === Qt.Key_End) target = root.chapterDuration
            else return
            root.commitFraction(target / root.chapterDuration)
            event.accepted = true
        }
        FocusRing { anchors.fill: parent; radius: 4; shown: chapterLine.activeFocus && !root.compact }
    }
    Rectangle {
        objectName: "abBarBookLine"
        visible: root.showBookLine && !root.compact
        x: chapterLine.x; y: 59; width: chapterLine.width; height: 2; radius: 2; color: "#1cffffff"
        Rectangle { width: parent.width * root.clamp(root.session.bookFraction); height: 2; radius: 2; color: "#66f0c44a" }
        HoverHandler { id: bookHover }
        ToolTip.visible: bookHover.hovered
        ToolTip.text: Math.round(root.clamp(root.session.bookFraction) * 100) + "% of the whole book"
    }
    TimeLabel { objectName: "abBarCompactTime"; visible: root.compact; x: 18; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11; text: "−" + root.fmt(root.remaining) }

    Item {
        id: transport
        x: root.compact ? 10 : 18; y: root.compact ? 0 : 66
        width: parent.width - (root.compact ? 20 : 36); height: root.compact ? 50 : 52
        Row {
            visible: !root.compact; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            BarButton { objectName: "abBarSpeed"; label: root.session.speed + "×"; accessibleName: "Playback speed"; onTriggered: root.session.cycleSpeed() }
            BarButton { id: sleepButton; objectName: "abBarSleep"; icon: "moon"; badge: root.session.sleepMinutesLeft > 0 ? root.session.sleepMinutesLeft + "m" : ""; accessibleName: "Sleep timer"; onTriggered: sleepMenu.popup(sleepButton, 0, -sleepMenu.height) }
        }
        Row {
            anchors.centerIn: parent
            spacing: root.compact ? 2 : 6
            height: root.compact ? 36 : 46
            BarButton { objectName: "abBarPrevChapter"; visible: !root.compact; icon: "prev"; accessibleName: "Previous chapter"; enabled: root.session.chapterIndex > 0; onTriggered: root.chapter(-1) }
            BarButton { objectName: "abBarBack60"; icon: "back"; seconds: "60"; accessibleName: "Back 1 minute"; onTriggered: root.skip(-60) }
            BarButton { objectName: "abBarBack10"; icon: "back"; seconds: "10"; accessibleName: "Back 10 seconds"; onTriggered: root.skip(-10) }
            Item {
                width: root.compact ? 44 : 58; height: parent.height
                BarButton { objectName: "abBarPlay"; x: root.compact ? 4 : 6; size: root.compact ? 36 : 46; hero: true; icon: root.session.paused ? "play" : "pause"; accessibleName: root.session.paused ? "Play audiobook" : "Pause audiobook"; onTriggered: root.session.togglePlay() }
            }
            BarButton { objectName: "abBarFwd10"; icon: "forward"; seconds: "10"; accessibleName: "Forward 10 seconds"; onTriggered: root.skip(10) }
            BarButton { objectName: "abBarFwd60"; icon: "forward"; seconds: "60"; accessibleName: "Forward 1 minute"; onTriggered: root.skip(60) }
            BarButton { objectName: "abBarNextChapter"; visible: !root.compact; icon: "next"; accessibleName: "Next chapter"; enabled: root.session.chapterIndex + 1 < root.session.chapterModel.length; onTriggered: root.chapter(1) }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            BarButton { objectName: "abBarUndo"; visible: root.session.canGoBack; icon: "undo"; returnDot: true; accessibleName: "Back to where I was"; onTriggered: root.session.backToPrevious() }
            BarButton { objectName: "abBarChapters"; visible: !root.compact; icon: "list"; accessibleName: "Chapters"; onTriggered: root.chaptersRequested() }
            Item {
                id: volume
                objectName: "abBarVolume"
                visible: !root.compact; width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter
                HoverHandler { id: volumeHover; onHoveredChanged: { if (hovered) { volumeClose.stop(); volumePopup.open() } else volumeClose.restart() } }
                BarButton {
                    id: mute
                    objectName: "abBarMute"; icon: root.session.mute || root.session.volume <= 0 ? "mute" : "volume"
                    accessibleName: root.session.mute ? "Unmute" : "Mute"
                    onTriggered: root.session.mute = !root.session.mute
                    Keys.onUpPressed: { volumePopup.open(); volumeSlider.forceActiveFocus(Qt.TabFocusReason) }
                }
                Popup {
                    id: volumePopup
                    objectName: "abBarVolumePopup"
                    x: 3; y: -108; width: 34; height: 104; padding: 8
                    background: Rectangle { radius: 12; color: "#f011151e"; border.width: 1; border.color: "#33ffffff" }
                    HoverHandler { id: popupHover; onHoveredChanged: { if (hovered) volumeClose.stop(); else volumeClose.restart() } }
                    Slider {
                        id: volumeSlider
                        objectName: "abBarVolumeSlider"
                        Accessible.name: "Volume"
                        anchors.fill: parent; orientation: Qt.Vertical; from: 0; to: 100; stepSize: 5
                        value: root.session.volume
                        onMoved: { root.session.volume = value; root.session.mute = false }
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                root.session.volume = event.key === Qt.Key_Home ? 0 : 100
                                root.session.mute = false
                                event.accepted = true
                            }
                        }
                        background: Rectangle { x: (volumeSlider.width - 3)/2; y: 4; width: 3; height: volumeSlider.height - 8; radius: 2; color: "#30ffffff"; Rectangle { anchors.bottom: parent.bottom; width: 3; height: parent.height * volumeSlider.position; color: "#d9d8de" } }
                        handle: Rectangle { x: (volumeSlider.width - width)/2; y: volumeSlider.visualPosition * (volumeSlider.height - height); width: 9; height: 9; radius: 4.5; color: "#dfdfe3"; border.width: volumeSlider.activeFocus ? 2 : 0; border.color: theme.gold }
                    }
                }
                Timer { id: volumeClose; interval: 180; onTriggered: if (!volumeHover.hovered && !popupHover.hovered && !volumeSlider.activeFocus) volumePopup.close() }
            }
        }
    }
    component BarMenu: Menu {
        width: 262
        padding: 6
        background: Rectangle { radius: 12; color: "#f50f121b"; border.width: 1; border.color: "#33ffffff" }
    }
    component BarMenuItem: MenuItem {
        id: entry
        implicitWidth: 250
        implicitHeight: 34
        contentItem: Text {
            text: entry.text
            leftPadding: 10; rightPadding: 10
            verticalAlignment: Text.AlignVCenter
            font.family: theme.ui; font.pixelSize: 12
            color: entry.checked ? theme.gold : "#e3e2e8"
        }
        indicator: null
        background: Rectangle { radius: 8; color: entry.highlighted ? "#12ffffff" : "transparent" }
    }
    BarMenu {
        id: moreMenu
        objectName: "abBarMenu"
        BarMenuItem { text: root.showRemaining ? "Show total duration" : "Show remaining time"; onTriggered: root.showRemaining = !root.showRemaining }
    }
    BarMenu {
        id: sleepMenu
        objectName: "abBarSleepMenu"
        Repeater {
            model: [0, 15, 30, 45, 60]
            BarMenuItem {
                required property int modelData
                text: modelData === 0 ? "Off" : modelData + " minutes"
                checkable: true; checked: root.session.sleepMinutes === modelData
                onTriggered: root.session.sleepMinutes = modelData
            }
        }
    }
}
