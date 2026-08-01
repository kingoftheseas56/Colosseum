// HostedPlayerPage.qml — the dedicated Theatre surface for hosted playback (VidKing).
//
// It is a black, full-window WebEngine surface that loads ONLY the local wrapper
// (qrc:/hostedplayer/host.html); the wrapper owns the cross-origin VidKing iframe. This
// page is the security cage around it:
//   - a dedicated OFF-THE-RECORD profile (no cookies, memory cache), destroyed with the
//     page when the Loader unloads (close/minimize) — no hosted state outlives its use;
//   - popups, top-level navigation away from the wrapper, downloads, and permission
//     requests are all refused; clipboard read/paste are pinned off;
//   - the ONLY object on the WebChannel is the least-privilege HostedPlayerBridge, so the
//     hosted iframe can reach nothing else in the app.
//
// It owns the Progress lifecycle: a 5-second silent heartbeat plus notifying writes on
// lifecycle boundaries, using the SAME payload shape and video id as mpv playback, so
// Continue Watching resumes VidKing. It never instantiates mpv and never downloads.
// (Theatre VidKing plan, 2026-08-02, Task 6.)
pragma ComponentBehavior: Bound
import QtQuick
import QtWebEngine
import QtWebChannel
import "HostedPlayerApi.js" as HostedPlayerApi

