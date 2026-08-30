import QtQuick
import "controls"
import "controls/Player2Browser.js" as Browser
import "../ActivityLaneHelpers.js" as ActivityLaneHelpers
import "../Player2ActivityHelpers.js" as Player2ActivityHelpers
import Colosseum.Activity

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

    // Your Colosseum activity (Lane B) bookkeeping — the identity key currently open with
    // activityTracker below, or "" when no session is tracked. See the activity block near
    // the session Connections for the full hook (CPP-PORT-CONTRACT.md §9 Lane B).
    property string activityActiveKey: ""

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
            // Rate-aware, as production is: at 1.5x the finish time is not the 1x finish time.
            ? Browser.endsAtLabel(Date.now(), shell.session.position, shell.session.duration,
                                  shell.session.speed > 0 ? shell.session.speed : 1)
            : ""
    }
    // Progress cadence (Task 14): the player tells the host where you are so the host can persist a
    // resume point. Throttled to once every few seconds + forced on pause; the host owns the store.
    property real _lastReportedSec: -1
    function reportProgress(forceVisible) {
        if (!shell.hostServices || !shell.session || shell.session.duration <= 0)
            return
        var id = shell.currentEpisodeId.length ? shell.currentEpisodeId : shell.rootMediaId
        if (!id.length)
            return
        if (forceVisible || Browser.shouldReportProgress(shell._lastReportedSec, shell.session.position, 5)) {
            shell.hostServices.reportProgress(id, shell.session.position, shell.session.duration, !forceVisible)
            shell._lastReportedSec = shell.session.position
        }
    }
    onPausedChanged: if (shell.paused) reportProgress(true)
    Timer {
        id: endsAtTick
        interval: 1000; repeat: true; running: true
        // Formats the clock (display only), reports progress on cadence, and samples activity
        // (§9 Lane B: "Player 2's existing regular UI clock/timer") — never touches position.
        onTriggered: { shell.updateEndsAt(); shell.reportProgress(false); shell.activitySample() }
    }
    Connections {
        target: shell.session
        ignoreUnknownSignals: true
        function onDurationChanged() { shell.updateEndsAt() }
        // --- Your Colosseum activity (Lane B), CPP-PORT-CONTRACT.md §9 Lane B ----------------
        // consuming is driven EXCLUSIVELY from the native typed session state (never mpv/
        // property-string state, per §9's "Recommended minimal bridge"). State entering
        // Playing is "a real identity becomes actively playable" (begin-if-needed, itself a
        // no-op for a same-identity reload/recovery); Ended is the natural end of the item;
        // every other state entry (Opening/Buffering/Paused/Seeking/Recovering/Error/Idle) is a
        // discontinuity reset — a superset of the contract's explicit "ANY transition out of
        // Playing" + listed entries, safe because activityDiscontinuity() only resets the
        // sampling baseline and fails closed toward undercount (§25), never invents time.
        function onStateChanged() {
            var st = shell.session ? shell.session.state : -1
            if (st === 3)        // Player2State::Playing
                shell.activityBeginIfNeeded()
            else if (st === 6) {  // Player2State::Ended
                shell.reportProgress(true)
                shell.activityNaturalEof()
            }
            else                 // Opening/Buffering/Paused/Seeking/Recovering/Error/Idle entry
                shell.activityDiscontinuity()
        }
        // A generation bump is a new demux/decoder identity under the hood (episode switch,
        // stream replacement, recovery) — reset the sampling baseline here directly rather than
        // relying only on the state blip around it.
        function onGenerationChanged() { shell.activityDiscontinuity() }
        // The authoritative post-seek landing (§9 "seek start + seekCompleted") — seek START is
        // already covered by the Seeking state entry above; this resets right at the landing
        // rather than waiting for the next sample tick or a state flip back to Playing.
        function onSeekCompleted(generation, actualSeconds) { shell.activityDiscontinuity() }
        function onSpeedChanged() { shell.activityDiscontinuity() }
    }
    // A different session instance (or none at all) means whatever this shell was tracking
    // under the OLD session no longer applies; the new session's own onStateChanged begins a
    // fresh one when it reaches Playing.
    onSessionChanged: shell.activityEndSession()

    // Identity derivation (§7 Player 2) lives in Player2ActivityHelpers.js; the shared
    // begin/no-op/end state-transition rule (§9 "item identity/autoplay change" — an autoplay
    // episode switch ends the old item's session and begins a new one under the SAME titleKey,
    // preserving title grouping) is the SAME ActivityLaneHelpers.decideTransition()/keyFor()
    // Lane A (qml/PlayerPage.qml) and Lane E (qml/AudiobookSession.qml) already use — called,
    // never modified, so tests/qml/tst_player2_activity.qml exercises the exact code this hook
    // runs, not a parallel reimplementation. sink is guarded null so a missing/unbound
    // ActivityStore can never break playback (§25).
    function activityBeginIfNeeded() {
        var idf = Player2ActivityHelpers.videoIdentityFor(shell.rootMediaId, shell.currentEpisodeId)
        var action = ActivityLaneHelpers.decideTransition(shell.activityActiveKey, idf)
        if (action === "noop")
            return
        shell.activityEndSession()
        if (action === "end")
            return
        shell.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
        var sink = (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
        var sessionId = (sink && sink.newSessionId) ? sink.newSessionId() : ""
        activityTracker.begin({
            "world": "theatre",
            "kind": idf.kind,
            "titleKey": idf.titleKey,
            "itemKey": idf.itemKey,
            "title": shell.mediaTitle,
            "itemLabel": idf.kind === "episode" ? shell.mediaSubtitle : "",
            "cover": shell.mediaLogo || "",
            "syncable": true,
            "source": "player2"
        }, sessionId)
    }
    // Sampling source: the existing endsAtTick 1-second UI clock, in addition to (never instead
    // of) its wall-clock/Continue work.
    function activitySample() {
        if (!shell.activityActiveKey.length)
            return
        var s = shell.session
        if (!s)
            return
        // §9 Lane B qualifying state, exclusively from the native typed session.
        var consuming = (s.state === 3) && !s.networkStalled   // Player2State::Playing
        var rateMilli = (s.speed && s.speed > 0) ? Math.round(s.speed * 1000) : 1000
        activityTracker.sample(Math.round(s.position * 1000), Math.round(s.duration * 1000),
                                rateMilli, consuming)
    }
    // Discontinuity: every §9 Lane B reset bullet except item/episode change
    // (activityBeginIfNeeded's own end+begin already covers that autoplay/identity case).
    function activityDiscontinuity() {
        if (!shell.activityActiveKey.length)
            return
        var s = shell.session
        if (!s)
            return
        var rateMilli = (s.speed && s.speed > 0) ? Math.round(s.speed * 1000) : 1000
        activityTracker.discontinuity(Math.round(s.position * 1000), Math.round(s.duration * 1000), rateMilli)
    }
    // Natural EOF: the native session's real Ended state. Ends the session too (a later replay
    // begins a fresh one), mirroring Lane A/Lane E.
    function activityNaturalEof() {
        if (!shell.activityActiveKey.length)
            return
        activityTracker.naturalEof()
        activityTracker.endSession()
        shell.activityActiveKey = ""
    }
    function activityEndSession() {
        if (!shell.activityActiveKey.length)
            return
        activityTracker.endSession()
        shell.activityActiveKey = ""
    }
    // One transient tracker for this lane (§8/§9 Lane B). Activity NEVER breaks playback if
    // ProfileActivity is absent (§25).
    ActivityPlaybackTracker {
        id: activityTracker
        sink: (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
    }

    // Pause info card (main-player parity): media details hydrated from host metadata, shown a beat
    // after you pause so a glance tells you what you're watching.
    property string mediaLogo: ""
    property string mediaPlot: ""
    property string mediaYear: ""
    property bool pauseCardShown: false
    readonly property bool paused: shell.session && shell.session.state === 4   // Player2State::Paused
    // Production also refuses the card while the player is starting, errored, or mid-seek, and until
    // the file is ready (its `fileReady` gate). Without those, a paused-but-buffering engine could
    // raise a details card over a player that has nothing to show yet.
    // Host-fed: the page owns the loading/error surface, and production's pause-card gate excludes
    // `starting` and `errored` outright. Without this the card could sit under a loading screen.
    property bool loaderActive: false
    readonly property bool pauseCardEligible: shell.paused && !shell.menusOpen && !shell.loaderActive
                                              && shell.session && shell.session.duration > 0
                                              && shell.session.state !== 1   // Opening
                                              && shell.session.state !== 2   // Buffering
                                              && shell.session.state !== 5   // Seeking
                                              && shell.session.state !== 8   // Error
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

    // --- HUD download button state (parity with the shipped player, PlayerPage.qml:1634-1701) ------
    // The host reports "queued" / "active" / "ready" / "failed"; the button speaks the shipped
    // player's vocabulary, so translate once here rather than at the paint site.
    property string downloadKind: "idle"   // idle | queued | downloading | done | failed
    property real downloadProgress: 0
    property string downloadPath: ""
    property string downloadError: ""

    function downloadTooltip() {
        if (shell.downloadKind === "queued")
            return "Preparing download"
        if (shell.downloadKind === "downloading")
            return "Downloading " + Math.round((shell.downloadProgress || 0) * 100)
                   + "% - click to cancel"
        if (shell.downloadKind === "done")
            return "Saved to " + (shell.downloadPath.length ? shell.downloadPath : "Downloads")
        if (shell.downloadKind === "failed")
            return "Failed: " + (shell.downloadError.length ? shell.downloadError : "Download failed")
        return "Download video"
    }

    // Same decision table as the shipped player's handleDownloadAction(): a click means "start" when
    // idle and "get out of this state" otherwise.
    function handleDownloadAction() {
        if (!shell.hostServices)
            return
        if (shell.downloadKind === "downloading" || shell.downloadKind === "queued") {
            if (shell.hostServices.cancelDownload)
                shell.hostServices.cancelDownload(shell.rootMediaId)
            shell.downloadKind = "idle"
        } else if (shell.downloadKind === "done" || shell.downloadKind === "failed") {
            shell.downloadKind = "idle" // acknowledge and re-arm, as production's reset does
        } else {
            // An empty sourceId is deliberate: the host falls back to the URL actually playing,
            // which is exactly what production's currentCastUrl() resolves to first.
            shell.hostServices.requestDownload(shell.rootMediaId, "")
        }
        shell.wakeChrome()
    }

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
        else {
            shell.activityEndSession()   // Activity (§9 Lane B): close/lifecycle exit ends the session
            shell.closeRequested()
        }
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
        function onDownloadStateChanged(mediaId, state) {
            if (mediaId !== shell.rootMediaId)
                return
            var s = state || ({})
            var kind = String(s.state || "")
            shell.downloadProgress = Number(s.progress || 0)
            shell.downloadPath = String(s.path || "")
            shell.downloadError = String(s.error || "")
            shell.downloadKind = kind === "active" ? "downloading"
                                 : kind === "ready" ? "done"
                                 : kind === "failed" ? "failed"
                                 : kind.length ? "queued" : "idle"
        }
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
            else
                hideTimer.restart() // re-arm: the hold is temporary, the timer must not die with it
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
    // The card is the SHIPPED player's, ported verbatim (his directive 2026-07-26); this block only
    // feeds it. Same id, so the D-key and overflow toggles did not change.
    PlaybackStatsCard {
        id: statsOverlay
        theme: shell.theme
        stats: shell._playbackStats
    }
    property var _playbackStats: ({})
    Timer {
        // Production's cadence (playbackStatsTimer, 1Hz, only while open).
        interval: 1000; repeat: true; running: statsOverlay.open; triggeredOnStart: true
        onTriggered: {
            var s = shell.session
            var d = s && s.diagnostics ? s.diagnostics() : ({})
            var res = ""
            var m = String(d.inputFormat || "").match(/(\d{2,5})\s*x\s*(\d{2,5})/)
            function trackLabel(type, index) {
                var rows = (s && s.tracks) ? s.tracks : []
                for (var i = 0; i < rows.length; i++) {
                    var t = rows[i] || ({})
                    if (t.type === type && Number(t.index) === Number(index))
                        return t.title || t.language || t.codec || String(t.index)
                }
                return index >= 0 ? String(index) : ""
            }
            shell._playbackStats = {
                "width": m ? Number(m[1]) : 0, "height": m ? Number(m[2]) : 0,
                "videoCodec": d.videoCodec || "",
                "hwdec": d.hardwareFormat || "",
                // Bitrates and fps are not measured by this engine yet; the card renders "--"
                // for them, exactly as the shipped card renders an absent mpv property.
                "frameDropDecoder": Number(d.dropped || 0),
                "frameDropOutput": Number(d.scheduledLateDrops || 0),
                "audioTrack": trackLabel("audio", transportBar.currentAudioIndex),
                "subtitleTrack": trackLabel("subtitle", transportBar.currentSubtitleIndex),
                "speed": s ? s.speed : 1,
                "volume": s ? s.volume * 100 : 0,
                "muted": s ? s.muted : false
            }
            // The audio CODEC itself lives on the track row, same source the label came from.
            var rows = (s && s.tracks) ? s.tracks : []
            for (var i = 0; i < rows.length; i++)
                if (rows[i].type === "audio" && Number(rows[i].index) === Number(transportBar.currentAudioIndex))
                    shell._playbackStats.audioCodec = rows[i].codec || ""
        }
    }

    // All interactive chrome fades together on auto-hide.
    Item {
        id: chrome
        anchors.fill: parent
        opacity: shell.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // Title bar: scrim, Back, NOW PLAYING / title / episode line, the wall clock, Minimize and
        // Close. It REPLACES the bare top scrim and the standalone clock that used to sit here - it
        // provides both (TopBar's own nowClock text draws the wall clock now), and keeping them
        // alongside it painted the clock twice.
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
            onBackRequested: { shell.closeAllMenus(); shell.reportProgress(true); shell.backRequested() }
            onMinimizeRequested: { shell.closeAllMenus(); shell.reportProgress(true); shell.minimizeRequested() }
            onCloseRequested: { shell.closeAllMenus(); shell.requestClose() }
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
                canSwitchSource: sourceDrawer.sources.length > 1
                // A local file must not offer Download - production gates on a STREAM url existing,
                // and a file already on disk has nothing to fetch (cross-model review, P0: checking
                // only for a nonempty URL exposed the button on local playback).
                canDownload: shell.hostServices
                             && String(shell.hostServices.currentPlaybackUrl || "").length > 0
                             && String(shell.hostServices.mediaLocalPath || "").length === 0
                downloadKind: shell.downloadKind
                downloadTooltip: shell.downloadTooltip()
                onFullscreenRequested: shell.fullscreenRequested()
                onBrowseRequested: { sourceDrawer.open = true; shell.wakeChrome() }
                // His ruling: the drawer KEEPS ownership of switching, so the HUD button is a
                // shortcut straight to its Sources tab, not a second switcher that could disagree
                // with it.
                onSwitchSourceRequested: {
                    sourceDrawer.tab = "sources"
                    sourceDrawer.open = true
                    shell.wakeChrome()
                }
                onDownloadRequested: shell.handleDownloadAction()
                onPrevEpisodeRequested: shell.playAdjacentEpisode(-1)
                onNextEpisodeRequested: shell.playAdjacentEpisode(1)
            }
        }
    }

    // Pause info card (main-player parity) — bottom-left; fades in a beat after you pause.
    PauseCard {
        anchors.fill: parent
        theme: shell.theme
        barTiny: transportBar.barTiny   // fold with the dock, on the dock's own measure
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
        onConfirmed: {
            closeConfirm.open = false
            shell.reportProgress(true)
            shell.activityEndSession()   // Activity (§9 Lane B): close/lifecycle exit ends the session
            shell.closeRequested()
        }
        onCancelled: closeConfirm.open = false
    }
}
