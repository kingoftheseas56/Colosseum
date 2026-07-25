import QtQuick
import "controls"
import "controls/Player2Browser.js" as Browser

// The immersive Player 2 chrome, overlaid on the video surface. It receives the C++ `session` (typed
// state + commands) and `hostServices` (app orchestration); it renders typed state and sends typed
// intent only — no demux, no pacing, no property strings. Palette and layout track the current
// player so the parity ledger can compare them side by side.
Item {
    id: shell

    property var session
    property var hostServices
    signal fullscreenRequested()
    // Host-fed window state (the host owns the window); drives the fullscreen/exit icon (parity).
    property bool windowed: true
    // Typed intent to leave the player (host owns the actual close: lab quits the Window, production
    // navigates away). Emitted directly when nothing is playing, or after the viewer confirms the
    // "Stop playback?" prompt. Same seam shape as fullscreenRequested.
    signal closeRequested()
    // Typed intent to toggle picture-in-picture. The host owns the window (lab: a small always-on-top
    // Window; production: its own PiP surface), exactly like fullscreenRequested.
    signal pipRequested()
    // Typed intents from the title bar. The host owns what "back" and "minimize" actually do (the app
    // returns to where you came from and parks the session warm in the taskbar).
    signal backRequested()
    signal minimizeRequested()
    // The line under the title: "S3 E1 - 1080p - <source>" in the shipped player.
    property string mediaSubtitle: ""
    // Fact, not command: whether the display-sleep/screensaver should be inhibited right now. The host
    // acts on it (lab: records it; production: SetThreadExecutionState). Fires only on transitions.
    signal keepAwakeRequested(bool inhibit)

    // Derived from playback state; drives keepAwakeRequested on every transition (held while the picture
    // advances, released on pause/idle/end/error) — the plan's power-inhibit lifetime.
    readonly property bool _inhibitSleep: shell.session ? Browser.shouldInhibitSleep(shell.session.state) : false
    on_InhibitSleepChanged: shell.keepAwakeRequested(shell._inhibitSleep)

    // Structured identity of what's playing, provided by the host (production: from its playbackContext;
    // lab: set on the shell). The drawer needs these to ask the host and to mark the now-playing row;
    // the engine itself never carries media identity — that stays orchestration.
    property string mediaTitle: ""
    property string rootMediaId: ""        // the series/movie id the host queries by
    property string currentEpisodeId: ""   // the exact episode playing now (now-playing highlight)
    property bool isSeries: false
    property int activeSeason: 1

    // Host-resolved intro/recap/credits skip segments for the current episode (drives SkipButton).
    property var skipSegments: []

    // The Kodi-style wall clocks — recomputed on a 1s tick and when the duration lands, never bound to
    // position churn. nowClock = the current time (top-right, like the main player); endsAtClock = the
    // wall-clock finish time (transport state row + the pause card).
    property string nowClock: ""
    property string endsAtClock: ""
    function updateEndsAt() {
        shell.nowClock = Browser.fmtWallClock(Date.now())
        shell.endsAtClock = shell.session
            ? Browser.endsAtLabel(Date.now(), shell.session.position, shell.session.duration, 1)
            : ""
    }
    // Progress cadence (Task 14): the player tells the host where you are so the host can persist a
    // resume point. Throttled to once every few seconds + forced on pause; the host owns the store.
    property real _lastReportedSec: -1
    function reportProgress(force) {
        if (!shell.hostServices || !shell.session || shell.session.duration <= 0)
            return
        var id = shell.currentEpisodeId.length ? shell.currentEpisodeId : shell.rootMediaId
        if (!id.length)
            return
        if (force || Browser.shouldReportProgress(shell._lastReportedSec, shell.session.position, 5)) {
            shell.hostServices.reportProgress(id, shell.session.position, shell.session.duration)
            shell._lastReportedSec = shell.session.position
        }
    }
    onPausedChanged: if (shell.paused) reportProgress(true)
    Timer {
        id: endsAtTick
        interval: 1000; repeat: true; running: true
        // Formats the clock (display only) and reports progress on cadence — never touches position.
        onTriggered: { shell.updateEndsAt(); shell.reportProgress(false) }
    }
    Connections {
        target: shell.session
        ignoreUnknownSignals: true
        function onDurationChanged() { shell.updateEndsAt() }
    }

    // Pause info card (main-player parity): media details hydrated from host metadata, shown a beat
    // after you pause so a glance tells you what you're watching.
    property string mediaLogo: ""
    property string mediaPlot: ""
    property string mediaYear: ""
    property bool pauseCardShown: false
    readonly property bool paused: shell.session && shell.session.state === 4   // Player2State::Paused
    readonly property bool pauseCardEligible: shell.paused && !shell.menusOpen
                                              && shell.session && shell.session.duration > 0
    onPauseCardEligibleChanged: {
        if (shell.pauseCardEligible) pauseCardDelay.restart()
        else { pauseCardDelay.stop(); shell.pauseCardShown = false }
    }
    Timer {
        id: pauseCardDelay
        interval: 900
        onTriggered: if (shell.pauseCardEligible) shell.pauseCardShown = true
    }
    function requestMediaMeta() {
        if (shell.hostServices && shell.rootMediaId.length)
            shell.hostServices.requestMetadata(shell.rootMediaId)
    }
    onRootMediaIdChanged: requestMediaMeta()

    // Typed intent up to the host app: the browser picked another episode / a different source. The
    // shell forwards; the app drives the actual (re)play — the same seam pattern as fullscreenRequested.
    signal playEpisodeRequested(string episodeId)
    signal switchSourceRequested(int index, string sourceId)

    focus: true

    // Parity palette (matches the current player's Theme: gold accent on dark glass).
    readonly property QtObject theme: QtObject {
        readonly property color gold: "#f0c44a"
        readonly property color ink: "#f7f7f5"
        readonly property color inkDim: "#c9c8d0"
        readonly property color inkDimmer: "#9a99a5"
        readonly property color panel: Qt.rgba(0.04, 0.05, 0.07, 0.94)
        readonly property color edge: Qt.rgba(1, 1, 1, 0.18)
    }

    property bool controlsShown: true
    readonly property bool menusOpen: transportBar.anyMenuOpen || overflowMenu.open || sourceDrawer.open
                                      || shortcutsSheet.open || closeConfirm.open
    function wakeChrome() {
        controlsShown = true
        hideTimer.restart()
    }
    function closeAllMenus() {
        transportBar.closeMenus()
        overflowMenu.open = false
        sourceDrawer.open = false
        shortcutsSheet.open = false
        closeConfirm.open = false
    }

    // The viewer asked to leave. Prompt first only if something is actively playing (Browser gate);
    // otherwise leave straight away. The host wires onCloseRequested to the real close.
    function requestClose() {
        var st = shell.session ? shell.session.state : 0
        if (Browser.shouldConfirmClose(st))
            closeConfirm.open = true
        else
            shell.closeRequested()
    }

    // Ask the host for this episode's skip segments (intro/recap/credits). Re-asked whenever the
    // playing episode changes; the host resolves once with a typed list (or empty).
    function requestSkipSegments() {
        if (!shell.hostServices)
            return
        var id = shell.currentEpisodeId.length ? shell.currentEpisodeId : shell.rootMediaId
        if (id.length)
            shell.hostServices.requestSkipSegments(id)
    }
    onCurrentEpisodeIdChanged: {
        shell.skipSegments = []; requestSkipSegments()
        shell.prevEpisodeId = ""; shell.nextEpisodeId = ""; refreshAdjacency()
    }

    // Prev/next episode: peek both directions so the transport arrows only light when a real neighbour
    // exists (the host resolves a {dead:true} map at a series boundary). Clicking plays the peeked id
    // via the same playEpisodeRequested seam the drawer uses — the host owns the actual (re)play.
    property string prevEpisodeId: ""
    property string nextEpisodeId: ""
    function refreshAdjacency() {
        if (shell.hostServices && shell.isSeries && shell.currentEpisodeId.length) {
            shell.hostServices.requestAdjacentEpisode(shell.currentEpisodeId, -1)
            shell.hostServices.requestAdjacentEpisode(shell.currentEpisodeId, 1)
        }
    }
    function playAdjacentEpisode(direction) {
        var id = direction < 0 ? shell.prevEpisodeId : shell.nextEpisodeId
        if (id.length)
            shell.playEpisodeRequested(id)
    }

    Connections {
        target: shell.hostServices
        ignoreUnknownSignals: true
        function onSkipSegmentsResolved(mediaId, segments) { shell.skipSegments = segments }
        function onAdjacentEpisodeResolved(mediaId, direction, episode) {
            if (mediaId !== shell.currentEpisodeId)
                return
            var id = (episode && !episode.dead && episode.mediaId) ? String(episode.mediaId) : ""
            if (direction < 0) shell.prevEpisodeId = id
            else shell.nextEpisodeId = id
        }
        function onMetadataResolved(mediaId, meta) {
            if (mediaId !== shell.rootMediaId)
                return
            shell.mediaLogo = meta.logo ? meta.logo : ""
            shell.mediaPlot = meta.plot ? meta.plot : ""
            shell.mediaYear = meta.year ? String(meta.year) : ""
            if (!shell.mediaTitle.length && meta.title)
                shell.mediaTitle = meta.title
        }
    }

    Timer {
        id: hideTimer
        interval: (transportBar.paused || transportBar.buffering || !shell.session) ? 4500 : 1800
        onTriggered: {
            // Never hide while paused/buffering or while a menu is open.
            if (!transportBar.paused && !transportBar.buffering && !shell.menusOpen)
                shell.controlsShown = false
        }
    }
    Component.onCompleted: { hideTimer.start(); requestSkipSegments(); requestMediaMeta(); updateEndsAt(); refreshAdjacency() }

    // Subtitles paint on the video, below the chrome, and persist when the chrome auto-hides.
    SubtitleLayer {
        anchors.fill: parent
        session: shell.session
        theme: shell.theme
    }

    // Pointer: move wakes the chrome; left-click toggles play/pause (or dismisses a menu); right-click
    // raises the overflow menu; the cursor hides with the HUD.
    MouseArea {
        id: videoMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: shell.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: shell.wakeChrome()
        onClicked: function(mouse) {
            shell.wakeChrome()
            if (mouse.button === Qt.RightButton) {
                // Close any open track menu first so two popovers never stack, then raise overflow.
                transportBar.closeMenus()
                shell.popupOverflow(mouse.x, mouse.y)
                return
            }
            if (shell.menusOpen) { shell.closeAllMenus(); return }
            if (shell.session) transportBar.togglePlayPause()
        }
    }

    WheelHandler {
        onWheel: function(event) {
            if (!shell.session)
                return
            var step = event.angleDelta.y > 0 ? 0.05 : -0.05
            shell.session.setMuted(false)
            shell.session.setVolume(Math.max(0, Math.min(1, shell.session.volume + step)))
            shell.wakeChrome()
        }
    }

    Keys.onPressed: function(event) {
        shell.wakeChrome()
        switch (event.key) {
        case Qt.Key_Space:
            transportBar.togglePlayPause(); event.accepted = true; break
        case Qt.Key_Left:
            if (shell.session) shell.session.seekRelative(-10); event.accepted = true; break
        case Qt.Key_Right:
            if (shell.session) shell.session.seekRelative(10); event.accepted = true; break
        case Qt.Key_Comma:
            if (shell.session) shell.session.frameStep(-1); event.accepted = true; break
        case Qt.Key_Period:
            if (shell.session) shell.session.frameStep(1); event.accepted = true; break
        case Qt.Key_M:
            if (shell.session) shell.session.setMuted(!shell.session.muted); event.accepted = true; break
        case Qt.Key_D:
            statsOverlay.open = !statsOverlay.open; event.accepted = true; break
        case Qt.Key_F:
            shell.fullscreenRequested(); event.accepted = true; break
        case Qt.Key_E:
            // Feature 8: E raises the episode/source browser (and toggles it back shut).
            sourceDrawer.open = !sourceDrawer.open; event.accepted = true; break
        case Qt.Key_Question:
            // "?" raises (and toggles) the keyboard-shortcuts sheet; Esc/tap also close it.
            shortcutsSheet.open = !shortcutsSheet.open; event.accepted = true; break
        case Qt.Key_Escape:
            if (shell.menusOpen) { shell.closeAllMenus(); event.accepted = true }
            break
        }
    }

    // Stats overlay persists (toggled with D / overflow) independent of the chrome fade.
    StatsOverlay {
        id: statsOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 40
        anchors.topMargin: 120
        session: shell.session
        theme: shell.theme
    }

    // All interactive chrome fades together on auto-hide.
    Item {
        id: chrome
        anchors.fill: parent
        opacity: shell.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Rectangle { // top scrim
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 112
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.60) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        // Live wall clock, top-right — the one place the player tells you the actual time (main-player
        // parity). Fades with the chrome.
        Text {
            id: nowClockLabel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 24
            anchors.topMargin: 18
            visible: shell.nowClock.length > 0
            text: shell.nowClock
            color: shell.theme.inkDim
            font.family: "Segoe UI"; font.pixelSize: 13; font.letterSpacing: 0.5
            font.features: ({ "tnum": 1 })
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
        }

        Item {
            id: bottomDock
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: transportBar.implicitHeight + 20

            Rectangle { // bottom scrim
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.45) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                }
            }

            // Title bar, matched to the shipped player's. Absent until now because the lab ran in an
            // ordinary desktop window that already had a frame.
            TopBar {
                id: topBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                theme: shell.theme
                title: shell.mediaTitle
                subtitle: shell.mediaSubtitle
                nowClock: shell.nowClock
                shown: shell.controlsShown
                onBackRequested: { shell.closeAllMenus(); shell.backRequested() }
                onMinimizeRequested: { shell.closeAllMenus(); shell.minimizeRequested() }
                onCloseRequested: { shell.closeAllMenus(); shell.requestClose() }
            }

            TransportBar {
                id: transportBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                session: shell.session
                theme: shell.theme
                endsAtClock: shell.endsAtClock
                hasPrevEpisode: shell.prevEpisodeId.length > 0
                hasNextEpisode: shell.nextEpisodeId.length > 0
                windowed: shell.windowed
                onFullscreenRequested: shell.fullscreenRequested()
                onBrowseRequested: { sourceDrawer.open = true; shell.wakeChrome() }
                onPrevEpisodeRequested: shell.playAdjacentEpisode(-1)
                onNextEpisodeRequested: shell.playAdjacentEpisode(1)
            }
        }
    }

    // Pause info card (main-player parity) — bottom-left; fades in a beat after you pause.
    PauseCard {
        anchors.fill: parent
        theme: shell.theme
        shown: shell.pauseCardShown
        mediaTitle: shell.mediaTitle
        mediaLogo: shell.mediaLogo
        currentEpisodeId: shell.currentEpisodeId
        mediaYear: shell.mediaYear
        mediaPlot: shell.mediaPlot
        durationSeconds: shell.session ? shell.session.duration : 0
        endsAtClock: shell.endsAtClock
        tracks: shell.session ? shell.session.tracks : []
    }

    // Skip Intro/Recap/Credits — persists through the chrome auto-hide, so it lives here (not inside the
    // fading dock). Appears only inside a segment; hidden while a menu/drawer is open.
    SkipButton {
        id: skipButton
        anchors.fill: parent
        theme: shell.theme
        segments: shell.skipSegments
        positionSeconds: shell.session ? shell.session.position : 0
        enabled: !shell.menusOpen
        chromeShown: shell.controlsShown
        onSkipRequested: function(toSeconds) { if (shell.session) shell.session.seekExact(toSeconds) }
    }

    // Feature 8 — the in-player episode/source browser. Above the transport chrome; the video keeps
    // playing beside it. Opens via the transport "Episodes & sources" button or the E key.
    SourceDrawer {
        id: sourceDrawer
        anchors.fill: parent
        theme: shell.theme
        hostServices: shell.hostServices
        rootMediaId: shell.rootMediaId
        currentEpisodeId: shell.currentEpisodeId
        mediaTitle: shell.mediaTitle
        isSeries: shell.isSeries
        activeSeason: shell.activeSeason
        onDismissed: sourceDrawer.open = false
        onEpisodePicked: function(episodeId) { shell.playEpisodeRequested(episodeId) }
        onSourcePicked: function(index, sourceId) { shell.switchSourceRequested(index, sourceId) }
    }

    // Right-click "more controls" menu, positioned at the cursor and clamped to the window.
    function popupOverflow(px, py) {
        overflowMenu.x = Math.max(10, Math.min(width - overflowMenu.width - 10, px))
        overflowMenu.y = Math.max(10, Math.min(height - overflowMenu.implicitHeight - 10, py))
        overflowMenu.open = true
    }
    OverflowMenu {
        id: overflowMenu
        session: shell.session
        theme: shell.theme
        onToggleStatsRequested: { statsOverlay.open = !statsOverlay.open; overflowMenu.open = false }
        onShowShortcutsRequested: { shortcutsSheet.open = true; overflowMenu.open = false }
        onPipRequested: { overflowMenu.open = false; shell.pipRequested() }
    }

    // Modal keyboard-shortcuts sheet (raised by "?" or the overflow menu). Data-driven from
    // Player2Shortcuts.js; sits above the fading chrome so it holds while open.
    ShortcutsSheet {
        id: shortcutsSheet
        theme: shell.theme
    }

    // Modal "Stop playback?" confirmation. requestClose() raises it when actively watching; confirming
    // fires the typed closeRequested seam (host does the real close), cancelling just dismisses.
    CloseConfirm {
        id: closeConfirm
        theme: shell.theme
        onConfirmed: { closeConfirm.open = false; shell.closeRequested() }
        onCancelled: closeConfirm.open = false
    }
}
