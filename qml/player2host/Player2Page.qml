import QtQuick
import Colosseum.Player2 1.0
import "../player2"

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

    // Player 2 could not carry this playback. The app hands the SAME request to mpvqt (see Main.qml);
    // this is the one legal backend swap and it only ever happens before a frame is shown.
    signal backendFallback(string reason, var request)
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
    // The last request handed to the engine, kept so a fallback can be replayed on mpvqt verbatim
    // rather than reconstructed (a reconstruction is where a "fallback played the wrong thing" bug
    // would live).
    property var _lastRequest: ({})
    // True between Stream.play() and streamReady/streamError - a torrent is warming up.
    property bool _awaitingStream: false

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

            onFullscreenRequested: page.fullscreenRequested()
            onCloseRequested: page.closeRequested()
            onPipRequested: page.minimizeRequested()
            // Same call and the same reason string the shipped player passes (qml/PlayerPage.qml:2624).
            onKeepAwakeRequested: function(inhibit) {
                if (typeof Power !== "undefined")
                    Power.setInhibited(inhibit, page.mediaTitle || "Colosseum playback")
            }
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
        function onFallbackRequested(reason) { page.backendFallback(reason, page._lastRequest) }
        function onRestartRequired(reason) { page.backendRestartRequired(reason) }
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
            // The torrent never produced a URL. Player 2 has no retry/pick-another-source machinery
            // yet (that lives in the shipped player), so hand it over rather than sit on a dead page.
            page.backendFallback(String(message || "the stream could not be started"), page._lastRequest)
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
        hostServices.streamCandidates = streamCandidates || []
        hostServices.mediaResumeHash = String(infoHash || "")
        hostServices.mediaResumeFileIdx = Number(fileIdx || 0)

        // A candidate carries either a direct URL (debrid/HTTP) or an infoHash the torrent sidecar
        // serves over loopback — the same two transports the shipped player distinguishes.
        var url = page._directUrlFor(streamCandidates, infoHash)
        if (url.length) {
            page._open(url)
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
        page._open(page._fileUrl(hostServices.mediaLocalPath))
    }

    function playRemoteUrl(target) {
        var t = target || ({})
        page._reset()
        page.mediaTitle = t.title || ""
        page.mediaArt = t.art || ""
        page.mediaId = (t.id && String(t.id).length) ? String(t.id) : ("arriving:" + String(t.streamUrl || ""))
        page._open(String(t.streamUrl || ""))
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

    function _reset() {
        page.pendingSeekSec = 0
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

    function _fileUrl(path) {
        var p = String(path || "")
        if (!p.length)
            return ""
        return (p.indexOf("file:") === 0) ? p : ("file:///" + p.replace(/\\/g, "/"))
    }

    function _open(url) {
        var request = {
            "url": String(url || ""),
            "mediaId": page.mediaId,
            "title": page.mediaTitle,
            "resumeSeconds": page.pendingSeekSec,
            "live": String(page.subStreamId || "").indexOf("iptv:") === 0,
            "headers": ({})
        }
        page._lastRequest = request
        hostServices.currentPlaybackUrl = request.url

        var decision = backend.play(request)
        if (decision.outcome === "player2")
            return

        // Anything else means Player 2 is not carrying this playback. Nothing has been shown yet, so
        // handing it to mpvqt is invisible — but it is never silent: the reason travels with it.
        page.backendFallback(String(decision.reason || "Player 2 declined this playback"), request)
    }
}