Item {
    id: page
    property Item backdrop: null

    // The typed request from TheatreSeries (Task 4) — providerId, extensionId, type,
    // imdbId, tmdbId, season, episode, mediaId, title, backdrop, position.
    property var request: ({})

    // Per-open opaque token: a late event from a previous title carries a stale token
    // and is ignored, so it can never write into the new title's progress.
    property string sessionToken: ""
    property int generation: 0

    property real lastPosition: 0
    property real duration: 0
    property real lastHeartbeatMs: 0
    property bool started: false
    property bool errored: false
    property string errorText: ""

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    Theme { id: theme }

    function nowMs() { return (new Date()).getTime() }

    // "1h 23m" / "45m" / "12s" — mirrors the player's "N left" copy, no invented parts.
    function formatTime(secs) {
        secs = Math.max(0, Math.floor(Number(secs) || 0))
        var h = Math.floor(secs / 3600)
        var m = Math.floor((secs % 3600) / 60)
        var s = secs % 60
        if (h > 0) return h + "h " + m + "m"
        if (m > 0) return m + "m"
        return s + "s"
    }

    // ---- public API (Main drives these) --------------------------------------

    function open(req) {
        page.request = req || ({})
        page._load(page.request, Number((page.request.position) || 0))
    }

    // Generation-safe load: validate + build the embed through the trusted registry,
    // mint a new session token, reset per-title state, then load the LOCAL wrapper with
    // the encoded embed URL + token. A validation failure shows the honest panel — it
    // never falls through to a stream.
    function _load(req, positionSeconds) {
        var embed = HostedPlayerApi.embedUrl(req.providerId, {
            "type": req.type, "tmdbId": req.tmdbId,
            "season": req.season, "episode": req.episode
        }, positionSeconds)
        if (!embed || !req.mediaId) { page.showError(); return }
        page.generation += 1
        page.sessionToken = "s" + page.nowMs() + "-" + page.generation
        page.lastPosition = Math.max(0, Number(positionSeconds) || 0)
        page.duration = 0
        page.lastHeartbeatMs = 0
        page.started = false
        page.errored = false
        page.errorText = ""
        web.url = "qrc:/hostedplayer/host.html?url=" + encodeURIComponent(embed)
                + "&session=" + encodeURIComponent(page.sessionToken)
        startupGuard.restart()
    }

    function captureState() {
        return { "position": page.lastPosition, "duration": page.duration,
                 "session": page.sessionToken }
    }

    // Restore rebuilds the embed at the captured position — WebEngine sessions are not
    // kept warm across a minimize (the profile was destroyed), so this re-loads.
    function restoreState(state) {
        if (!state) return
        var pos = Number(state.position || 0)
        if (pos > 0 && page.request && page.request.mediaId)
            page._load(page.request, pos)
    }

    // Minimize: write the final progress, then halt playback immediately. Main unloads
    // the Loader right after, destroying this page and its off-the-record profile.
    function suspendForMinimize() {
        page.recordProgress(true)
        web.url = "about:blank"
    }
    function resumeFromMinimize() { startupGuard.restart() }

    function stop() {
        page.recordProgress(true)
        web.url = "about:blank"
    }

    // ---- progress (same payload + id as mpv; ProgressStore owns the 90% rule) ----

    function recordProgress(notify) {
        if (typeof Progress === "undefined") return
        // 10-second anti-clutter floor: nothing written until 10s of position exists.
        if (page.duration <= 0 || page.lastPosition < 10) return
        var r = page.request
        var epPrefix = (r.type === "series" && r.season && r.episode)
                       ? ("S" + r.season + " · E" + r.episode + "  ·  ") : ""
        var payload = {
            "id": r.mediaId,
            "kind": "video",
            "caption": r.title,
            "title": r.title,
            "sub": epPrefix + page.formatTime(page.duration - page.lastPosition) + " left",
            "cover": r.backdrop,
            "c1": "#33445d", "c2": "#0c1118",
            "progress": Math.max(0, Math.min(1, page.lastPosition / page.duration)),
            "resume": {
                "hostedPlayerId": r.providerId,
                "extensionId": r.extensionId,
                "imdbId": r.imdbId,
                "tmdbId": r.tmdbId,
                "subType": r.type,
                "subId": r.mediaId,
                "season": r.season || 0,
                "episode": r.episode || 0,
                "position": page.lastPosition
            }
        }
        if (notify) Progress.record(payload)
        else Progress.recordSilent(payload)
    }

    function maybeHeartbeat() {
        if (page.duration <= 0 || page.lastPosition < 10) return
        var now = page.nowMs()
        if (now - page.lastHeartbeatMs < 5000) return   // at most once every 5s
        page.lastHeartbeatMs = now
        page.recordProgress(false)
    }

    function showError() {
        page.errored = true
        page.errorText = "VidKing could not find or start a source for this title."
        startupGuard.stop()
    }

    // ---- the bridge event stream (session-guarded) ---------------------------
    function handleEvent(event) {
        if (!event || event.session !== page.sessionToken) return   // stale / wrong title
        var ev = event.event
        if (Number(event.duration) > 0) page.duration = Number(event.duration)
        if (Number(event.currentTime) >= 0) page.lastPosition = Number(event.currentTime)
        if (ev === "error") { page.showError(); return }
        if (ev === "play" || ev === "playing" || ev === "timeupdate" || ev === "seeked") {
            page.started = true
            startupGuard.stop()
        }
        if (ev === "timeupdate") page.maybeHeartbeat()
        else if (ev === "pause" || ev === "seeked" || ev === "ended") page.recordProgress(true)
    }

    Connections {
        target: typeof HostedPlayerBridge !== "undefined" ? HostedPlayerBridge : null
        function onPlayerEvent(event) { page.handleEvent(event) }
    }

    // Honest startup guard: no usable playback event within 20s → the unavailable panel.
    Timer {
        id: startupGuard
        interval: 20000; repeat: false
        onTriggered: if (!page.started && !page.errored) page.showError()
    }

    // Component teardown (Loader unload on close/minimize) writes a final progress row.
    Component.onDestruction: page.recordProgress(true)

    // ---- surface -------------------------------------------------------------
    Rectangle { anchors.fill: parent; color: "#000000" }

    WebEngineProfile {
        id: hostedProfile
        offTheRecord: true
        httpCacheType: WebEngineProfile.MemoryHttpCache
        persistentCookiesPolicy: WebEngineProfile.NoPersistentCookies
        // A hosted player never downloads — refuse every download outright.
        onDownloadRequested: (download) => download.cancel()
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        visible: !page.errored
        backgroundColor: "#000000"
        profile: hostedProfile
        webChannel: WebChannel { id: hostedChannel }
        // Clipboard is pinned off — the hosted page can neither read nor write it.
        settings.javascriptCanAccessClipboard: false
        settings.javascriptCanPaste: false
        // The wrapper is a qrc page; it needs no file:// or remote local-content access.
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false

        // Register ONLY the least-privilege bridge — the hosted iframe reaches nothing else.
        Component.onCompleted: hostedChannel.registerObject("hostedPlayerBridge", HostedPlayerBridge)

        // Refuse popups / target=_blank — do not open a new window.
        onNewWindowRequested: (request) => { /* refused: intentionally unhandled */ }

        // Only the local wrapper (or about:blank on teardown) may be the top-level page.
        // The VidKing embed loads as a SUBFRAME, governed by host.html's CSP, not here.
        onNavigationRequested: (request) => {
            var u = String(request.url)
            if (u.indexOf("qrc:/hostedplayer/host.html") === 0
                    || u === "about:blank" || u.length === 0)
                request.action = WebEngineNavigationRequest.AcceptRequest
            else
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }

        // Deny every permission request (camera, mic, geolocation, notifications, …).
        onPermissionRequested: (permission) => permission.deny()

        // Fullscreen: accept the element request AND forward to the shell fullscreen action,
        // so the app window and the video element stay in step.
        onFullScreenRequested: (fsRequest) => { fsRequest.accept(); page.fullscreenRequested() }

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadFailedStatus) page.showError()
        }
    }

    // ---- literal status panel: starting / unavailable ------------------------
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: page.errored || (!page.started && web.visible)
        width: Math.min(parent.width - 96, 520)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.errored ? page.errorText : "Starting VidKing…"
            color: page.errored ? theme.ink : theme.inkDim
            font.family: theme.display; font.pixelSize: page.errored ? 22 : 18
            font.italic: !page.errored
            wrapMode: Text.WordWrap
        }
        Row {
            visible: page.errored
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 16
            Rectangle {
                width: backToSourcesT.implicitWidth + 40; height: 42; radius: 11
                color: btsMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                border.width: 1; border.color: theme.edge
                Text { id: backToSourcesT; anchors.centerIn: parent; text: "Back to Sources"
                       color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                MouseArea { id: btsMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: page.backRequested() }
            }
            Rectangle {
                width: retryT.implicitWidth + 40; height: 42; radius: 11; color: theme.gold
                Text { id: retryT; anchors.centerIn: parent; text: "Retry"
                       color: "#1a1306"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                MouseArea { anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { page.errored = false; page._load(page.request, page.lastPosition) } }
            }
        }
    }

    // ---- minimal window chrome (Colosseum's existing vocabulary) -------------
    BackAction {
        id: backBtn
        x: theme.margin; y: 28; z: 20
        onTriggered: page.backRequested()
    }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin
        y: 34; spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                    sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                    opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent
                    source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                            ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                    sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                    opacity: fsMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: fsMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: page.fullscreenRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                    sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                    opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: page.closeRequested() }
        }
    }
}
