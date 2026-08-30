import QtQuick
import Colosseum.Player2 1.0
import "../player2"
// QUALIFIED on purpose: an unqualified import of qml/ drags in its Theme.qml, which collides
// with the player2 module's Theme singleton and makes the whole page fail to load.
import ".." as Shipped

// Player2Page is Player 2 wearing PlayerPage's clothes.
//
// The app drives its video player from six places in Main.qml (openPlayer, closePlayer, closeSession,
// the movie arm of activateSession, captureSession, teardownSession). Rather than branch all six on
// which backend is active — the 1158-1184 block is a hot merge region other brothers touch — this page
// implements the SAME public interface PlayerPage.qml exposes, so the only production change is which
// source the existing Loader points at.
//
// Interface mirrored from qml/PlayerPage.qml: backdrop; backRequested/minimizeRequested/
// fullscreenRequested/closeRequested; playTorrent/playLocalFile/playRemoteUrl/stop/captureState/
// restoreState/suspendForMinimize/resumeFromMinimize.
Item {
    id: page
    anchors.fill: parent

    // --- PlayerPage's public surface ---------------------------------------------------------
    property Item backdrop
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    // Player 2 gave up on this playback before a frame was shown. There is no other backend to hand
    // it to in this process (mpv cannot render on D3D11) — this signal exists so Main.qml can log the
    // distinction from a post-first-frame failure; the page itself is the only destination, via
    // errorText below.
    signal backendFallback(string reason)
    // Player 2 died with the picture already up. No hot swap — the app surfaces it and starts clean.
    signal backendRestartRequired(string reason)

    // --- what we're playing -------------------------------------------------------------------
    property string mediaId: ""
    property string mediaTitle: ""
    property string mediaArt: ""
    property string subStreamType: ""
    property string subStreamId: ""
    property var playbackContext: ({})
    property real pendingSeekSec: 0
    // True between Stream.play() and streamReady/streamError - a torrent is warming up.
    property bool _awaitingStream: false

    // --- loader / error surface state ---------------------------------------------------------
    // Loader identity, derived exactly as the shipped player derives it (qml/PlayerPage.qml:1173):
    // most doors hand the player an empty context, so the metahub logo comes from the imdb id that
    // is already sitting in the artwork URL.
    property string mediaLogo: ""
    property string mediaLoadingArt: ""
    property string mediaLoadingLine: ""
    // The title bar's second line, same content the shipped player shows there.
    property string mediaSubtitle: ""
    // Set when the engine gives up. The page SHOWS this instead of the app closing the player out
    // from under the viewer - which is what it did before, and read as "the player just vanished".
    property string errorText: ""
    readonly property bool errored: page.errorText.length > 0
    // Player2State: 1 = Opening, 2 = Buffering, 5 = Seeking, 7 = Recovering (Player2Types.h).
    readonly property int _state: backend.session ? backend.session.state : 0
    // A seek into a torrent's not-yet-downloaded bytes is a deliberate wait on one held-open
    // connection - but the session stays in Seeking on purpose, so the state alone reads as
    // "nothing is happening". The source's own stall flag is what makes that wait visible here,
    // and it keeps this surface saying the same thing the transport bar says.
    readonly property bool _stalledSeek: backend.session ? (page._state === 5
                                                            && backend.session.networkStalled) : false
    // _starting raises the full-screen PlayerLoadingScreen, and _stalledSeek is deliberately NOT in
    // it. The shipped player sets its `starting` flag in exactly three places - the initial open
    // (PlayerPage.qml:1095), "Retrying stream..." (:1194) and "Reconnecting stream..." (:1345) - and
    // in NO seek path and NO mid-playback buffer path. It keeps the picture on screen through a
    // buffer and never throws the Stremio backdrop over it. So a stalled seek keeps the picture too
    // and speaks through the transport line instead. Do not "fix" this by adding _stalledSeek here.
    // ...and mid-playback Buffering/Recovering are NOT in it either, for the same reason. That was
    // the defect he saw on 2026-07-26: "the loading/bufferig screen is shaky. it alternates between
    // the slick per-show custom font to genric font in a blinking fashion." Every buffer hiccup
    // re-raised this whole full-screen surface, and because the hero Image clears its source when
    // the screen goes inactive, each raise showed the plain-text title first and snapped to the
    // show's logotype a moment later - the "blink" between a custom face and a generic one is the
    // LOGO being replaced by its text fallback, over and over.
    //
    // The shipped player does not do this because its `starting` is a latched flag, not a live read
    // of session state: it goes false once playback begins (PlayerPage.qml:917/1066/1077) and only
    // comes back for a stream retry/reconnect. `_hasPlayed` gives this page the same latch, so the
    // loader owns the screen until the first frame and never again for this playback - after that,
    // buffering speaks through the transport line, exactly as production does.
    property bool _hasPlayed: false
    readonly property bool _starting: !page.errored
                                      && (page._awaitingStream
                                          || (!page._hasPlayed
                                              && (page._state === 1 || page._state === 2
                                                  || page._state === 7)))

    function _statusText() {
        if (page.errored)
            return page.errorText
        if (page._awaitingStream)
            return "Starting stream..."
        if (page._state === 2 || page._state === 7 || page._stalledSeek)
            return "Buffering..."
        return "Loading..."
    }

    readonly property bool isSeries: page.subStreamType === "series"

    Player2Backend { id: backend }

    // --- the engine surface + chrome ----------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: "black"

        Player2VideoItem {
            id: videoSurface
            anchors.fill: parent
        }

        Player2Shell {
            id: shell
            anchors.fill: parent
            session: backend.session
            hostServices: hostServices

            mediaTitle: page.mediaTitle
            rootMediaId: page.mediaId
            currentEpisodeId: page.mediaId
            isSeries: page.isSeries
            activeSeason: hostServices._episodeMeta().season

            // Window state from the app, so the fullscreen control shows the right mode instead of
            // assuming one (the shell defaults to windowed and had never been told otherwise).
            windowed: (typeof WindowMode !== "undefined") ? WindowMode.shellWindowed : true
            mediaSubtitle: page.mediaSubtitle
            // Production's pause-card gate excludes `starting` and `errored`; the page owns both.
            loaderActive: page._starting || page.errored

            // These two were declared by the shell and connected to NOTHING (cross-model review,
            // 2026-07-26, P0). Every source pick in the drawer, and the HUD's new switch button,
            // reached a dead end in the integrated build: the signal fired and the app ignored it.
            // A control that appears to work is worse than one that is absent.
            onSwitchSourceRequested: function(index, sourceId) {
                page._switchToSource(index, sourceId)
            }
            onPlayEpisodeRequested: function(episodeId) {
                page._playEpisode(String(episodeId))
            }
            onFullscreenRequested: page.fullscreenRequested()
            onCloseRequested: page.closeRequested()
            onBackRequested: page.backRequested()
            onMinimizeRequested: page.minimizeRequested()
            onPipRequested: page.minimizeRequested()
            // Same call and the same reason string the shipped player passes (qml/PlayerPage.qml:2624).
            onKeepAwakeRequested: function(inhibit) {
                if (typeof Power !== "undefined")
                    Power.setInhibited(inhibit, page.mediaTitle || "Colosseum playback")
            }
        }
    }

    // The SHIPPED loading screen, reused verbatim rather than reimplemented - same Stremio-style
    // backdrop + logo + status + indeterminate bar the current player shows, so there is nothing to
    // drift. It also carries the error case, which is why a failed session no longer closes the page.
    Shipped.PlayerLoadingScreen {
        anchors.fill: parent
        z: 4
        active: page._starting || page.errored
        errored: page.errored
        title: (page.mediaTitle || "").replace(/\s+[-–—]\s+S\d+\s*E\d+.*$/i, "")
        episodeLine: page.mediaLoadingLine
        logoUrl: page.mediaLogo
        backdropUrl: page.mediaLoadingArt
        statusText: page._statusText()
        errorText: page._statusText()
        onCancelRequested: page.closeRequested()
    }

    // The latch that retires the loader: the first moment this playback is genuinely Playing (3).
    // A Binding with RestoreNone rather than a signal handler, because the change-signal name for an
    // underscore-prefixed property is ambiguous in QML and a handler that silently never fires would
    // leave the loader raised forever. _reset() clears it for the next playback.
    Binding {
        target: page
        property: "_hasPlayed"
        value: true
        when: page._state === 3
        restoreMode: Binding.RestoreNone
    }

    // The episode the drawer asked for: its sources come back here, and the top-ranked one plays.
    // Guarded on the id we asked for, so a late answer for an episode the viewer moved off cannot
    // hijack what is playing now.
    Connections {
        target: hostServices
        ignoreUnknownSignals: true
        function onAlternateSourcesResolved(mediaId, sources) {
            if (!page._pendingEpisodeId.length || String(mediaId) !== page._pendingEpisodeId)
                return
            const episodeId = page._pendingEpisodeId
            page._pendingEpisodeId = ""
            const rows = sources || []
            if (!rows.length) {
                page._failPlayback("No playable source found for that episode.")
                return
            }
            page.mediaId = episodeId   // the shell's currentEpisodeId binds to this
            page.subStreamId = episodeId
            page.pendingSeekSec = 0        // a different episode starts at its own resume point
            // The host retained the transport-grade candidates; rows here are display projection only.
            page._switchToSource(Number(rows[0].sourceIndex || 0), String(rows[0].id || ""))
        }
    }

    ColosseumHostServices {
        id: hostServices
        playbackContext: page.playbackContext
        mediaTitle: page.mediaTitle
        mediaArt: page.mediaArt
        subStreamType: page.subStreamType
        subStreamId: page.subStreamId
        durationSeconds: backend.session ? backend.session.duration : 0
        chapters: backend.session ? backend.session.chapters : []
    }

    Connections {
        target: backend
        function onFallbackRequested(reason) { page._failPlayback(reason) }
        function onRestartRequired(reason) {
            // Show it. Closing the player was the old behaviour and it read as the app vanishing.
            page.errorText = String(reason || "Player 2 could not continue this playback")
            page.backendRestartRequired(reason)
        }
    }

    // The torrent seam, same shape the shipped player uses (qml/PlayerPage.qml:2924).
    Connections {
        target: (typeof Stream !== "undefined") ? Stream : null
        ignoreUnknownSignals: true
        function onStreamReady(url, infoHash, fileIdx) {
            if (!page._awaitingStream)
                return
            page._awaitingStream = false
            page._open(String(url || ""))
        }
        function onStreamError(message) {
            if (!page._awaitingStream)
                return
            page._awaitingStream = false
            // The torrent never produced a URL. There is no other backend to hand this to in this
            // process, so the page's own error surface is the only honest destination.
            page._failPlayback(String(message || "the stream could not be started"))
        }
    }

    Component.onCompleted: backend.attachVideoItem(videoSurface)
    // Never leave the display-sleep inhibit held after the page goes away (PlayerPage.qml:2633).
    Component.onDestruction: if (typeof Power !== "undefined") Power.release()

    // --- the six entry points Main.qml uses ---------------------------------------------------

    function playTorrent(infoHash, fileIdx, title, posterUrl, subType, subId, streamCandidates, playbackContext) {
        page._reset()
        page.mediaTitle = title || ""
        page.mediaArt = posterUrl || ""
        page.subStreamType = subType || ""
        page.subStreamId = subId || ""
        page.playbackContext = playbackContext || ({})
        page.mediaId = page.subStreamId.length ? page.subStreamId : String(infoHash || "")
        page._applyLoaderIdentity(playbackContext, posterUrl)
        hostServices.streamCandidates = streamCandidates || []
        hostServices.mediaResumeHash = String(infoHash || "")
        hostServices.mediaResumeFileIdx = Number(fileIdx || 0)

        // A candidate carries either a direct URL (debrid/HTTP) or an infoHash the torrent sidecar
        // serves over loopback — the same two transports the shipped player distinguishes.
        var url = page._directUrlFor(streamCandidates, infoHash)
        if (url.length) {
            var directHeaders = page._directHeadersFor(streamCandidates, infoHash)
            page._open(url, directHeaders)
            return
        }
        if (String(infoHash || "").length) {
            // Torrent playback is ASYNCHRONOUS. Stream.streamUrl() is only a formatter and returns
            // EMPTY until the sidecar's HTTP server is listening; the real URL arrives on
            // streamReady. Asking for it synchronously made every torrent fall back with "no
            // playable source" on the first swap attempt (2026-07-25) — the shipped player has
            // always used play() -> streamReady, and so must this.
            page._awaitingStream = true
            hostServices.currentPlaybackUrl = ""
            Stream.play(String(infoHash), Number(fileIdx || 0))
            return
        }
        page._open("")   // nothing playable travelled with the request; let the router say so
    }

    function playLocalFile(target) {
        var t = target || ({})
        page._reset()
        page.mediaTitle = t.title || ""
        page.mediaArt = t.art || ""
        page.playbackContext = t.playbackContext || ({})
        hostServices.mediaLocalPath = String(t.localPath || "")
        page.mediaId = (t.id && String(t.id).length) ? String(t.id) : ("local:" + hostServices.mediaLocalPath)
        page._applyLoaderIdentity(t.playbackContext, t.art)
        page._open(page._fileUrl(hostServices.mediaLocalPath))
    }

    function playRemoteUrl(target) {
        var t = target || ({})
        page._reset()
        page.mediaTitle = t.title || ""
        page.mediaArt = t.art || ""
        page.mediaId = (t.id && String(t.id).length) ? String(t.id) : ("arriving:" + String(t.streamUrl || ""))
        page._applyLoaderIdentity(t.playbackContext, t.art)
        page._open(String(t.streamUrl || ""), t.headers)
    }

    function stop() {
        backend.stop()
    }

    // The engine's own frame counters, for probes and diagnostics. Presented > 0 is the only honest
    // proof that a picture actually reached the screen - session state and audio both look healthy
    // while the video is black.
    function sessionSeek(sec) { if (backend.session) backend.session.seekExact(sec) }
    function sessionState() { return backend.session ? backend.session.state : -1 }
    function sessionDuration() { return backend.session ? backend.session.duration : -1 }
    function sessionPosition() { return backend.session ? backend.session.position : -1 }
    function sessionNetworkStalled() { return backend.session ? backend.session.networkStalled : false }
    // -1 when the stream never declared a length, or for a local file: the seek bar draws no cache
    // strip for either, which is the shipped player's behaviour too.
    function sessionBufferedSeconds() { return backend.session ? backend.session.bufferedSeconds : -1 }
    function statusText() { return page._statusText() }
    // Is the full-screen loading surface up? A stalled seek must answer FALSE - the picture stays.
    function loadingActive() { return page._starting || page.errored }
    function sessionPause() { if (backend.session) backend.session.pause() }
    function sessionPlay() { if (backend.session) backend.session.play() }
    // Is the chrome up? The auto-hide timer holds while paused/buffering and must RE-ARM, so this
    // has to go false on its own once playback resumes - with no mouse input at all.
    function chromeShown() { return shell.controlsShown }

    function diagnosticsSnapshot() {
        return (backend.session && backend.session.diagnostics) ? backend.session.diagnostics() : ({})
    }

    function captureState() {
        return { "position": (backend.session && backend.session.position > 0) ? backend.session.position : 0 }
    }

    function restoreState(st) {
        var p = Number((st || ({})).position || 0)
        if (p > 0)
            page.pendingSeekSec = p
    }

    // Minimize keeps the engine alive — the same reason the mpv host is never torn down: recreating
    // it per play is where the use-after-free teardown trap lives.
    function suspendForMinimize() {
        if (backend.session)
            backend.session.pause()
    }

    function resumeFromMinimize() {
        if (backend.session)
            backend.session.play()
    }

    // --- internals ----------------------------------------------------------------------------

    // A retry or reconnect re-arms the loader, because the shipped player raises `starting` again in
    // exactly those two places (its "Retrying stream..." and "Reconnecting stream..." paths) even
    // after playback has begun. The latch alone only covered the FIRST open, which the cross-model
    // review flagged: without this, a re-resolve after first play would show a frozen picture with
    // no indication anything was happening.
    function _rearmLoader() {
        page._hasPlayed = false
    }

    function _reset() {
        page.pendingSeekSec = 0
        page.errorText = ""
        // A new playback earns the loader again from scratch.
        page._hasPlayed = false
        // Drop any torrent still warming up for the PREVIOUS media, so its late streamReady cannot
        // open the wrong thing over what we are about to play.
        page._awaitingStream = false
        hostServices.streamCandidates = []
        hostServices.currentStreamIndex = -1
        hostServices.deadStreamKeys = ({})
        hostServices.mediaLocalPath = ""
        hostServices.mediaResumeHash = ""
        hostServices.mediaResumeFileIdx = 0
        hostServices.invalidate()
    }

    // Mirrors qml/PlayerPage.qml:1173-1179 exactly.
    function _applyLoaderIdentity(context, posterUrl) {
        var ctx = context || ({})
        var m = String(posterUrl || "").match(/\/(tt\d+)\//)
        var ttId = m ? m[1] : ""
        page.mediaLogo = ctx.logo
            || (ttId ? "https://live.metahub.space/logo/medium/" + ttId + "/img" : "")
        page.mediaLoadingArt = ctx.episodeStill || ctx.loaderBackdrop || posterUrl || ""
        page.mediaLoadingLine = ctx.episodeLine || ""
        page.mediaSubtitle = ctx.episodeLine || ""
    }

    // Switch to another already-ranked source for the SAME media, carrying the position across so
    // the viewer resumes where they were — the shipped player's switch-in-place behaviour. The
    // candidate list is the one the door handed us; picking row N means playing row N's transport,
    // which is a direct URL for debrid/HTTP and the torrent sidecar otherwise.
    function _switchToSource(index, sourceId) {
        var rows = hostServices.streamCandidates || []
        var i = Number(index)
        var row = (sourceId && hostServices._candidateForKey)
                ? hostServices._candidateForKey(String(sourceId)) : null
        if (!row) {
            if (!(i >= 0 && i < rows.length))
                return
            row = rows[i] || ({})
        } else {
            for (var r = 0; r < rows.length; r++)
                if (rows[r] === row) { i = r; break }
        }
        page.pendingSeekSec = (backend.session && backend.session.position > 0)
                              ? backend.session.position : 0
        hostServices.currentStreamIndex = i
        page.errorText = ""
        page._awaitingStream = false
        page._rearmLoader()   // a re-resolve shows the loader again, as production's retry does
        if (row.url && String(row.url).length) {
            hostServices.mediaResumeHash = String(row.infoHash || "")
            hostServices.mediaResumeFileIdx = Number(row.fileIdx || 0)
            page._open(String(row.url), row.headers)
            return
        }
        if (String(row.infoHash || "").length) {
            // Same asynchronous rule as the first open: the sidecar's URL only exists once it is
            // listening, so ask Stream.play and wait for streamReady rather than formatting a URL.
            hostServices.mediaResumeHash = String(row.infoHash)
            hostServices.mediaResumeFileIdx = Number(row.fileIdx || 0)
            page._awaitingStream = true
            hostServices.currentPlaybackUrl = ""
            Stream.play(String(row.infoHash), Number(row.fileIdx || 0))
            return
        }
        page._failPlayback("That source has nothing playable attached.")
    }

    // Play another episode chosen in the drawer. Its sources are resolved through the SAME host seam
    // the drawer itself uses, then the best one is played — no second ranking policy.
    property string _pendingEpisodeId: ""
    function _playEpisode(episodeId) {
        if (!episodeId.length || !hostServices)
            return
        page._pendingEpisodeId = episodeId
        hostServices.requestAlternateSources(episodeId)
    }

    function _directUrlFor(candidates, infoHash) {
        var rows = candidates || []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            if (String(c.infoHash || "") !== String(infoHash || ""))
                continue
            if (c.url && String(c.url).length)
                return String(c.url)
        }
        // The shipped player routes a direct link by stuffing it into infoHash as "url:<link>"
        // (qml/AddonClient.js:199) — honour that convention rather than inventing a second one.
        var hash = String(infoHash || "")
        return (hash.indexOf("url:") === 0) ? hash.substring(4) : ""
    }

    function _directHeadersFor(candidates, infoHash) {
        var rows = candidates || []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            if (String(c.infoHash || "") === String(infoHash || "")
                    && c.headers && typeof c.headers === "object" && !Array.isArray(c.headers))
                return c.headers
        }
        return ({})
    }

    function _fileUrl(path) {
        var p = String(path || "")
        if (!p.length)
            return ""
        return (p.indexOf("file:") === 0) ? p : ("file:///" + p.replace(/\\/g, "/"))
    }

    // Every path that gives up on a playback ends here: there is no other backend in this process,
    // so the page's own error surface is the only honest destination.
    function _failPlayback(reason) {
        page.errorText = String(reason || "Player 2 could not play this")
        page.backendFallback(page.errorText)
    }

    function _open(url, headers) {
        var requestHeaders = (headers && typeof headers === "object" && !Array.isArray(headers))
                           ? headers : ({})
        var request = {
            "url": String(url || ""),
            "mediaId": page.mediaId,
            "title": page.mediaTitle,
            "resumeSeconds": page.pendingSeekSec,
            "live": String(page.subStreamId || "").indexOf("iptv:") === 0,
            "headers": requestHeaders
        }
        hostServices.currentPlaybackUrl = request.url

        var decision = backend.play(request)
        if (decision.outcome === "player2")
            return

        // A router decline belongs on the page's one visible failure funnel.
        page._failPlayback(decision.reason || "Player 2 declined this playback")
    }
}
