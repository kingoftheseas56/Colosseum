pragma ComponentBehavior: Bound

// PlayerPage - Harbor/TB3-style fullscreen player chrome on top of Colosseum's mpvqt MpvItem.
// Streaming remains behind the Stream.play -> streamReady seam; this file only owns player UI.
import QtQuick
import QtQuick.Window
import Colosseum.Player
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio

Item {
    id: root
    anchors.fill: parent
    focus: true

    property Item backdrop
    property string mediaTitle: ""
    property string mediaSubtitle: ""
    // --- continue/resume identity (set by openPlayer; fed to the Progress store) ---
    property string mediaId: ""           // stable id (Cinemeta ttXXXX if known, else infoHash)
    property string mediaArt: ""          // poster url, for the Continue card cover
    property string mediaResumeHash: ""   // resume payload: re-open this torrent...
    property int    mediaResumeFileIdx: 0 //   ...at this file index

    // --- online subtitles (Harbor-style: torrent streams have no subs, so fetch them) ---
    property string subStreamType: ""     // "movie" | "series" (for the OpenSubtitles query)
    property string subStreamId: ""       // "tt123" or "tt123:1:2"
    property var    onlineSubs: []        // [{id,url,lang,langName,external,...}] from Subtitles.js
    property var    addedOnlineUrls: ({}) // url -> true once loaded into mpv (drops it from the online list)
    property int    subModelRev: 0        // bump to re-evaluate subRows after an add
    property bool   subsLoading: false
    property bool   fileReady: false      // mpv has a file open (needed before sub-add)
    property bool   autoSubDone: false    // auto-load ran for this file
    property bool   userTouchedSubs: false// user picked Off/a track → stop auto-overriding
    property bool   subtitleDropToastOpen: false
    property string subtitleDropToastText: ""
    property bool   subtitleDropToastFailed: false

    property var streamCandidates: []
    property int currentStreamIndex: -1
    property int streamRetryCount: 0
    property int streamWatchdogSeconds: 75
    property var deadStreamKeys: ({})
    property string stubCheckedKey: ""
    property int stubDurationThresholdSec: 60
    property var adjacentEpisodes: ({})
    property int adjacentResolveGen: 0
    // Harbor-style "Up Next" countdown between episodes: visible + cancelable,
    // not a silent jump. Only shown when a next episode actually exists.
    property bool upNextVisible: false
    property int upNextCountdownSec: 10
    property int upNextRemainingSec: 0
    property bool roomPanelOpen: false
    property bool applyingRoomSync: false
    property bool roomReady: false
    property string roomChatDraft: ""
    property int roomDriftToleranceSeconds: 3
    property bool castPanelOpen: false
    property string currentPlaybackUrl: ""
    property bool liveGuideOpen: false
    property bool dvrPanelOpen: false
    property string currentDvrId: ""
    property bool pipMode: typeof WindowMode !== "undefined" && WindowMode.pipMode
    // Harbor pause-on-minimize (pauseMinimized:true default): pause when the window is
    // minimized, resume on restore — but only ever undoing a pause we caused.
    property bool autoPausedInactive: false
    readonly property bool windowMinimized: root.Window.window ? (root.Window.window.visibility === Window.Minimized) : false
    onWindowMinimizedChanged: root.handleWindowMinimize()
    property bool frameGrabToastOpen: false
    property string frameGrabToastText: ""
    property string frameGrabPath: ""
    property bool frameGrabFailed: false
    property string gifState: "idle"
    property int gifElapsedSec: 0
    property int gifMaxSeconds: 30
    property real abLoopA: -1
    property real abLoopB: -1
    property bool abLoopSeeking: false
    property string sleepTimerMode: "off"
    property real sleepTimerFiresAt: 0
    property real sleepTimerRemainingMs: 0
    property int sleepEndEpisodesRemaining: 0
    property bool statsOverlayOpen: false
    property var playbackStats: ({})
    property bool drawMode: false
    property var drawStrokes: []
    property string drawColor: "#f4b23c"
    property string activeDrawStrokeId: ""
    property int drawStrokeLifetimeMs: 9500

    // Combined subtitle list: embedded/loaded mpv tracks + online subs not yet loaded.
    readonly property var subRows: {
        var dep = root.subModelRev            // re-eval after an add
        var rows = mpv.subtitleTracks.slice() // embedded (and any online already sub-added → external)
        var rawRows = rows
        rows = []
        for (var t = 0; t < rawRows.length; t++)
            rows.push(root.subtitleRow(rawRows[t]))
        for (var i = 0; i < root.onlineSubs.length; i++) {
            var s = root.onlineSubs[i]
            if (!root.addedOnlineUrls[s.url]) rows.push(root.onlineSubtitleRow(s))
        }
        return rows
    }

    readonly property var audioRows: {
        var rows = []
        for (var i = 0; i < mpv.audioTracks.length; i++)
            rows.push(root.audioRow(mpv.audioTracks[i]))
        return rows
    }
    readonly property var subtitleSearchMeta: root.parseSubtitleMeta()

    function fetchSubtitles() {
        root.onlineSubs = []
        root.addedOnlineUrls = ({})
        root.autoSubDone = false
        root.userTouchedSubs = false
        root.subsLoading = false
        if (!root.subStreamType.length || !root.subStreamId.length)
            return
        root.subsLoading = true
        var reqId = root.subStreamId
        Subtitles.fetch(root.subStreamType, root.subStreamId, function(list) {
            if (root.subStreamId !== reqId) return    // a newer play superseded this fetch
            root.subsLoading = false
            root.onlineSubs = list
            root.maybeAutoSub()
        })
    }

    // Route a subtitle pick: online → download/add into mpv; embedded → just select.
    function pickSubtitle(id) {
        if (("" + id).indexOf("ext:") === 0) {
            for (var i = 0; i < root.onlineSubs.length; i++) {
                if (root.onlineSubs[i].id === id) {
                    var s = root.onlineSubs[i]
                    root.addedOnlineUrls[s.url] = true
                    root.subModelRev++
                    mpv.addSubtitle(s.url, s.title || s.langName, s.lang, true)
                    return
                }
            }
            return
        }
        mpv.subtitleTrack = id
    }

    function addOnlineSubtitle(url, title, lang) {
        if (!url || !url.length)
            return
        root.userTouchedSubs = true
        root.addedOnlineUrls[url] = true
        root.subModelRev++
        mpv.addSubtitle(url, title || "OpenSubtitles", lang || "", true)
    }

    function loadSubtitleFile(fileUrl) {
        if (!fileUrl)
            return false
        root.userTouchedSubs = true
        var raw = String(fileUrl)
        root.addedOnlineUrls[raw] = true
        root.subModelRev++
        mpv.addSubtitle(raw, root.subtitleBasename(raw), "", true)
        return true
    }

    function isSubtitleFile(fileUrl) {
        var s = decodeURIComponent(String(fileUrl || "")).toLowerCase()
        var q = s.indexOf("?")
        if (q >= 0)
            s = s.slice(0, q)
        return s.endsWith(".srt") || s.endsWith(".ass") || s.endsWith(".ssa")
            || s.endsWith(".vtt") || s.endsWith(".sub")
    }

    function showSubtitleDropToast(ok, fileUrl) {
        var name = root.subtitleBasename(fileUrl || "Subtitle")
        root.subtitleDropToastText = ok ? ("Loaded " + name) : ("Couldn't load " + name)
        root.subtitleDropToastFailed = !ok
        root.subtitleDropToastOpen = true
        subtitleDropToastTimer.restart()
        root.wakeChrome()
    }

    function loadDroppedSubtitle(fileUrl) {
        if (!root.isSubtitleFile(fileUrl)) {
            root.showSubtitleDropToast(false, fileUrl)
            return false
        }
        var ok = root.loadSubtitleFile(fileUrl)
        root.showSubtitleDropToast(ok, fileUrl)
        return ok
    }

    function subtitleBasename(fileUrl) {
        var s = decodeURIComponent(String(fileUrl || ""))
        s = s.replace(/^file:\/+/, "")
        s = s.replace(/\\/g, "/")
        var parts = s.split("/")
        return parts.length ? parts[parts.length - 1] : "Subtitle"
    }

    function subtitleRow(track) {
        var title = track.title || track.label || track.lang || "Subtitle"
        var lang = track.lang || ""
        var hiProbe = (title + " " + lang).toLowerCase()
        return {
            "id": String(track.id || ""),
            "label": title,
            "lang": lang,
            "codec": track.codec || "",
            "channels": track.channels || "",
            "external": !!track.external,
            "forced": !!track.forced,
            "hearingImpaired": /sdh|hearing|\bhi\b/i.test(hiProbe),
            "default": !!track.default,
            "url": track.url || "",
            "title": title,
            "selected": !!track.selected
        }
    }

    function onlineSubtitleRow(subtitle) {
        return {
            "id": String(subtitle.id || ""),
            "label": subtitle.langName || subtitle.label || subtitle.lang || "OpenSubtitles",
            "lang": subtitle.lang || "",
            "codec": subtitle.codec || "srt",
            "channels": "",
            "external": true,
            "forced": !!subtitle.forced,
            "hearingImpaired": !!subtitle.hearingImpaired,
            "default": !!subtitle.default,
            "url": subtitle.url || "",
            "downloads": subtitle.downloads || 0,
            "title": subtitle.title || subtitle.langName || "OpenSubtitles"
        }
    }

    function audioRow(track) {
        return {
            "id": String(track.id || ""),
            "label": track.label || track.title || track.lang || "Audio track",
            "lang": track.lang || "",
            "codec": track.codec || "",
            "channels": track.channels || "",
            "default": !!track.default,
            "selected": !!track.selected
        }
    }

    function parseSubtitleMeta() {
        var id = root.subStreamId || ""
        var parts = id.split(":")
        return {
            "imdbId": parts.length ? parts[0] : "",
            "season": parts.length > 1 ? Number(parts[1]) : undefined,
            "episode": parts.length > 2 ? Number(parts[2]) : undefined,
            "type": root.subStreamType || (parts.length > 2 ? "series" : "movie")
        }
    }

    // Harbor default: auto-load the preferred-language (English) sub once the file is open,
    // unless something is already selected or the user turned subs off/picked one.
    function maybeAutoSub() {
        if (root.autoSubDone || root.userTouchedSubs || !root.fileReady)
            return
        if (mpv.subtitleTrack !== "") { root.autoSubDone = true; return }
        var pick = Subtitles.pickDefault(root.onlineSubs)
        if (!pick)
            return
        root.autoSubDone = true
        root.addedOnlineUrls[pick.url] = true
        root.subModelRev++
        mpv.addSubtitle(pick.url, pick.title || pick.langName, pick.lang, true)
    }
    property bool starting: false
    property bool errored: false
    property string statusMsg: ""
    property bool controlsShown: true
    property bool seeking: false
    property real seekPreview: mpv.position
    property real seekBackSeconds: 10
    property real seekForwardSeconds: 10
    property real spaceBaseSpeed: 1
    property bool spaceHoldFired: false
    property int fillModeIndex: 0
    readonly property real chromeScaleY: {
        var w = root.Window.window
        return (w && w.screen && w.screen.devicePixelRatio > 0) ? w.screen.devicePixelRatio : 1
    }
    // NOTE: QML lays out in LOGICAL pixels; mpvqt composites correctly under QML overlays
    // regardless of devicePixelRatio. Dividing by DPR shrank the chrome box and parked the
    // transport mid-screen (the swallow it was meant to fix was actually a z-order bug, since
    // solved by chrome z:99999 over mpv z:-1). So chrome fills the TRUE window.
    readonly property real chromeVisibleWidth: width
    readonly property real chromeVisibleHeight: height
    readonly property bool compact: chromeVisibleWidth < 1000
    readonly property bool tight: chromeVisibleWidth < 680
    readonly property bool anyMenuOpen: audioMenu.panelOpen || subMenu.panelOpen || speedMenu.panelOpen || fillMenu.panelOpen || subStyleBar.open || root.roomPanelOpen || root.castPanelOpen || root.liveGuideOpen || root.dvrPanelOpen || toolsMenu.panelOpen
    readonly property bool abLoopActive: root.abLoopA >= 0 && root.abLoopB > root.abLoopA
    readonly property bool sleepTimerActive: root.sleepTimerMode !== "off"
    readonly property var speedChoices: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    readonly property var sleepPresets: [
        { "id": "15", "label": "15 min", "mode": "minutes", "minutes": 15 },
        { "id": "30", "label": "30 min", "mode": "minutes", "minutes": 30 },
        { "id": "45", "label": "45 min", "mode": "minutes", "minutes": 45 },
        { "id": "60", "label": "1 hour", "mode": "minutes", "minutes": 60 },
        { "id": "120", "label": "2 hours", "mode": "minutes", "minutes": 120 },
        { "id": "180", "label": "3 hours", "mode": "minutes", "minutes": 180 },
        { "id": "240", "label": "4 hours", "mode": "minutes", "minutes": 240 },
        { "id": "360", "label": "6 hours", "mode": "minutes", "minutes": 360 },
        { "id": "ep", "label": "End of episode", "mode": "end_episode", "minutes": 0 },
        { "id": "ep2", "label": "End of next episode", "mode": "end_next_episode", "minutes": 0 }
    ]
    readonly property var fillModes: [
        { id: "fit", label: "Fit", panscan: 0, zoom: 0, aspect: "-1" },
        { id: "fill", label: "Fill", panscan: 1, zoom: 0, aspect: "-1" },
        { id: "zoom", label: "Zoom", panscan: 0, zoom: 0.35, aspect: "-1" },
        { id: "16:9", label: "16:9", panscan: 0, zoom: 0, aspect: "16:9" },
        { id: "4:3", label: "4:3", panscan: 0, zoom: 0, aspect: "4:3" },
        { id: "scope", label: "2.39:1", panscan: 0, zoom: 0, aspect: "2.39:1" }
    ]

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    function normalizeStreamCandidates(infoHash, fileIdx, title, candidates) {
        var out = []
        var rows = candidates || []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            if (!c.infoHash || !String(c.infoHash).length)
                continue
            out.push({
                "infoHash": String(c.infoHash),
                "fileIdx": c.fileIdx !== undefined ? Number(c.fileIdx) : 0,
                "title": c.release || c.title || title || "Stream",
                "quality": c.qualityLine || c.quality || "",
                "seeders": c.seeders !== undefined ? c.seeders : -1,
                "sourceName": c.sourceName || c.addonName || "Torrentio",
                "url": c.url || ""
            })
        }
        if (!out.length && infoHash && String(infoHash).length) {
            out.push({
                "infoHash": String(infoHash),
                "fileIdx": fileIdx || 0,
                "title": title || "Stream",
                "quality": "",
                "seeders": -1,
                "sourceName": "Torrentio"
            })
        }
        return out
    }

    function findStreamIndex(infoHash, fileIdx) {
        var hash = String(infoHash || "").toLowerCase()
        for (var i = 0; i < root.streamCandidates.length; i++) {
            var c = root.streamCandidates[i] || ({})
            if (String(c.infoHash || "").toLowerCase() === hash && Number(c.fileIdx || 0) === Number(fileIdx || 0))
                return i
        }
        return root.streamCandidates.length ? 0 : -1
    }

    function streamCandidateKey(candidateOrHash, maybeFileIdx) {
        var c = (typeof candidateOrHash === "object" && candidateOrHash !== null) ? candidateOrHash : null
        var hash = c ? c.infoHash : candidateOrHash
        var fileIdx = c ? c.fileIdx : maybeFileIdx
        return String(hash || "").toLowerCase() + ":" + Number(fileIdx || 0)
    }

    function currentStreamCandidate() {
        if (root.currentStreamIndex < 0 || root.currentStreamIndex >= root.streamCandidates.length)
            return ({})
        return root.streamCandidates[root.currentStreamIndex] || ({})
    }

    function isStreamDead(candidateOrIndex) {
        var c = (typeof candidateOrIndex === "number") ? (root.streamCandidates[candidateOrIndex] || ({})) : candidateOrIndex
        var key = root.streamCandidateKey(c)
        return key.length > 1 && root.deadStreamKeys[key] !== undefined
    }

    function markStreamDead(candidateOrIndex, reason) {
        var c = (typeof candidateOrIndex === "number") ? (root.streamCandidates[candidateOrIndex] || ({})) : candidateOrIndex
        var key = root.streamCandidateKey(c)
        if (key.length <= 1)
            return
        root.deadStreamKeys[key] = reason || "dead"
        root.deadStreamKeys = Object.assign({}, root.deadStreamKeys)
    }

    function nextPlayableStreamIndex(afterIndex) {
        if (root.streamCandidates.length <= 0)
            return -1
        var start = Math.max(-1, Number(afterIndex || -1))
        for (var offset = 1; offset <= root.streamCandidates.length; offset++) {
            var idx = (start + offset) % root.streamCandidates.length
            if (idx !== root.currentStreamIndex && !root.isStreamDead(idx))
                return idx
        }
        return -1
    }

    function detectStubStream() {
        if (!root.fileReady || root.starting || root.errored || mpv.pause)
            return false
        if (root.subStreamId.indexOf("iptv:") === 0 || root.mediaId.indexOf("iptv:") === 0)
            return false
        if (!(mpv.duration > 0 && mpv.duration < root.stubDurationThresholdSec))
            return false
        var c = root.currentStreamCandidate()
        var key = root.streamCandidateKey(c)
        if (!key.length || root.stubCheckedKey === key)
            return false
        root.stubCheckedKey = key
        var reason = "stub_" + Math.round(mpv.duration) + "s"
        root.markStreamDead(c, reason)
        streamWatchdog.stop()
        root.recordProgress()
        var next = root.nextPlayableStreamIndex(root.currentStreamIndex)
        if (next >= 0) {
            root.statusMsg = "This source is only " + Math.round(mpv.duration) + "s. Trying another stream..."
            root.playStreamAt(next, "switch")
        } else {
            root.errored = true
            root.starting = false
            root.fileReady = false
            root.statusMsg = "This source is only " + Math.round(mpv.duration) + "s. No alternate stream available."
            root.wakeChrome()
        }
        return true
    }

    function resolveAdjacentContext(playbackContext) {
        var ctx = playbackContext || ({})
        if (ctx.episodeQueue && ctx.episodeIndex !== undefined) {
            var queue = ctx.episodeQueue || []
            var idx = Number(ctx.episodeIndex)
            function withQueue(target, index) {
                if (!target)
                    return null
                var out = Object.assign({}, target)
                out.context = { "episodeQueue": queue, "episodeIndex": index }
                return out
            }
            return {
                "prev": idx > 0 ? withQueue(queue[idx - 1], idx - 1) : null,
                "next": (idx >= 0 && idx + 1 < queue.length) ? withQueue(queue[idx + 1], idx + 1) : null
            }
        }
        return ctx.adjacentEpisodes || ({})
    }

    function playStreamAt(index, reason) {
        if (index < 0 || index >= root.streamCandidates.length) {
            root.errored = true
            root.starting = false
            root.statusMsg = "No playable stream."
            root.wakeChrome()
            return
        }
        if (root.isStreamDead(index)) {
            var replacement = root.nextPlayableStreamIndex(index)
            if (replacement >= 0) {
                root.playStreamAt(replacement, reason)
            } else {
                root.errored = true
                root.starting = false
                root.statusMsg = "No playable stream."
                root.wakeChrome()
            }
            return
        }
        var c = root.streamCandidates[index] || ({})
        root.currentStreamIndex = index
        root.streamRetryCount = 0
        root.mediaResumeHash = c.infoHash || ""
        root.mediaResumeFileIdx = c.fileIdx || 0
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = reason === "switch" ? "Switching stream..."
                       : reason === "retry" ? "Retrying stream..."
                       : "Starting stream..."
        streamWatchdog.restart()
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        // Direct-url streams (debrid / HTTP hosts / live-tv extensions) skip the
        // torrent engine — mpv plays the url natively. They arrive either as an
        // explicit url field or under the "url:" infoHash routing prefix (the
        // resume path carries only the hash). Extensions spec Phase 2, slice G.
        var directUrl = (c.url && String(c.url).length) ? String(c.url)
                      : (String(c.infoHash || "").indexOf("url:") === 0
                         ? String(c.infoHash).substring(4) : "")
        if (directUrl.length) {
            root.mediaSubtitle = "Direct stream"
            root.currentPlaybackUrl = directUrl
            mpv.loadFile(directUrl)
            return
        }
        root.mediaSubtitle = "Torrent stream"
        Stream.play(c.infoHash, c.fileIdx || 0)
    }

    function playTorrent(infoHash, fileIdx, title, posterUrl, subType, subId, streamCandidates, playbackContext) {
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.mediaTitle = title || ""
        root.mediaSubtitle = "Torrent stream"
        root.mediaArt = posterUrl || ""
        // Stable id: series episodes use the exact stream id (tt:season:episode) so the
        // detail page can paint per-episode progress; movies keep the base Cinemeta id.
        var m = String(posterUrl || "").match(/\/(tt\d+)\//)
        root.mediaId = (subType === "series" && subId) ? subId
                     : ((m && m[1]) ? m[1] : (infoHash + ":" + fileIdx))
        root.deadStreamKeys = ({})
        root.stubCheckedKey = ""
        root.autoPausedInactive = false
        root.cancelUpNext()
        root.streamCandidates = root.normalizeStreamCandidates(infoHash, fileIdx, title, streamCandidates)
        root.currentStreamIndex = root.findStreamIndex(infoHash, fileIdx)
        root.adjacentEpisodes = root.resolveAdjacentContext(playbackContext)
        // Online subtitles for this exact title/episode (Harbor-style).
        root.subStreamType = subType || ""
        root.subStreamId = subId || ""
        root.fetchSubtitles()
        root.playStreamAt(root.currentStreamIndex, "start")
    }

    function retryCurrentStream() {
        if (root.currentStreamIndex < 0 || root.currentStreamIndex >= root.streamCandidates.length)
            return
        var c = root.streamCandidates[root.currentStreamIndex] || ({})
        root.streamRetryCount += 1
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = "Retrying stream..."
        streamWatchdog.restart()
        root.wakeChrome()
        Stream.play(c.infoHash, c.fileIdx || 0)
    }

    function pickAnotherStream() {
        if (root.streamCandidates.length <= 1) {
            root.errored = true
            root.starting = false
            root.statusMsg = "No alternate stream available."
            root.wakeChrome()
            return
        }
        var next = root.nextPlayableStreamIndex(root.currentStreamIndex)
        if (next < 0) {
            root.errored = true
            root.starting = false
            root.statusMsg = "No alternate stream available."
            root.wakeChrome()
            return
        }
        root.playStreamAt(next, "switch")
    }

    function handlePlaybackFailure(reason) {
        streamWatchdog.stop()
        root.recordProgress()
        if (root.streamRetryCount < 1) {
            root.retryCurrentStream()
            return
        }
        if (root.streamCandidates.length > 1) {
            root.pickAnotherStream()
            return
        }
        root.errored = true
        root.starting = false
        root.statusMsg = "Playback failed."
        root.wakeChrome()
    }

    function handleStreamWatchdog() {
        if (!root.starting || root.fileReady)
            return
        root.statusMsg = "This source did not start. Trying another stream..."
        root.handlePlaybackFailure("source did not start")
    }

    function hasAdjacentEpisode(which) {
        var ep = root.adjacentEpisodes ? root.adjacentEpisodes[which] : null
        return !!(ep && ep.id && String(ep.id).length)
    }

    function goToAdjacentEpisode(which) {
        var ep = root.adjacentEpisodes ? root.adjacentEpisodes[which] : null
        if (!ep || !ep.id)
            return
        root.cancelUpNext()
        root.adjacentResolveGen += 1
        var myGen = root.adjacentResolveGen
        root.recordProgress()
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = which === "next" ? "Next episode..." : "Previous episode..."
        streamWatchdog.restart()
        root.wakeChrome()
        Torrentio.loadStreams(ep.type || "series", ep.id, function(list) {
            if (myGen !== root.adjacentResolveGen)
                return
            if (!list || !list.length) {
                streamWatchdog.stop()
                root.errored = true
                root.starting = false
                root.statusMsg = "No stream found for " + (which === "next" ? "next episode." : "previous episode.")
                root.wakeChrome()
                return
            }
            var first = list[0]
            root.playTorrent(first.infoHash, first.fileIdx || 0,
                             ep.title || root.mediaTitle, ep.backdrop || root.mediaArt,
                             ep.type || "series", ep.id, list, ep.context || ({}))
        })
    }

    // Harbor's "Up Next": when an episode ends and a next one exists, show a visible
    // countdown card the user can cancel, instead of silently jumping.
    function startUpNextCountdown() {
        if (!root.hasAdjacentEpisode("next"))
            return false
        root.upNextRemainingSec = Math.max(1, root.upNextCountdownSec)
        root.upNextVisible = true
        upNextTimer.restart()
        root.wakeChrome()
        return true
    }

    function cancelUpNext() {
        upNextTimer.stop()
        root.upNextVisible = false
        root.upNextRemainingSec = 0
    }

    function confirmUpNext() {
        root.cancelUpNext()
        root.goToAdjacentEpisode("next")
    }

    function upNextEpisode() {
        return (root.adjacentEpisodes && root.adjacentEpisodes.next) ? root.adjacentEpisodes.next : null
    }

    function upNextTitle() {
        var e = root.upNextEpisode()
        return e ? (e.title || "Next episode") : ""
    }

    function upNextArt() {
        var e = root.upNextEpisode()
        return e ? (e.backdrop || root.mediaArt || "") : ""
    }

    function createRoom() {
        if (typeof Room === "undefined")
            return
        Room.createLocalRoom("Me")
        root.roomReady = false
        root.roomPanelOpen = true
        root.publishRoomState()
        root.wakeChrome()
    }

    function leaveRoom() {
        if (typeof Room !== "undefined")
            Room.leaveRoom()
        root.roomReady = false
        root.roomPanelOpen = false
        root.wakeChrome()
    }

    function publishRoomState() {
        if (typeof Room === "undefined" || !Room.active || !Room.isHost || root.applyingRoomSync)
            return
        Room.publishState({
            "mediaId": root.mediaId,
            "title": root.mediaTitle || mpv.mediaTitle,
            "subtitle": root.mediaSubtitle,
            "position": mpv.position,
            "duration": mpv.duration,
            "playing": !mpv.pause && !root.starting && !root.errored,
            "speed": mpv.speed,
            "streamHash": root.mediaResumeHash,
            "fileIdx": root.mediaResumeFileIdx,
            "subType": root.subStreamType,
            "subId": root.subStreamId
        })
    }

    function applyRoomSyncState(state) {
        if (typeof Room === "undefined" || !Room.active || Room.isHost || !state)
            return
        root.applyingRoomSync = true
        var targetPosition = Number(state.position || 0)
        if (mpv.duration > 0 && isFinite(targetPosition)
                && Math.abs(mpv.position - targetPosition) > root.roomDriftToleranceSeconds) {
            mpv.seekExact(root.clamp(targetPosition, 0, Math.max(0, mpv.duration)))
        }
        if (state.speed !== undefined)
            mpv.speed = Number(state.speed) || 1
        mpv.pause = !(state.playing === true)
        root.applyingRoomSync = false
        root.wakeChrome()
    }

    function sendRoomChat() {
        if (typeof Room === "undefined")
            return
        Room.sendChat(root.roomChatDraft)
        root.roomChatDraft = ""
        root.wakeChrome()
    }

    function markRoomReady(ready) {
        root.roomReady = ready
        if (typeof Room !== "undefined")
            Room.markReady(ready)
        root.wakeChrome()
    }

    function openCastPanel() {
        if (typeof Cast !== "undefined")
            Cast.discoverDevices()
        root.castPanelOpen = true
        root.wakeChrome()
    }

    function currentCastUrl() {
        if (root.currentPlaybackUrl.length)
            return root.currentPlaybackUrl
        if (typeof Stream !== "undefined" && root.mediaResumeHash.length)
            return Stream.streamUrl(root.mediaResumeHash, root.mediaResumeFileIdx)
        return ""
    }

    function startCast(device) {
        if (typeof Cast === "undefined")
            return
        var url = root.currentCastUrl()
        Cast.startCasting(device || ({}), {
            "url": url,
            "title": root.mediaTitle || mpv.mediaTitle,
            "poster": root.mediaArt,
            "position": mpv.position,
            "duration": mpv.duration,
            "playing": !mpv.pause && !root.starting && !root.errored
        })
        if (Cast.active || Cast.pending)
            mpv.pause = true
        root.castPanelOpen = false
        root.wakeChrome()
    }

    function stopCast() {
        if (typeof Cast !== "undefined")
            Cast.stopCasting()
        root.wakeChrome()
    }

    function toggleCastPlay() {
        if (typeof Cast === "undefined" || !Cast.active)
            return
        if (Cast.playing)
            Cast.pause()
        else
            Cast.play()
        root.wakeChrome()
    }

    function seekCast(delta) {
        if (typeof Cast === "undefined" || !Cast.active)
            return
        Cast.seek(Cast.position + delta)
        root.wakeChrome()
    }

    function configureLiveChannel(channel) {
        if (typeof Live === "undefined")
            return
        var ch = channel || ({})
        if (!ch.url && root.currentPlaybackUrl.length)
            ch.url = root.currentPlaybackUrl
        if (!ch.name)
            ch.name = root.mediaTitle || "Live channel"
        Live.setLiveChannel(ch)
    }

    function openLiveGuide() {
        if (typeof Live !== "undefined" && !Live.isLive)
            root.configureLiveChannel({})
        root.liveGuideOpen = true
        root.wakeChrome()
    }

    function switchLiveChannel(channel) {
        if (typeof Live === "undefined")
            return
        Live.switchChannel(channel || ({}))
    }

    function startDvrRecording() {
        if (typeof Live === "undefined" || !Live.isLive)
            return
        root.currentDvrId = Live.startRecording({
            "url": root.currentCastUrl(),
            "channelName": Live.activeChannel.name || root.mediaTitle || "Live channel",
            "programTitle": Live.activeChannel.program || "",
            "durationSec": 1800,
            "outputPath": ""
        })
        root.wakeChrome()
    }

    function stopDvrRecording() {
        if (typeof Live === "undefined" || !root.currentDvrId.length)
            return
        Live.stopRecording(root.currentDvrId)
        root.currentDvrId = ""
        root.wakeChrome()
    }

    function goLiveEdge() {
        if (mpv.duration > 0)
            mpv.seekExact(Math.max(0, mpv.duration - 1))
        root.wakeChrome()
    }

    function handleWindowMinimize() {
        // Don't fight PiP / casting / room-sync — those are meant to keep playing
        // in the background even when the main window is minimized.
        if (root.pipMode)
            return
        if (typeof Cast !== "undefined" && Cast.active)
            return
        if (typeof Room !== "undefined" && Room.active)
            return
        if (root.windowMinimized) {
            if (!mpv.pause && root.fileReady && !root.starting && !root.errored) {
                root.autoPausedInactive = true
                mpv.pause = true
            }
        } else if (root.autoPausedInactive) {
            root.autoPausedInactive = false
            if (mpv.pause)
                mpv.pause = false
        }
    }

    function downloadTooltip() {
        if (typeof Download === "undefined")
            return "Download"
        var s = Download.status || ({ "kind": "idle" })
        if (s.kind === "preparing")
            return "Preparing download"
        if (s.kind === "downloading") {
            var pct = Math.round((s.ratio || 0) * 100)
            return "Downloading " + pct + "% - click to cancel"
        }
        if (s.kind === "done")
            return "Saved to " + (s.path || Download.defaultDownloadDir || "Downloads")
        if (s.kind === "error")
            return "Failed: " + (s.message || "Download failed")
        return "Download video"
    }

    function downloadIcon() {
        if (typeof Download === "undefined")
            return "download"
        var s = Download.status || ({ "kind": "idle" })
        if (s.kind === "done")
            return "check"
        if (s.kind === "error")
            return "warning"
        if (s.kind === "downloading")
            return "cancel"
        return "download"
    }

    function startVideoDownload() {
        if (typeof Download === "undefined")
            return
        var url = root.currentCastUrl()
        if (!url.length)
            return
        var meta = parseSubtitleMeta()
        var fullTitle = root.mediaTitle || mpv.mediaTitle || "Video"
        Download.startDownload({
            "url": url,
            "title": fullTitle,
            "subtitle": root.mediaSubtitle || "",
            "id": root.subStreamId || "",
            "season": meta.season !== undefined ? meta.season : 0,
            "episode": meta.episode !== undefined ? meta.episode : 0,
            "kind": meta.type === "series" ? "episode" : "movie",
            "seriesTitle": fullTitle.replace(/\s*[-—]\s*S\d+E\d+.*$/, ""),
            "art": root.mediaArt || ""
        })
        root.wakeChrome()
    }

    function handleDownloadAction() {
        if (typeof Download === "undefined")
            return
        var s = Download.status || ({ "kind": "idle" })
        if (s.kind === "downloading" || s.kind === "preparing") {
            Download.cancelDownload()
        } else if (s.kind === "done") {
            Download.revealDownload()
            Download.resetDownload()
        } else if (s.kind === "error") {
            Download.resetDownload()
        } else {
            root.startVideoDownload()
        }
        root.wakeChrome()
    }

    function captureFrameGrab() {
        try {
            var path = mpv.captureFrame(root.mediaTitle || mpv.mediaTitle || "Video",
                                        root.mediaSubtitle || root.fmtTime(mpv.position))
            if (!path || !String(path).length) {
                root.frameGrabToastText = "Frame grab failed"
                root.frameGrabPath = ""
                root.frameGrabFailed = true
            } else {
                root.frameGrabToastText = "Screenshot saved"
                root.frameGrabPath = path
                root.frameGrabFailed = false
            }
        } catch (e) {
            root.frameGrabToastText = "Frame grab failed"
            root.frameGrabPath = ""
            root.frameGrabFailed = true
        }
        root.frameGrabToastOpen = true
        frameGrabToastTimer.restart()
        root.wakeChrome()
    }
    function showGifToast(ok, path) {
        root.frameGrabToastText = ok ? "GIF saved" : "GIF export failed"
        root.frameGrabPath = ok ? path : ""
        root.frameGrabFailed = !ok
        root.frameGrabToastOpen = true
        frameGrabToastTimer.restart()
        root.wakeChrome()
    }
    function startGifRecording() {
        if (root.gifState !== "idle")
            return
        if (!mpv.startGifRecording()) {
            root.showGifToast(false, "")
            return
        }
        root.gifState = "recording"
        root.gifElapsedSec = 0
        gifElapsedTimer.restart()
        root.wakeChrome()
    }
    function stopGifRecording() {
        if (root.gifState !== "recording")
            return
        gifElapsedTimer.stop()
        root.gifState = "encoding"
        root.wakeChrome()
        var path = mpv.stopGifRecording(root.mediaTitle || mpv.mediaTitle || "Video",
                                        root.mediaSubtitle || root.fmtTime(mpv.position))
        root.gifState = "idle"
        root.gifElapsedSec = 0
        root.showGifToast(!!path && String(path).length > 0, path || "")
    }
    function abortGifRecording() {
        if (root.gifState === "idle")
            return
        gifElapsedTimer.stop()
        mpv.abortGifRecording()
        root.gifState = "idle"
        root.gifElapsedSec = 0
        root.wakeChrome()
    }

    // Write the current watch position to the Continue store. Called on a ticking timer
    // while playing and once more on stop, so the resume bar reflects where you really are.
    function recordProgress() {
        if (root.mediaId === "" || mpv.duration <= 0 || mpv.position <= 0)
            return
        // Anti-clutter floor (matches Tankoban 2's MIN_POSITION_SEC = 10): an accidental
        // few-second open should never leave a Continue card behind.
        if (mpv.position < 10)
            return
        var frac = root.clamp(mpv.position / mpv.duration, 0, 1)
        var remain = Math.max(0, mpv.duration - mpv.position)
        // for a series, lead the Continue sub-line with the season/episode (from the stream id);
        // a movie just shows the time left.
        var m = root.parseSubtitleMeta()
        var epPrefix = (m.type === "series" && m.season !== undefined && m.episode !== undefined)
                       ? ("S" + m.season + " · E" + m.episode + " · ") : ""
        Progress.record({
            "id": root.mediaId,
            "kind": "video",
            "caption": root.mediaTitle,
            "title": root.mediaTitle,
            "sub": epPrefix + root.fmtTime(remain) + " left",
            "cover": root.mediaArt,
            "c1": "#33445d", "c2": "#0c1118",
            "progress": frac,
            "resume": { "infoHash": root.mediaResumeHash,
                        "fileIdx": root.mediaResumeFileIdx,
                        "subType": root.subStreamType,
                        "subId": root.subStreamId,
                        "position": mpv.position }
        })
    }

    // Tick the watch position into the store every few seconds while actually playing.
    Timer {
        interval: 5000; repeat: true
        running: !root.starting && !root.errored && !mpv.pause && mpv.duration > 0
        onTriggered: root.recordProgress()
    }

    Timer {
        id: frameGrabToastTimer
        interval: 5200
        repeat: false
        onTriggered: root.frameGrabToastOpen = false
    }

    Timer {
        id: subtitleDropToastTimer
        interval: 2200
        repeat: false
        onTriggered: root.subtitleDropToastOpen = false
    }

    Timer {
        id: gifElapsedTimer
        interval: 1000
        repeat: true
        running: root.gifState === "recording"
        onTriggered: {
            root.gifElapsedSec += 1
            if (root.gifElapsedSec >= root.gifMaxSeconds)
                root.stopGifRecording()
        }
    }

    Timer {
        id: abLoopTimer
        interval: 120
        repeat: true
        running: root.abLoopActive && !root.starting && !root.errored
        onTriggered: {
            if (root.abLoopSeeking || mpv.pause)
                return
            if (mpv.position >= root.abLoopB) {
                root.abLoopSeeking = true
                root.seekTo(root.abLoopA)
                abLoopReleaseTimer.restart()
            }
        }
    }

    Timer {
        id: abLoopReleaseTimer
        interval: 250
        repeat: false
        onTriggered: root.abLoopSeeking = false
    }

    Timer {
        id: sleepTimerTick
        interval: 1000
        repeat: true
        running: root.sleepTimerMode === "minutes"
        onTriggered: root.updateSleepTimer()
    }

    Timer {
        id: playbackStatsTimer
        interval: 1000
        repeat: true
        running: root.statsOverlayOpen
        onTriggered: root.refreshPlaybackStats()
    }

    Timer {
        id: drawGcTimer
        interval: 2000
        repeat: true
        running: root.drawStrokes.length > 0
        onTriggered: root.pruneDrawStrokes()
    }

    Timer {
        id: roomPublishTimer
        interval: 2000
        repeat: true
        running: typeof Room !== "undefined" && Room.active && Room.isHost
        onTriggered: root.publishRoomState()
    }

    function playUrl(url, title) {
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.mediaTitle = title || ""
        root.mediaSubtitle = "Direct file"
        root.currentPlaybackUrl = url || ""
        root.errored = false
        root.starting = true
        root.statusMsg = "Opening..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        mpv.loadFile(url)
    }

    function stop() {
        root.recordProgress()   // capture where we left off BEFORE mpv clears position
        root.closeMenus()
        mpv.command(["stop"])
        root.starting = false
        root.errored = false
        root.statusMsg = ""
    }

    function fmtTime(sec) {
        if (!isFinite(sec) || sec < 0)
            return "0:00"
        var total = Math.floor(sec)
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        var ss = s < 10 ? "0" + s : "" + s
        if (h > 0) {
            var mm = m < 10 ? "0" + m : "" + m
            return h + ":" + mm + ":" + ss
        }
        return m + ":" + ss
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function round2(v) { return Math.round(v * 100) / 100 }
    function seekFraction() {
        var pos = root.seeking ? root.seekPreview : mpv.position
        return mpv.duration > 0 ? root.clamp(pos / mpv.duration, 0, 1) : 0
    }
    function previewAt(mouseX, width) {
        return mpv.duration * root.clamp(mouseX / Math.max(1, width), 0, 1)
    }
    function seekTo(sec) {
        mpv.seekExact(root.clamp(sec, 0, Math.max(0, mpv.duration)))
        root.wakeChrome()
    }
    function seekStep(delta) {
        mpv.seekStep(delta)
        root.wakeChrome()
    }
    function setAbLoopA() {
        root.abLoopA = Math.max(0, mpv.position)
        if (root.abLoopB <= root.abLoopA)
            root.abLoopB = -1
        root.wakeChrome()
    }
    function setAbLoopB() {
        if (root.abLoopA < 0)
            return
        var next = Math.max(0, mpv.position)
        if (next <= root.abLoopA)
            return
        root.abLoopB = next
        root.wakeChrome()
    }
    function clearAbLoop() {
        root.abLoopA = -1
        root.abLoopB = -1
        root.abLoopSeeking = false
        if (typeof abLoopReleaseTimer !== "undefined")
            abLoopReleaseTimer.stop()
        root.wakeChrome()
    }
    function setSleepTimer(preset) {
        var p = preset || ({})
        if (p.mode === "minutes") {
            root.sleepTimerMode = "minutes"
            root.sleepTimerFiresAt = Date.now() + Math.max(1, Number(p.minutes || 0)) * 60000
            root.sleepEndEpisodesRemaining = 0
            root.updateSleepTimer()
        } else if (p.mode === "end_episode") {
            root.sleepTimerMode = "end_episode"
            root.sleepTimerFiresAt = 0
            root.sleepTimerRemainingMs = 0
            root.sleepEndEpisodesRemaining = 1
        } else if (p.mode === "end_next_episode") {
            root.sleepTimerMode = "end_next_episode"
            root.sleepTimerFiresAt = 0
            root.sleepTimerRemainingMs = 0
            root.sleepEndEpisodesRemaining = 2
        } else {
            root.cancelSleepTimer()
        }
        root.wakeChrome()
    }
    function cancelSleepTimer() {
        root.sleepTimerMode = "off"
        root.sleepTimerFiresAt = 0
        root.sleepTimerRemainingMs = 0
        root.sleepEndEpisodesRemaining = 0
        root.wakeChrome()
    }
    function updateSleepTimer() {
        if (root.sleepTimerMode !== "minutes")
            return
        root.sleepTimerRemainingMs = Math.max(0, root.sleepTimerFiresAt - Date.now())
        if (root.sleepTimerRemainingMs <= 0) {
            mpv.pause = true
            root.cancelSleepTimer()
        }
    }
    function sleepTimerLabel() {
        if (root.sleepTimerMode === "minutes") {
            var total = Math.max(0, Math.round(root.sleepTimerRemainingMs / 1000))
            var minutes = Math.floor(total / 60)
            var seconds = total % 60
            return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
        }
        if (root.sleepTimerMode === "end_episode")
            return "End ep"
        if (root.sleepTimerMode === "end_next_episode")
            return "+" + Math.max(1, root.sleepEndEpisodesRemaining) + " ep"
        return ""
    }
    function sleepPresetSelected(preset) {
        var p = preset || ({})
        if (root.sleepTimerMode !== p.mode)
            return false
        if (p.mode === "minutes")
            return Math.abs((root.sleepTimerFiresAt - Date.now()) / 60000 - Number(p.minutes || 0)) <= 1
        return true
    }
    function handleSleepEpisodeEnd() {
        if (root.sleepTimerMode === "end_episode") {
            mpv.pause = true
            root.cancelSleepTimer()
            return true
        }
        if (root.sleepTimerMode === "end_next_episode") {
            root.sleepEndEpisodesRemaining -= 1
            if (root.sleepEndEpisodesRemaining <= 0) {
                mpv.pause = true
                root.cancelSleepTimer()
                return true
            }
        }
        return false
    }
    function refreshPlaybackStats() {
        root.playbackStats = {
            "videoBitrate": mpv.mpvProperty("video-bitrate"),
            "audioBitrate": mpv.mpvProperty("audio-bitrate"),
            "frameDropDecoder": mpv.mpvProperty("frame-drop-count"),
            "frameDropOutput": mpv.mpvProperty("vo-drop-frame-count"),
            "estimatedFps": mpv.mpvProperty("estimated-vf-fps"),
            "containerFps": mpv.mpvProperty("container-fps"),
            "videoCodec": mpv.mpvProperty("video-codec"),
            "audioCodec": mpv.mpvProperty("audio-codec"),
            "hwdec": mpv.mpvProperty("hwdec-current"),
            "cacheBufferingState": mpv.mpvProperty("cache-buffering-state"),
            "width": mpv.mpvProperty("width"),
            "height": mpv.mpvProperty("height")
        }
    }
    function formatBitrate(value) {
        var n = Number(value || 0)
        if (!isFinite(n) || n <= 0)
            return "--"
        if (n >= 1000000)
            return (n / 1000000).toFixed(2) + " Mbps"
        if (n >= 1000)
            return Math.round(n / 1000) + " kbps"
        return Math.round(n) + " bps"
    }
    function firstSelectedTrack(rows) {
        var list = rows || []
        for (var i = 0; i < list.length; i++) {
            if (list[i].selected)
                return list[i]
        }
        return null
    }
    function statsValue(label) {
        var s = root.playbackStats || ({})
        if (label === "Resolution") {
            var w = Number(s.width || 0)
            var h = Number(s.height || 0)
            return w > 0 && h > 0 ? (w + "x" + h) : "--"
        }
        if (label === "Frame rate") {
            var fps = Number(s.estimatedFps || s.containerFps || 0)
            return fps > 0 ? fps.toFixed(2) + " fps" : "--"
        }
        if (label === "Video codec")
            return s.videoCodec || "--"
        if (label === "Audio codec")
            return s.audioCodec || "--"
        if (label === "HW decode")
            return s.hwdec || "--"
        if (label === "Video bitrate")
            return root.formatBitrate(s.videoBitrate)
        if (label === "Audio bitrate")
            return root.formatBitrate(s.audioBitrate)
        if (label === "Dropped frames")
            return String(Number(s.frameDropDecoder || 0)) + " / " + String(Number(s.frameDropOutput || 0))
        if (label === "Cache buffering")
            return s.cacheBufferingState !== undefined && s.cacheBufferingState !== "" ? Number(s.cacheBufferingState).toFixed(0) + "%" : "--"
        if (label === "Audio track") {
            var audio = root.firstSelectedTrack(root.audioRows)
            return audio ? (audio.title || audio.label || audio.lang || audio.id) : "--"
        }
        if (label === "Subtitle track") {
            var sub = root.firstSelectedTrack(root.subRows)
            return sub ? (sub.title || sub.label || sub.lang || sub.id) : "Off"
        }
        if (label === "Speed")
            return mpv.speed.toFixed(2) + "x"
        if (label === "Volume")
            return Math.round(mpv.volume) + "%" + (mpv.mute ? " / muted" : "")
        return "--"
    }
    function normalizedDrawPoint(mouseX, mouseY) {
        return {
            "x": root.clamp(mouseX / Math.max(1, drawInputArea.width), 0, 1),
            "y": root.clamp(mouseY / Math.max(1, drawInputArea.height), 0, 1)
        }
    }
    function startDrawStroke(mouseX, mouseY) {
        var p = root.normalizedDrawPoint(mouseX, mouseY)
        root.activeDrawStrokeId = "local-" + Date.now()
        var rows = root.drawStrokes.slice()
        rows.push({
            "id": root.activeDrawStrokeId,
            "color": root.drawColor,
            "bornAt": Date.now(),
            "points": [p]
        })
        root.drawStrokes = rows
        drawCanvas.requestPaint()
    }
    function addDrawPoint(mouseX, mouseY) {
        if (!root.activeDrawStrokeId.length)
            return
        var p = root.normalizedDrawPoint(mouseX, mouseY)
        var rows = root.drawStrokes.slice()
        for (var i = rows.length - 1; i >= 0; i--) {
            if (rows[i].id === root.activeDrawStrokeId) {
                var pts = rows[i].points ? rows[i].points.slice() : []
                if (pts.length) {
                    var last = pts[pts.length - 1]
                    var dx = p.x - last.x
                    var dy = p.y - last.y
                    if (dx * dx + dy * dy < 0.004 * 0.004)
                        return
                }
                pts.push(p)
                rows[i] = Object.assign({}, rows[i], { "points": pts })
                root.drawStrokes = rows
                drawCanvas.requestPaint()
                return
            }
        }
    }
    function endDrawStroke(mouseX, mouseY) {
        if (root.activeDrawStrokeId.length)
            root.addDrawPoint(mouseX, mouseY)
        root.activeDrawStrokeId = ""
        root.pruneDrawStrokes()
    }
    function pruneDrawStrokes() {
        var now = Date.now()
        var rows = []
        for (var i = 0; i < root.drawStrokes.length; i++) {
            var s = root.drawStrokes[i]
            if (now - Number(s.bornAt || 0) < root.drawStrokeLifetimeMs)
                rows.push(s)
        }
        if (rows.length !== root.drawStrokes.length) {
            root.drawStrokes = rows
            drawCanvas.requestPaint()
        }
    }
    function toggleDrawMode() {
        root.drawMode = !root.drawMode
        if (!root.drawMode)
            root.activeDrawStrokeId = ""
        root.closeMenus()
        root.wakeChrome()
    }
    function togglePlayPause() {
        if (!root.starting && !root.errored)
            mpv.pause = !mpv.pause
        root.wakeChrome()
    }
    function setVolumeFromFraction(f) {
        var normalFraction = 0.62
        var next = f <= normalFraction
            ? (f / normalFraction) * 100
            : 100 + ((f - normalFraction) / (1 - normalFraction)) * 500
        mpv.volume = Math.round(root.clamp(next, 0, 600))
        if (mpv.volume > 0)
            mpv.mute = false
        root.wakeChrome()
    }
    function volumeFraction() {
        var v = root.clamp(mpv.volume, 0, 600)
        var normalFraction = 0.62
        if (v <= 100)
            return (v / 100) * normalFraction
        return normalFraction + ((v - 100) / 500) * (1 - normalFraction)
    }
    function closeMenus() {
        audioMenu.panelOpen = false
        subMenu.panelOpen = false
        subStyleBar.open = false
        speedMenu.panelOpen = false
        fillMenu.panelOpen = false
        toolsMenu.panelOpen = false
        root.roomPanelOpen = false
        root.castPanelOpen = false
        root.liveGuideOpen = false
        root.dvrPanelOpen = false
    }
    function wakeChrome() {
        root.controlsShown = true
        hideTimer.restart()
    }
    function applyFill(index) {
        root.fillModeIndex = root.clamp(index, 0, root.fillModes.length - 1)
        var mode = root.fillModes[root.fillModeIndex]
        mpv.panscan = mode.panscan
        mpv.videoZoom = mode.zoom
        mpv.videoAspect = mode.aspect
        fillMenu.panelOpen = false
        root.wakeChrome()
    }
    function cycleSubtitle() {
        var tracks = mpv.subtitleTracks
        if (!tracks || tracks.length === 0) {
            mpv.subtitleTrack = ""
            return
        }
        var idx = -1
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].selected === true || tracks[i].id === mpv.subtitleTrack) {
                idx = i
                break
            }
        }
        if (idx < 0)
            mpv.subtitleTrack = tracks[0].id
        else if (idx + 1 >= tracks.length)
            mpv.subtitleTrack = ""
        else
            mpv.subtitleTrack = tracks[idx + 1].id
    }
    function toggleWindowFullscreen() {
        var w = root.Window.window
        if (!w)
            return
        w.visibility = (w.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
        root.wakeChrome()
    }
    function trackTitle(track, fallback) {
        if (track.title && ("" + track.title).trim() !== "")
            return track.title
        if (track.lang && ("" + track.lang).trim() !== "")
            return ("" + track.lang).toUpperCase()
        return fallback
    }
    function trackMeta(track) {
        var parts = []
        if (track.lang && ("" + track.lang).trim() !== "")
            parts.push(("" + track.lang).toUpperCase())
        parts.push(track.external ? "External" : "Embedded")
        if (track.codec && ("" + track.codec).trim() !== "")
            parts.push(("" + track.codec).toUpperCase())
        if (track.forced)
            parts.push("Forced")
        if (track.default)
            parts.push("Default")
        return parts.join(" / ")
    }

    function syncPowerInhibit() {
        if (typeof Power === "undefined")
            return
        var shouldInhibit = root.visible && root.fileReady && !root.starting && !root.errored && !mpv.pause
        Power.setInhibited(shouldInhibit, root.mediaTitle || "Colosseum playback")
    }

    Component.onCompleted: {
        root.forceActiveFocus()
        root.wakeChrome()
        root.syncPowerInhibit()
    }
    Component.onDestruction: if (typeof Power !== "undefined") Power.release()
    onVisibleChanged: {
        if (visible)
            root.forceActiveFocus()
        root.syncPowerInhibit()
    }
    onStartingChanged: {
        if (starting)
            root.wakeChrome()
        root.syncPowerInhibit()
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; z: -1; color: "#000000" }

    MpvItem {
        id: mpv
        anchors.fill: parent
        z: 0
        onFileStarted: {
            root.starting = true
            root.statusMsg = "Buffering..."
            root.wakeChrome()
            root.syncPowerInhibit()
        }
        onFileLoaded: {
            root.starting = false
            root.errored = false
            root.statusMsg = ""
            streamWatchdog.stop()
            root.seekPreview = mpv.position
            root.fileReady = true
            root.maybeAutoSub()      // file is open → safe to sub-add the auto/online subtitle
            root.wakeChrome()
            root.syncPowerInhibit()
            root.detectStubStream()
        }
        onEndFile: function(reason) {
            root.starting = false
            root.fileReady = false
            root.syncPowerInhibit()
            if (reason === "error" || reason === "other") {
                root.handlePlaybackFailure(reason)
            } else if (reason === "eof") {
                root.recordProgress()
                if (root.handleSleepEpisodeEnd())
                    return
                root.startUpNextCountdown()
            }
        }
        onPauseChanged: {
            if (mpv.pause)
                root.wakeChrome()
            root.syncPowerInhibit()
            root.detectStubStream()
        }
        onDurationChanged: root.detectStubStream()
    }

    SubStyleBar {
        id: subStyleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        player: mpv
    }

    Connections {
        target: Stream
        function onStreamReady(url, infoHash, fileIdx) {
            root.statusMsg = "Buffering..."
            root.currentPlaybackUrl = url || ""
            streamWatchdog.restart()
            mpv.loadFile(url)
        }
        function onStreamError(message) {
            root.statusMsg = message
            root.handlePlaybackFailure("stream")
        }
    }

    Connections {
        target: Room
        function onSyncCommand(state) {
            root.applyRoomSyncState(state)
        }
    }

    Connections {
        target: Live
        function onChannelSwitchRequested(channel) {
            root.liveGuideOpen = false
            root.configureLiveChannel(channel)
            root.playUrl(channel.url || "", channel.name || "Live channel")
            root.mediaSubtitle = channel.group || "Live channel"
        }
    }

    Connections {
        target: WindowMode
        function onPipEntered() {
            root.pipMode = true
            root.closeMenus()
            root.controlsShown = false
        }
        function onPipExited() {
            root.pipMode = false
            root.wakeChrome()
        }
    }

    Timer {
        id: streamWatchdog
        interval: root.streamWatchdogSeconds * 1000
        repeat: false
        onTriggered: root.handleStreamWatchdog()
    }

    Timer {
        id: hideTimer
        interval: mpv.pause || root.starting ? 4500 : 1800
        repeat: false
        onTriggered: if (!mpv.pause && !root.starting && !root.seeking && !root.anyMenuOpen) root.controlsShown = false
    }

    Timer {
        id: spaceHoldTimer
        interval: 350
        repeat: false
        onTriggered: {
            root.spaceHoldFired = true
            mpv.speed = Math.max(2, root.spaceBaseSpeed)
        }
    }

    Timer {
        id: upNextTimer
        interval: 1000
        repeat: true
        onTriggered: {
            root.upNextRemainingSec -= 1
            if (root.upNextRemainingSec <= 0)
                root.confirmUpNext()
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPositionChanged: root.wakeChrome()
        onClicked: {
            // A click that only dismisses an open menu must NOT also toggle play/pause.
            if (root.anyMenuOpen) {
                root.closeMenus()
                return
            }
            root.togglePlayPause()
        }
        onDoubleClicked: if (!root.anyMenuOpen) root.toggleWindowFullscreen()
    }

    DropArea {
        anchors.fill: parent
        z: 7
        onEntered: function(drag) {
            root.wakeChrome()
        }
        onDropped: function(drop) {
            var urls = drop.urls || []
            var picked = ""
            for (var i = 0; i < urls.length; i++) {
                if (root.isSubtitleFile(urls[i])) {
                    picked = urls[i]
                    break
                }
            }
            if (!picked.length && urls.length > 0)
                picked = urls[0]
            if (picked.length)
                root.loadDroppedSubtitle(picked)
            else
                root.showSubtitleDropToast(false, "Subtitle")
            drop.acceptProposedAction()
        }
    }

    Keys.onPressed: function(event) {
        root.wakeChrome()
        if (event.key === Qt.Key_Space) {
            event.accepted = true
            if (event.isAutoRepeat)
                return
            root.spaceBaseSpeed = mpv.speed
            root.spaceHoldFired = false
            spaceHoldTimer.restart()
            return
        }
        if (event.key === Qt.Key_Escape) {
            event.accepted = true
            if (root.anyMenuOpen)
                root.closeMenus()
            else
                root.backRequested()
            return
        }
        if (event.key === Qt.Key_Left) { event.accepted = true; root.seekStep(-root.seekBackSeconds); return }
        if (event.key === Qt.Key_Right) { event.accepted = true; root.seekStep(root.seekForwardSeconds); return }
        if (event.key === Qt.Key_Comma) { event.accepted = true; root.seekStep(-30); return }
        if (event.key === Qt.Key_Period) { event.accepted = true; root.seekStep(30); return }
        if (event.key === Qt.Key_Home) { event.accepted = true; root.seekTo(0); return }
        if (event.key === Qt.Key_End && mpv.duration > 0) { event.accepted = true; root.seekTo(mpv.duration - 0.5); return }
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            event.accepted = true
            var digit = event.key - Qt.Key_0
            root.seekTo(digit === 0 ? 0 : mpv.duration * digit / 10)
            return
        }
        if (event.key === Qt.Key_F) { event.accepted = true; root.toggleWindowFullscreen(); return }
        if (event.key === Qt.Key_M) { event.accepted = true; mpv.mute = !mpv.mute; return }
        if (event.key === Qt.Key_Up) { event.accepted = true; mpv.volume = mpv.volume + (event.modifiers & Qt.ShiftModifier ? 50 : 5); return }
        if (event.key === Qt.Key_Down) { event.accepted = true; mpv.volume = mpv.volume - (event.modifiers & Qt.ShiftModifier ? 50 : 5); return }
        if (event.key === Qt.Key_BracketLeft) { event.accepted = true; mpv.speed = root.clamp(root.round2(mpv.speed - 0.25), 0.25, 3); return }
        if (event.key === Qt.Key_BracketRight) { event.accepted = true; mpv.speed = root.clamp(root.round2(mpv.speed + 0.25), 0.25, 3); return }
        if (event.key === Qt.Key_Z) { event.accepted = true; mpv.subDelay = root.round2(mpv.subDelay - (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return }
        if (event.key === Qt.Key_X) { event.accepted = true; mpv.subDelay = root.round2(mpv.subDelay + (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return }
        if (event.key === Qt.Key_S || event.key === Qt.Key_C) { event.accepted = true; root.cycleSubtitle(); return }
        if (event.key === Qt.Key_I) { event.accepted = true; root.setAbLoopA(); return }
        if (event.key === Qt.Key_O) { event.accepted = true; root.setAbLoopB(); return }
        if (event.key === Qt.Key_L) { event.accepted = true; root.clearAbLoop(); return }
        if (event.key === Qt.Key_D) {
            event.accepted = true
            root.statsOverlayOpen = !root.statsOverlayOpen
            if (root.statsOverlayOpen)
                root.refreshPlaybackStats()
            return
        }
    }

    Keys.onReleased: function(event) {
        if (event.key !== Qt.Key_Space)
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        if (spaceHoldTimer.running)
            spaceHoldTimer.stop()
        if (root.spaceHoldFired)
            mpv.speed = root.spaceBaseSpeed
        else
            root.togglePlayPause()
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: root.starting || root.errored
        z: 4

        Item {
            width: 48
            height: 48
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.starting && !root.errored
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 3
                border.color: Qt.rgba(1, 1, 1, 0.18)
            }
            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: theme.gold
                x: parent.width / 2 - 6
                y: -1
            }
            RotationAnimation on rotation {
                running: root.starting
                loops: Animation.Infinite
                from: 0
                to: 360
                duration: 900
            }
        }
        Text {
            width: Math.min(root.width - 120, 520)
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.statusMsg
            color: root.errored ? "#e6a3a3" : theme.ink
            font.family: theme.ui
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        id: chrome
        x: 0
        y: 0
        width: root.chromeVisibleWidth
        height: root.chromeVisibleHeight
        z: 99999
        color: Qt.rgba(0, 0, 0, 0.001)
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 142
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.68) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.24) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        Rectangle {
            x: 0
            y: root.chromeVisibleHeight - height
            width: root.chromeVisibleWidth
            height: 236
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 0.38; color: Qt.rgba(0, 0, 0, 0.28) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.78) }
            }
        }

        RoundButton {
            // audit fix: this control MINIMIZES to the taskbar (session persists) — it is not a
            // page back, so it no longer wears a back chevron or the word "Back".
            id: backButton
            x: tight ? 16 : 28
            y: tight ? 14 : 20
            size: 48
            icon: "minimizeToBar"
            tooltip: "Minimize — keeps playing in the taskbar"
            onClicked: root.minimizeRequested()
        }

        Column {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: tight ? 18 : 24
            anchors.rightMargin: tight ? 18 : 34
            spacing: 3
            width: Math.min(560, parent.width - (tight ? 92 : 140))
            Text {
                width: parent.width
                text: root.mediaTitle || mpv.mediaTitle
                color: theme.ink
                font.family: theme.display
                font.pixelSize: tight ? 18 : 22
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.55)
            }
            Text {
                width: parent.width
                text: root.mediaSubtitle
                visible: text.length > 0
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.55)
            }
        }

        Rectangle {
            id: roomPanel
            visible: root.roomPanelOpen
            z: 8
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: tight ? 14 : 34
            anchors.topMargin: tight ? 78 : 92
            width: Math.min(430, parent.width - 28)
            height: Math.min(520, parent.height - 230)
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                id: roomTitle
                x: 18
                y: 15
                text: "Watch room"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: roomTitle.right
                anchors.leftMargin: 10
                anchors.verticalCenter: roomTitle.verticalCenter
                text: (typeof Room !== "undefined" && Room.active) ? Room.roomId : "Local"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Row {
                x: 16
                y: 52
                spacing: 8
                Repeater {
                    model: (typeof Room !== "undefined" && Room.active) ? Room.participants : []
                    delegate: Rectangle {
                        required property var modelData
                        width: 38
                        height: 38
                        radius: 19
                        color: modelData.ready ? Qt.rgba(0.95, 0.68, 0.18, 0.22) : Qt.rgba(1, 1, 1, 0.10)
                        border.width: modelData.host ? 1 : 0
                        border.color: theme.gold
                        Text {
                            anchors.centerIn: parent
                            text: String(modelData.name || "?").charAt(0).toUpperCase()
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Text {
                x: 18
                y: 102
                width: parent.width - 36
                text: (typeof Room !== "undefined" && Room.active)
                      ? (Room.isHost ? "Host controls this synced session." : "Following host playback.")
                      : "Create a local room to sync playback state."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Row {
                x: 16
                y: 144
                spacing: 8
                RoomActionButton {
                    label: (typeof Room !== "undefined" && Room.active) ? "Start synced playback" : "Create room"
                    onClicked: {
                        if (typeof Room !== "undefined" && Room.active)
                            root.publishRoomState()
                        else
                            root.createRoom()
                    }
                }
                RoomActionButton {
                    label: root.roomReady ? "Ready" : "Ready"
                    active: root.roomReady
                    onClicked: root.markRoomReady(!root.roomReady)
                }
                RoomActionButton {
                    visible: typeof Room !== "undefined" && Room.active
                    label: "Leave room"
                    onClicked: root.leaveRoom()
                }
            }

            Rectangle {
                x: 16
                y: 196
                width: parent.width - 32
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            Text {
                x: 18
                y: 214
                text: "Room chat"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            ListView {
                id: roomChatList
                x: 14
                y: 242
                width: parent.width - 28
                height: parent.height - 314
                clip: true
                spacing: 6
                model: (typeof Room !== "undefined" && Room.active) ? Room.chat : []
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: chatMessage.implicitHeight + 18
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.06)
                    Text {
                        id: chatName
                        x: 12
                        y: 7
                        text: modelData.name || "Guest"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: chatMessage
                        x: 12
                        y: 23
                        width: parent.width - 24
                        text: modelData.message || ""
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                x: 14
                y: parent.height - 58
                width: parent.width - 28
                height: 42
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.07)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.10)
                TextInput {
                    id: roomChatInput
                    anchors.left: parent.left
                    anchors.right: sendRoomButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    text: root.roomChatDraft
                    color: theme.ink
                    selectedTextColor: "#0b0d10"
                    selectionColor: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 13
                    clip: true
                    onTextChanged: root.roomChatDraft = text
                    Keys.onReturnPressed: root.sendRoomChat()
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Message"
                        visible: roomChatInput.text.length === 0
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }
                Rectangle {
                    id: sendRoomButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 6
                    width: 58
                    height: 30
                    radius: 8
                    color: roomSendMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.10)
                    Text {
                        anchors.centerIn: parent
                        text: "Send"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: roomSendMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.sendRoomChat()
                    }
                }
            }
        }

        Rectangle {
            id: castPanel
            visible: root.castPanelOpen
            z: 9
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: tight ? 14 : 34
            anchors.topMargin: tight ? 78 : 92
            width: Math.min(360, parent.width - 28)
            height: Math.min(420, parent.height - 230)
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.95)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                id: castTitle
                x: 18
                y: 15
                text: "Cast to TV or speaker"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                x: 18
                y: 43
                width: parent.width - 36
                text: (typeof Cast !== "undefined" && Cast.localServerUrl.length)
                      ? ("Local server: " + Cast.localServerUrl)
                      : "Scanning your network..."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Rectangle { x: 0; y: 72; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

            Text {
                visible: typeof Cast !== "undefined" && Cast.scanning
                x: 18
                y: 94
                text: "Scanning your network..."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
            }

            Text {
                visible: typeof Cast === "undefined" || (!Cast.scanning && Cast.devices.length === 0)
                x: 18
                y: 94
                width: parent.width - 36
                text: "No Chromecast, DLNA, or Roku devices found. Make sure your TV is on, woken up, and on the same Wi-Fi."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            ListView {
                x: 10
                y: 86
                width: parent.width - 20
                height: parent.height - 142
                clip: true
                spacing: 4
                model: (typeof Cast !== "undefined" && !Cast.scanning) ? Cast.devices : []
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 58
                    radius: 11
                    color: castDeviceMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    border.width: modelData.manual ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    IconGlyph {
                        x: 12
                        y: 13
                        width: 32
                        height: 32
                        kind: "cast"
                        ink: theme.gold
                    }
                    Text {
                        x: 54
                        y: 10
                        width: parent.width - 70
                        text: modelData.name || "Cast device"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 54
                        y: 31
                        width: parent.width - 70
                        text: modelData.model || modelData.kind || ""
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: castDeviceMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.startCast(modelData)
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 16
                anchors.bottomMargin: 14
                spacing: 8
                RoomActionButton {
                    label: "Rescan"
                    onClicked: root.openCastPanel()
                }
                RoomActionButton {
                    label: "Close"
                    onClicked: {
                        root.castPanelOpen = false
                        root.wakeChrome()
                    }
                }
            }
        }

        Rectangle {
            id: castSessionBar
            visible: typeof Cast !== "undefined" && (Cast.active || Cast.pending)
            z: 10
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: tight ? 18 : 24
            width: Math.min(parent.width - 32, 560)
            height: 54
            radius: 27
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.92)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            IconGlyph {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                kind: "cast"
                ink: theme.gold
            }
            Column {
                x: 56
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 258
                spacing: 1
                Text {
                    width: parent.width
                    text: "Casting to"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: (typeof Cast !== "undefined" && Cast.device.name) ? Cast.device.name : "Connecting..."
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                RoomActionButton {
                    width: 42
                    label: (typeof Cast !== "undefined" && Cast.playing) ? "II" : "Play"
                    onClicked: root.toggleCastPlay()
                }
                RoomActionButton {
                    width: 52
                    label: "-15s"
                    onClicked: root.seekCast(-15)
                }
                RoomActionButton {
                    width: 52
                    label: "+15s"
                    onClicked: root.seekCast(15)
                }
                RoomActionButton {
                    label: "Stop casting"
                    onClicked: root.stopCast()
                }
            }
        }

        Rectangle {
            id: liveGuide
            visible: root.liveGuideOpen
            z: 11
            anchors.fill: parent
            color: Qt.rgba(4 / 255, 6 / 255, 10 / 255, 0.96)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 92
                color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.96)
                border.width: 0
                RoundButton {
                    x: 22
                    anchors.verticalCenter: parent.verticalCenter
                    size: 46
                    icon: "back"
                    tooltip: "Close guide"
                    onClicked: {
                        root.liveGuideOpen = false
                        root.wakeChrome()
                    }
                }
                Column {
                    x: 86
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "Live guide"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: (typeof Live !== "undefined" && Live.activeChannel.name) ? Live.activeChannel.name : "Live channel"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(360, parent.width - 520)
                    height: 42
                    radius: 11
                    color: Qt.rgba(1, 1, 1, 0.08)
                    TextInput {
                        id: liveSearch
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        verticalAlignment: TextInput.AlignVCenter
                        text: (typeof Live !== "undefined") ? Live.query : ""
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        clip: true
                        onTextChanged: if (typeof Live !== "undefined") Live.setQuery(text)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Search channels"
                            visible: liveSearch.text.length === 0
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 14
                        }
                    }
                }
            }

            ListView {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 112
                anchors.leftMargin: 34
                anchors.rightMargin: 34
                anchors.bottomMargin: 34
                clip: true
                spacing: 8
                model: (typeof Live !== "undefined") ? Live.channels : []
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 64
                    radius: 10
                    color: liveChannelMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: (typeof Live !== "undefined" && Live.activeChannel.id === modelData.id) ? 1 : 0
                    border.color: theme.gold
                    Text {
                        x: 18
                        y: 11
                        width: parent.width - 36
                        text: modelData.name || "Live channel"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 18
                        y: 36
                        width: parent.width - 36
                        text: modelData.group || modelData.program || "Live"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: liveChannelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.switchLiveChannel(modelData)
                    }
                }
            }
        }

        Rectangle {
            id: dvrPanel
            visible: root.dvrPanelOpen
            z: 12
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 36)
            height: Math.min(420, parent.height - 72)
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.96)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                x: 24
                y: 22
                text: "DVR record"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                x: 24
                y: 52
                width: parent.width - 48
                text: (typeof Live !== "undefined" && Live.activeChannel.name) ? Live.activeChannel.name : "Live channel"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Text {
                x: 24
                y: 72
                width: parent.width - 48
                text: (typeof Live !== "undefined" && Live.defaultRecordingDir) ? Live.defaultRecordingDir : ""
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            Row {
                x: 24
                y: 102
                spacing: 8
                RoomActionButton {
                    label: root.currentDvrId.length ? "Stop recording" : "Start recording"
                    active: root.currentDvrId.length > 0
                    onClicked: {
                        if (root.currentDvrId.length)
                            root.stopDvrRecording()
                        else
                            root.startDvrRecording()
                    }
                }
                RoomActionButton {
                    label: "Close"
                    onClicked: {
                        root.dvrPanelOpen = false
                        root.wakeChrome()
                    }
                }
            }
            ListView {
                x: 18
                y: 158
                width: parent.width - 36
                height: parent.height - 180
                clip: true
                spacing: 6
                model: (typeof Live !== "undefined") ? Live.recordings : []
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 88
                    radius: 10
                    color: Qt.rgba(1, 1, 1, 0.06)
                    Text {
                        x: 14
                        y: 8
                        width: parent.width - 28
                        text: modelData.channelName || "Live channel"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 14
                        y: 30
                        width: parent.width - 118
                        text: (modelData.state || "recording") + " / " + (modelData.elapsedSec || 0) + "s / " + Math.round((modelData.bytesWritten || 0) / 1024) + " KB"
                        color: modelData.state === "recording" ? theme.gold : theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                    Text {
                        x: 14
                        y: 50
                        width: parent.width - 28
                        text: modelData.error || modelData.outputPath || ""
                        color: modelData.error ? theme.danger : theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    RoomActionButton {
                        x: parent.width - width - 10
                        y: 26
                        label: "Reveal"
                        visible: (modelData.outputPath || "").length > 0
                        enabled: (modelData.state || "recording") !== "recording"
                        onClicked: {
                            if (typeof Live !== "undefined")
                                Live.revealRecording(modelData.id)
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.abLoopA >= 0 || root.abLoopB >= 0
            z: 18
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 28
            anchors.topMargin: 92
            width: abLoopChipRow.implicitWidth + 22
            height: 38
            radius: 19
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, root.controlsShown ? 0.82 : 0.46)
            border.width: 1
            border.color: root.abLoopActive ? theme.gold : Qt.rgba(1, 1, 1, 0.16)

            Row {
                id: abLoopChipRow
                anchors.centerIn: parent
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "A-B loop"
                    color: root.abLoopActive ? theme.gold : theme.ink
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.abLoopA >= 0 ? root.fmtTime(root.abLoopA) : "--:--")
                          + " -> "
                          + (root.abLoopB >= 0 ? root.fmtTime(root.abLoopB) : "--:--")
                    color: theme.inkDim
                    font.family: "Consolas"
                    font.pixelSize: 12
                }
                RoundButton {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 26
                    icon: "cancel"
                    tooltip: "Clear A-B loop"
                    onClicked: root.clearAbLoop()
                }
            }
        }

        Rectangle {
            visible: root.statsOverlayOpen
            z: 18
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 28
            anchors.topMargin: root.abLoopA >= 0 || root.abLoopB >= 0 ? 140 : 92
            width: 330
            height: statsColumn.implicitHeight + 28
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.86)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            Column {
                id: statsColumn
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 5
                Text {
                    width: parent.width
                    text: "Playback stats"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.8
                }
                Repeater {
                    model: [
                        "Resolution", "Frame rate", "Video codec", "Audio codec",
                        "HW decode", "Video bitrate", "Audio bitrate", "Dropped frames",
                        "Cache buffering", "Audio track", "Subtitle track", "Speed", "Volume"
                    ]
                    delegate: Row {
                        required property string modelData
                        width: statsColumn.width
                        height: 18
                        Text {
                            width: parent.width * 0.46
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width * 0.54
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.statsValue(modelData)
                            color: theme.ink
                            font.family: "Consolas"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Canvas {
            id: drawCanvas
            visible: root.drawStrokes.length > 0 || root.drawMode
            anchors.fill: parent
            z: 17
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.lineWidth = 4
                ctx.shadowColor = "rgba(0, 0, 0, 0.55)"
                ctx.shadowBlur = 4
                for (var i = 0; i < root.drawStrokes.length; i++) {
                    var stroke = root.drawStrokes[i]
                    var pts = stroke.points || []
                    if (pts.length < 2)
                        continue
                    ctx.beginPath()
                    ctx.strokeStyle = stroke.color || root.drawColor
                    ctx.moveTo(pts[0].x * width, pts[0].y * height)
                    for (var p = 1; p < pts.length; p++)
                        ctx.lineTo(pts[p].x * width, pts[p].y * height)
                    ctx.stroke()
                }
            }
        }

        MouseArea {
            id: drawInputArea
            visible: root.drawMode
            enabled: root.drawMode
            anchors.fill: parent
            z: 20
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: root.startDrawStroke(mouseX, mouseY)
            onPositionChanged: root.addDrawPoint(mouseX, mouseY)
            onReleased: root.endDrawStroke(mouseX, mouseY)
            onCanceled: root.endDrawStroke(mouseX, mouseY)
        }

        Rectangle {
            visible: root.frameGrabToastOpen
            z: 19
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 92
            width: Math.min(parent.width - 40, toastRow.implicitWidth + 30)
            height: 42
            radius: 21
            color: root.frameGrabFailed ? Qt.rgba(0.48, 0.10, 0.12, 0.88)
                                        : Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.88)
            border.width: 1
            border.color: root.frameGrabFailed ? Qt.rgba(1, 0.35, 0.35, 0.40)
                                               : Qt.rgba(1, 1, 1, 0.18)
            Row {
                id: toastRow
                anchors.centerIn: parent
                spacing: 10
                Text {
                    text: root.frameGrabToastText
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: !root.frameGrabFailed && root.frameGrabPath.length > 0
                    text: "Open folder"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mpv.revealCaptureFolder(root.frameGrabPath)
                    }
                }
            }
        }

        Rectangle {
            visible: root.subtitleDropToastOpen
            z: 19
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: root.frameGrabToastOpen ? 140 : 92
            width: Math.min(parent.width - 40, subtitleDropToastText.implicitWidth + 30)
            height: 42
            radius: 21
            color: root.subtitleDropToastFailed ? Qt.rgba(0.48, 0.10, 0.12, 0.88)
                                                : Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.88)
            border.width: 1
            border.color: root.subtitleDropToastFailed ? Qt.rgba(1, 0.35, 0.35, 0.40)
                                                       : Qt.rgba(1, 1, 1, 0.18)
            Text {
                id: subtitleDropToastText
                anchors.centerIn: parent
                text: root.subtitleDropToastText
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, parent.width - 26)
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Rectangle {
            visible: root.gifState !== "idle"
            z: 19
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: root.frameGrabToastOpen ? 140 : 92
            width: Math.min(parent.width - 40, gifChipRow.implicitWidth + 30)
            height: 42
            radius: 21
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.88)
            border.width: 1
            border.color: root.gifState === "encoding" ? theme.gold : Qt.rgba(1, 1, 1, 0.18)
            Row {
                id: gifChipRow
                anchors.centerIn: parent
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.gifState === "encoding" ? "Encoding GIF..." : "Recording GIF " + root.gifElapsedSec + "s"
                    color: root.gifState === "encoding" ? theme.gold : theme.ink
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: root.gifState === "recording"
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Stop"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.stopGifRecording()
                    }
                }
                Text {
                    visible: root.gifState === "recording"
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Discard"
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.abortGifRecording()
                    }
                }
            }
        }

        // Harbor-style "Up Next" card: visible, cancelable countdown to the next
        // episode instead of a silent jump.
        Rectangle {
            id: upNextCard
            visible: root.upNextVisible && !root.pipMode
            z: 22
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 28
            anchors.bottomMargin: 112
            width: 344
            height: upNextCol.implicitHeight + 28
            radius: 14
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.16)

            // Absorb background clicks so the card doesn't toggle play/pause.
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

            Column {
                id: upNextCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 12

                Row {
                    spacing: 12
                    width: parent.width

                    Rectangle {
                        width: 82
                        height: 46
                        radius: 6
                        clip: true
                        color: Qt.rgba(1, 1, 1, 0.06)
                        Image {
                            anchors.fill: parent
                            source: root.upNextArt()
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: status === Image.Ready
                        }
                    }

                    Column {
                        width: parent.width - 94
                        spacing: 3
                        Text {
                            text: "Up next  •  Playing in " + root.upNextRemainingSec + "s"
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.upNextTitle()
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            width: parent.width
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Row {
                    spacing: 10
                    anchors.right: parent.right

                    Rectangle {
                        width: upNextCancelText.implicitWidth + 24
                        height: 32
                        radius: 16
                        color: upNextCancelHover.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.16)
                        Text {
                            id: upNextCancelText
                            anchors.centerIn: parent
                            text: "Cancel"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: upNextCancelHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.cancelUpNext()
                        }
                    }

                    Rectangle {
                        width: upNextPlayText.implicitWidth + 26
                        height: 32
                        radius: 16
                        color: upNextPlayHover.containsMouse ? theme.gold : Qt.rgba(244 / 255, 178 / 255, 60 / 255, 0.88)
                        Text {
                            id: upNextPlayText
                            anchors.centerIn: parent
                            text: "Play now"
                            color: "#12100a"
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }
                        MouseArea {
                            id: upNextPlayHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.confirmUpNext()
                        }
                    }
                }
            }
        }

        Rectangle {
            id: bottomDockLayer
            visible: !root.pipMode
            z: 3
            x: 0
            y: 0
            width: parent.width
            height: parent.height
            color: "transparent"

            Rectangle {
                id: bottomDock
                // NOTE: no layer.enabled here. A layer renders the dock to an offscreen
                // texture sized to the dock, which CLIPS the audio/subtitle/speed/fill
                // popovers (they open above the dock at negative y) to nothing — the exact
                // "menus don't show up" bug. Keep the dock un-layered so popovers escape it.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: tight ? 14 : 28
            anchors.rightMargin: tight ? 14 : 28
            anchors.bottomMargin: tight ? 12 : 22
            height: tight ? 130 : 156
            radius: 22
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.50)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            Row {
                id: seekRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: tight ? 16 : 22
                anchors.rightMargin: tight ? 16 : 22
                anchors.topMargin: 16
                height: 42
                spacing: 12

                Text {
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(root.seeking ? root.seekPreview : mpv.position)
                    color: theme.ink
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }

                Item {
                    id: seekBar
                    width: seekRow.width - (tight ? 0 : 140)
                    height: parent.height
                    property bool hovered: false
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: seekBar.hovered || root.seeking ? 8 : 6
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * root.seekFraction()
                        height: seekBar.hovered || root.seeking ? 8 : 6
                        radius: height / 2
                        color: theme.gold
                    }
                    Rectangle {
                        x: parent.width * root.seekFraction() - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.seeking ? 20 : 16
                        height: width
                        radius: width / 2
                        color: theme.gold
                        border.width: 1
                        border.color: Qt.rgba(0, 0, 0, 0.32)
                        visible: mpv.duration > 0
                    }
                    Rectangle {
                        visible: seekBar.hovered && !root.seeking && mpv.duration > 0
                        x: root.clamp(seekHover.mouseX - width / 2, 0, parent.width - width)
                        y: -30
                        width: previewText.implicitWidth + 16
                        height: 28
                        radius: 7
                        color: Qt.rgba(0, 0, 0, 0.86)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.10)
                        Text {
                            id: previewText
                            anchors.centerIn: parent
                            text: root.fmtTime(root.seekPreview)
                            color: theme.ink
                            font.family: "Consolas"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                    MouseArea {
                        id: seekHover
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: mpv.duration > 0
                        cursorShape: Qt.PointingHandCursor
                        onEntered: { seekBar.hovered = true; root.wakeChrome() }
                        onExited: {
                            seekBar.hovered = false
                            if (!root.seeking)
                                root.seekPreview = mpv.position
                        }
                        onPositionChanged: {
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                        }
                        onPressed: {
                            root.seeking = true
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                        }
                        onReleased: {
                            root.seekTo(root.seekPreview)
                            root.seeking = false
                        }
                    }
                }

                Text {
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(mpv.duration)
                    color: theme.inkDim
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignRight
                }
            }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: tight ? 16 : 22
                anchors.rightMargin: tight ? 16 : 22
                anchors.bottomMargin: 16
                height: 64

                VolumeControl {
                    id: volumeControl
                    visible: !tight
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                Row {
                    anchors.centerIn: parent
                    spacing: compact ? 6 : 8
                    RoundButton {
                        visible: root.hasAdjacentEpisode("prev")
                        size: tight ? 44 : 50
                        icon: "back"
                        tooltip: "Previous episode"
                        onClicked: root.goToAdjacentEpisode("prev")
                    }
                    RoundButton {
                        size: tight ? 48 : 56
                        icon: "seekBack"
                        label: root.seekBackSeconds
                        tooltip: "Back " + root.seekBackSeconds + "s"
                        onClicked: root.seekStep(-root.seekBackSeconds)
                    }
                    RoundButton {
                        size: tight ? 54 : 64
                        icon: mpv.pause ? "play" : "pause"
                        hero: true
                        tooltip: mpv.pause ? "Play" : "Pause"
                        onClicked: root.togglePlayPause()
                    }
                    RoundButton {
                        size: tight ? 48 : 56
                        icon: "seekForward"
                        label: root.seekForwardSeconds
                        tooltip: "Forward " + root.seekForwardSeconds + "s"
                        onClicked: root.seekStep(root.seekForwardSeconds)
                    }
                    RoundButton {
                        visible: root.hasAdjacentEpisode("next")
                        size: tight ? 44 : 50
                        icon: "nextEpisode"
                        tooltip: "Next episode"
                        onClicked: root.goToAdjacentEpisode("next")
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    RoundButton {
                        visible: root.errored || root.starting
                        size: 48
                        icon: "retry"
                        tooltip: "Retry stream"
                        onClicked: root.retryCurrentStream()
                    }

                    RoundButton {
                        visible: root.streamCandidates.length > 1
                        size: 48
                        icon: "stream"
                        tooltip: "Pick another stream"
                        onClicked: root.pickAnotherStream()
                    }

                    RoundButton {
                        visible: root.currentCastUrl().length > 0
                        size: 48
                        icon: root.downloadIcon()
                        active: typeof Download !== "undefined" && Download.status.kind !== "idle"
                        label: (typeof Download !== "undefined" && Download.status.kind === "downloading")
                               ? Math.round((Download.status.ratio || 0) * 100)
                               : ""
                        tooltip: root.downloadTooltip()
                        onClicked: root.handleDownloadAction()
                    }

                    AudioMenu {
                        id: audioMenu
                        visible: !tight
                        onToggleRequested: function(wasOpen) {
                            root.closeMenus()
                            audioMenu.panelOpen = !wasOpen
                            root.wakeChrome()
                        }
                        icon: "audio"
                        title: "Audio"
                        count: mpv.audioTracks.length
                        panelWidth: 360
                        panelHeight: Math.min(310, 86 + Math.max(1, mpv.audioTracks.length) * 48 + 42)
                        delegateModel: root.audioRows
                        selectedId: mpv.audioTrack
                        emptyText: "No alternate audio tracks in this file."
                        syncValue: mpv.audioDelay
                        onTrackPicked: function(trackId) { mpv.audioTrack = trackId }
                        onDelayStep: function(delta) { mpv.audioDelay = root.round2(mpv.audioDelay + delta) }
                        onResetDelay: mpv.audioDelay = 0
                    }

                    SubtitleMenu {
                        id: subMenu
                        onToggleRequested: function(wasOpen) {
                            root.closeMenus()
                            subMenu.panelOpen = !wasOpen
                            root.wakeChrome()
                        }
                        icon: "subtitle"
                        title: "Subtitles"
                        // Combined: embedded/loaded mpv tracks + online subs (OpenSubtitles).
                        count: root.subRows.length
                        panelWidth: 380
                        panelHeight: Math.min(360, 124 + Math.max(1, root.subRows.length) * 48 + 42)
                        delegateModel: root.subRows
                        selectedId: mpv.subtitleTrack
                        searchType: root.subtitleSearchMeta.type
                        searchId: root.subtitleSearchMeta.imdbId.length ? root.subStreamId : ""
                        emptyText: root.subsLoading ? "Finding subtitles…" : "No subtitles found for this title."
                        offRow: true
                        syncValue: mpv.subDelay
                        active: mpv.subtitleTrack !== ""
                        onTrackPicked: function(trackId) { root.userTouchedSubs = true; root.pickSubtitle(trackId) }
                        onOffPicked: { root.userTouchedSubs = true; mpv.subtitleTrack = "" }
                        onDelayStep: function(delta) { mpv.subDelay = root.round2(mpv.subDelay + delta) }
                        onResetDelay: mpv.subDelay = 0
                        onStyleRequested: {
                            subStyleBar.open = !subStyleBar.open
                            root.wakeChrome()
                        }
                        onFileLoaded: function(fileUrl) { root.loadSubtitleFile(fileUrl) }
                        onOnlinePicked: function(fileUrl, title, lang) { root.addOnlineSubtitle(fileUrl, title, lang) }
                    }

                    SpeedMenuButton {
                        id: speedMenu
                        visible: !compact
                    }

                    FillMenuButton {
                        id: fillMenu
                        visible: !compact
                    }

                    ToolsMenu {
                        id: toolsMenu
                        actions: [
                            { "icon": "camera", "label": "Screenshot", "active": false, "enabled": true,
                              "trigger": function() { root.captureFrameGrab() } },
                            { "icon": "gif", "label": root.gifState === "recording" ? "Stop GIF" : "Record GIF",
                              "active": root.gifState !== "idle", "enabled": true,
                              "trigger": function() {
                                  if (root.gifState === "recording") root.stopGifRecording()
                                  else if (root.gifState === "idle") root.startGifRecording()
                              } },
                            { "icon": "stats", "label": "Playback stats", "active": root.statsOverlayOpen, "enabled": true,
                              "trigger": function() {
                                  root.statsOverlayOpen = !root.statsOverlayOpen
                                  if (root.statsOverlayOpen) root.refreshPlaybackStats()
                              } },
                            { "icon": "draw", "label": "Draw", "active": root.drawMode, "enabled": true,
                              "trigger": function() { root.toggleDrawMode() } },
                            { "icon": "cast", "label": "Cast to device",
                              "active": root.castPanelOpen || (typeof Cast !== "undefined" && Cast.active), "enabled": true,
                              "trigger": function() {
                                  var wasOpen = root.castPanelOpen
                                  root.closeMenus()
                                  if (!wasOpen) root.openCastPanel(); else root.castPanelOpen = false
                              } },
                            { "icon": "room", "label": "Watch room",
                              "active": root.roomPanelOpen || (typeof Room !== "undefined" && Room.active), "enabled": true,
                              "trigger": function() {
                                  var wasOpen = root.roomPanelOpen
                                  root.closeMenus()
                                  root.roomPanelOpen = !wasOpen
                              } },
                            { "icon": "live", "label": "Live guide", "active": root.liveGuideOpen,
                              "enabled": (typeof Live !== "undefined" && Live.isLive),
                              "trigger": function() {
                                  var wasOpen = root.liveGuideOpen
                                  root.closeMenus()
                                  if (!wasOpen) root.openLiveGuide(); else root.liveGuideOpen = false
                              } },
                            { "icon": "record", "label": "DVR record",
                              "active": root.dvrPanelOpen || root.currentDvrId.length > 0,
                              "enabled": (typeof Live !== "undefined" && Live.isLive),
                              "trigger": function() {
                                  var wasOpen = root.dvrPanelOpen
                                  root.closeMenus()
                                  root.dvrPanelOpen = !wasOpen
                              } },
                            { "icon": "live", "label": "Go to live edge", "active": false,
                              "enabled": (typeof Live !== "undefined" && Live.isLive),
                              "trigger": function() { root.goLiveEdge() } }
                        ]
                    }

                    RoundButton {
                        size: 48
                        icon: "minimize"
                        tooltip: "Minimize"
                        onClicked: {
                            root.closeMenus()
                            if (!mpv.pause) { root.autoPausedInactive = true; mpv.pause = true }
                            root.minimizeRequested()
                        }
                    }

                    // close = end this watching session (Windows-window vocabulary; wired to
                    // closePlayerSession in the shell). [A5 addition riding A4's in-flight chrome]
                    RoundButton {
                        size: 48
                        icon: "cancel"
                        tooltip: "Close"
                        onClicked: {
                            root.closeMenus()
                            root.closeRequested()
                        }
                    }
                }
            }
        }
        }

    }

    component RoomActionButton: Item {
        id: rab
        property string label: ""
        property bool active: false
        signal clicked()
        width: Math.max(92, actionText.implicitWidth + 22)
        height: 34
        Rectangle {
            anchors.fill: parent
            radius: 9
            color: rab.active ? Qt.rgba(0.95, 0.68, 0.18, 0.22)
                              : actionMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12)
                              : Qt.rgba(1, 1, 1, 0.07)
            border.width: 1
            border.color: rab.active ? theme.gold : Qt.rgba(1, 1, 1, 0.10)
        }
        Text {
            id: actionText
            anchors.centerIn: parent
            text: rab.label
            color: rab.active ? theme.gold : theme.ink
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rab.clicked()
        }
    }

    component RoundButton: Item {
        id: rb
        property int size: 48
        property string icon: ""
        property string label: ""
        property string tooltip: ""
        property bool hero: false
        property bool active: false
        signal clicked()
        width: size
        height: size
        scale: press.pressed ? 0.95 : (press.containsMouse ? 1.04 : 1)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: rb.hero ? (press.containsMouse ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.13))
                           : rb.active ? Qt.rgba(1, 1, 1, 0.16)
                           : press.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
            border.width: rb.hero || rb.active ? 1 : 0
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }
        IconGlyph {
            anchors.fill: parent
            kind: rb.icon
            label: rb.label
            hero: rb.hero
            ink: rb.active ? theme.gold : theme.ink
        }
        MouseArea {
            id: press
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: root.wakeChrome()
            onClicked: rb.clicked()
        }
    }

    component VolumeControl: Item {
        id: vc
        width: mpv.volume > 101 ? 230 : 190
        height: 48
        RoundButton {
            id: muteButton
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            size: 48
            icon: mpv.mute || mpv.volume <= 0 ? "mute" : "volume"
            active: mpv.mute
            tooltip: "Mute"
            onClicked: mpv.mute = !mpv.mute
        }
        Item {
            id: volBar
            anchors.left: muteButton.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: vc.width - muteButton.width - 12
            height: 34
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: volMouse.containsMouse ? 8 : 6
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.16)
            }
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * root.volumeFraction()
                height: volMouse.containsMouse ? 8 : 6
                radius: height / 2
                color: mpv.volume > 100 ? "#f26f25" : theme.gold
            }
            Rectangle {
                x: parent.width * root.volumeFraction() - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: mpv.volume > 100 ? "#f26f25" : theme.gold
            }
            Text {
                visible: mpv.volume > 100
                anchors.left: parent.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(mpv.volume) + "%"
                color: "#f26f25"
                font.family: "Consolas"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            MouseArea {
                id: volMouse
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                function apply() { root.setVolumeFromFraction(mouseX / Math.max(1, volBar.width)) }
                onEntered: root.wakeChrome()
                onPressed: apply()
                onPositionChanged: if (pressed) apply()
            }
        }
    }

    // Harbor-style tools overflow: one "..." button opens a small menu of the
    // occasional player tools (screenshot, GIF, stats, draw, cast, room, live).
    // Keeps the bar from becoming a wall of icons that overflow the transport.
    component ToolsMenu: Item {
        id: tm
        property bool panelOpen: false
        // Each action: { icon, label, active(bool), enabled(bool), trigger(function) }
        property var actions: []
        width: 48
        height: 48

        RoundButton {
            anchors.fill: parent
            size: 48
            icon: "more"
            active: tm.panelOpen
            tooltip: "Tools"
            onClicked: {
                var wasOpen = tm.panelOpen
                root.closeMenus()
                tm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }

        Rectangle {
            // Reparented to full-screen chrome so rows above the dock stay clickable.
            parent: chrome
            visible: tm.panelOpen
            z: 40
            width: 232
            height: toolsCol.implicitHeight + 20
            onVisibleChanged: if (visible) {
                var p = tm.mapToItem(chrome, 0, 0)
                x = p.x + tm.width - width
                y = p.y - height - 12
            }
            radius: 16
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Column {
                id: toolsCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                spacing: 2

                Repeater {
                    model: tm.actions
                    delegate: Rectangle {
                        id: toolRow
                        required property var modelData
                        width: toolsCol.width
                        height: (modelData.enabled === false) ? 0 : 40
                        visible: modelData.enabled !== false
                        radius: 9
                        color: modelData.active ? Qt.rgba(0.95, 0.68, 0.18, 0.16)
                              : toolMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            IconGlyph {
                                width: 22
                                height: 22
                                anchors.verticalCenter: parent.verticalCenter
                                kind: toolRow.modelData.icon
                                ink: toolRow.modelData.active ? theme.gold : theme.ink
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: toolRow.modelData.label
                                color: toolRow.modelData.active ? theme.gold : theme.ink
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: toolMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                tm.panelOpen = false
                                toolRow.modelData.trigger()
                                root.wakeChrome()
                            }
                        }
                    }
                }
            }
        }
    }

    component PlayerMenu: Item {
        id: menu
        property bool panelOpen: false
        property string icon: ""
        property string title: ""
        property int count: 0
        property int panelWidth: 360
        property int panelHeight: 280
        property var delegateModel: []
        property string emptyText: ""
        property bool offRow: false
        property bool active: false
        property real syncValue: 0
        signal trackPicked(string trackId)
        signal offPicked()
        signal delayStep(real delta)
        signal resetDelay()

        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: menu.icon
            active: menu.panelOpen || menu.active
            tooltip: menu.title
            onClicked: {
                var wasOpen = menu.panelOpen
                root.closeMenus()
                menu.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Rectangle {
            visible: menu.panelOpen
            z: 20
            width: menu.panelWidth
            height: menu.panelHeight
            x: parent.width - width
            y: -height - 12
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                id: menuTitle
                x: 18
                y: 15
                text: menu.title
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: menuTitle.right
                anchors.leftMargin: 8
                anchors.verticalCenter: menuTitle.verticalCenter
                text: menu.count
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
            }
            Rectangle { x: 0; y: 48; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

            Rectangle {
                visible: menu.offRow
                x: 10
                y: 58
                width: parent.width - 20
                height: 34
                radius: 7
                color: !menu.active ? Qt.rgba(1, 1, 1, 0.10) : (offMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                border.width: !menu.active ? 1 : 0
                border.color: Qt.rgba(1, 1, 1, 0.10)
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Off"
                    color: !menu.active ? theme.ink : theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: offMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.offPicked()
                        menu.panelOpen = false
                    }
                }
            }

            ListView {
                id: menuList
                x: 10
                y: menu.offRow ? 98 : 58
                width: parent.width - 20
                height: parent.height - y - 46
                model: menu.delegateModel
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: trackRow
                    required property var modelData
                    width: ListView.view.width
                    height: 46
                    radius: 8
                    property bool selected: modelData.selected === true
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (trackMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: selected ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    Rectangle {
                        x: 10
                        y: 15
                        width: 16
                        height: 16
                        radius: 8
                        color: trackRow.selected ? theme.gold : Qt.rgba(1, 1, 1, 0.08)
                    }
                    Text {
                        x: 36
                        y: 7
                        width: parent.width - 48
                        text: root.trackTitle(modelData, menu.title === "Audio" ? "Audio track" : "Subtitle")
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 36
                        y: 25
                        width: parent.width - 48
                        text: root.trackMeta(modelData)
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: trackMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            menu.trackPicked("" + modelData.id)
                            menu.panelOpen = false
                        }
                    }
                }
            }

            Text {
                visible: menu.count === 0
                x: 18
                y: menu.offRow ? 108 : 68
                width: parent.width - 36
                text: menu.emptyText
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                height: 40
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "SYNC"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                DelayButton { text: "-0.1"; onClicked: menu.delayStep(-0.1) }
                Text {
                    width: 88
                    anchors.verticalCenter: parent.verticalCenter
                    text: (menu.syncValue >= 0 ? "+" : "") + menu.syncValue.toFixed(2) + "s"
                    color: theme.ink
                    font.family: "Consolas"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                }
                DelayButton { text: "+0.1"; onClicked: menu.delayStep(0.1) }
                DelayButton {
                    visible: Math.abs(menu.syncValue) > 0.0001
                    text: "0"
                    onClicked: menu.resetDelay()
                }
            }
        }
    }

    component SpeedMenuButton: Item {
        id: sm
        property bool panelOpen: false
        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: "speed"
            // Harbor: badge the rate only when ≠ 1× (no "1×" at normal speed); use the × glyph.
            label: Math.abs(mpv.speed - 1) < 0.01 ? "" : ((Math.round(mpv.speed * 100) / 100) + "×")
            active: sm.panelOpen || Math.abs(mpv.speed - 1) > 0.01 || root.sleepTimerActive
            tooltip: "Speed & sleep"
            onClicked: {
                var wasOpen = sm.panelOpen
                root.closeMenus()
                sm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Text {
            visible: root.sleepTimerActive
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            text: root.sleepTimerLabel()
            color: theme.gold
            font.family: "Consolas"
            font.pixelSize: 10
            font.weight: Font.DemiBold
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.85)
        }
        Rectangle {
            // Reparented to full-screen chrome so rows above the dock stay clickable.
            parent: chrome
            visible: sm.panelOpen
            z: 40
            width: 420
            height: 54 + Math.max(root.speedChoices.length, root.sleepPresets.length + (root.sleepTimerActive ? 1 : 0)) * 38
            onVisibleChanged: if (visible) {
                var p = sm.mapToItem(chrome, 0, 0)
                x = p.x + sm.width - width
                y = p.y - height - 12
            }
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
            // Harbor's exact section title (uppercase eyebrow).
            Text {
                x: 18
                y: 16
                text: "Playback speed"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.6
            }
            Text {
                x: parent.width / 2 + 18
                y: 16
                text: "Sleep timer"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.6
            }
            Rectangle {
                x: parent.width / 2
                y: 8
                width: 1
                height: parent.height - 16
                color: Qt.rgba(1, 1, 1, 0.10)
            }
            Repeater {
                model: root.speedChoices
                delegate: Rectangle {
                    required property int index
                    required property real modelData
                    x: 8
                    y: 46 + index * 38
                    width: parent.width / 2 - 16
                    height: 36
                    radius: 9
                    property bool selected: Math.abs(mpv.speed - modelData) < 0.01
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (speedMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: selected ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    // Harbor: "Normal" for 1×, else "1.25×" — left-aligned.
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.abs(modelData - 1) < 0.01 ? "Normal" : ((Math.round(modelData * 100) / 100) + "×")
                        color: parent.selected ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: parent.selected ? Font.DemiBold : Font.Medium
                    }
                    // Harbor: "default" hint on the Normal row.
                    Text {
                        visible: Math.abs(modelData - 1) < 0.01
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: "DEFAULT"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                    }
                    MouseArea {
                        id: speedMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            mpv.speed = modelData
                            sm.panelOpen = false
                            root.wakeChrome()
                        }
                    }
                }
            }
            Repeater {
                model: root.sleepPresets
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    x: parent.width / 2 + 8
                    y: 46 + index * 38
                    width: parent.width / 2 - 16
                    height: 36
                    radius: 9
                    property bool selected: root.sleepPresetSelected(modelData)
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (sleepMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: selected ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 76
                        text: modelData.label
                        color: parent.selected ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: parent.selected ? Font.DemiBold : Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: parent.selected && root.sleepTimerMode === "minutes"
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.sleepTimerLabel()
                        color: theme.inkDimmer
                        font.family: "Consolas"
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: sleepMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.setSleepTimer(modelData)
                            sm.panelOpen = false
                            root.wakeChrome()
                        }
                    }
                }
            }
            Rectangle {
                visible: root.sleepTimerActive
                x: parent.width / 2 + 8
                y: 46 + root.sleepPresets.length * 38
                width: parent.width / 2 - 16
                height: 36
                radius: 9
                color: cancelSleepMouse.containsMouse ? Qt.rgba(0.85, 0.18, 0.20, 0.16) : "transparent"
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Cancel timer"
                    color: "#ff8a8a"
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: cancelSleepMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.cancelSleepTimer()
                        sm.panelOpen = false
                    }
                }
            }
        }
    }

    component FillMenuButton: Item {
        id: fm
        property bool panelOpen: false
        width: 48
        height: 48
        RoundButton {
            anchors.fill: parent
            size: 48
            icon: "fit"
            active: fm.panelOpen || root.fillModeIndex !== 0
            tooltip: "Video fill"
            onClicked: {
                var wasOpen = fm.panelOpen
                root.closeMenus()
                fm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        Rectangle {
            // Reparented to the full-screen chrome layer: a popover nested in the short
            // bottom dock loses clicks on any row that renders above the dock (Qt bounds
            // pointer delivery to the dock ancestor). In chrome the whole panel is clickable.
            parent: chrome
            visible: fm.panelOpen
            z: 40
            width: 188
            height: 56 + root.fillModes.length * 34
            onVisibleChanged: if (visible) {
                var p = fm.mapToItem(chrome, 0, 0)
                x = p.x + fm.width - width
                y = p.y - height - 12
            }
            radius: 18
            color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
            Text {
                x: 18
                y: 15
                text: "Video"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Repeater {
                model: root.fillModes
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    x: 8
                    y: 48 + index * 34
                    width: parent.width - 16
                    height: 32
                    radius: 8
                    property bool selected: root.fillModeIndex === index
                    color: selected ? Qt.rgba(1, 1, 1, 0.10) : (fillMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.selected ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: fillMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.applyFill(index)
                    }
                }
            }
        }
    }

    component DelayButton: Item {
        id: db
        property string text: ""
        signal clicked()
        width: 42
        height: 24
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: dbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        }
        Text {
            anchors.centerIn: parent
            text: db.text
            color: theme.inkDim
            font.family: "Consolas"
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: dbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: db.clicked()
        }
    }

    component IconGlyph: Canvas {
        id: glyph
        property string kind: ""
        property string label: ""
        property bool hero: false
        property color ink: theme.ink
        antialiasing: true
        onKindChanged: requestPaint()
        onLabelChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var s = Math.min(w, h)
            var cx = w / 2
            var cy = h / 2
            ctx.clearRect(0, 0, w, h)
            ctx.strokeStyle = ink
            ctx.fillStyle = ink
            ctx.lineWidth = Math.max(1.5, s / 24)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            function line(x1, y1, x2, y2) {
                ctx.beginPath()
                ctx.moveTo(cx + x1 * s, cy + y1 * s)
                ctx.lineTo(cx + x2 * s, cy + y2 * s)
                ctx.stroke()
            }
            function circleArc(r, a1, a2, ccw) {
                ctx.beginPath()
                ctx.arc(cx, cy, r * s, a1 * Math.PI / 180, a2 * Math.PI / 180, ccw)
                ctx.stroke()
            }

            if (kind === "play") {
                ctx.beginPath()
                ctx.moveTo(cx - 0.08 * s, cy - 0.18 * s)
                ctx.lineTo(cx - 0.08 * s, cy + 0.18 * s)
                ctx.lineTo(cx + 0.21 * s, cy)
                ctx.closePath()
                ctx.fill()
            } else if (kind === "pause") {
                ctx.fillRect(cx - 0.16 * s, cy - 0.19 * s, 0.09 * s, 0.38 * s)
                ctx.fillRect(cx + 0.07 * s, cy - 0.19 * s, 0.09 * s, 0.38 * s)
            } else if (kind === "back") {
                line(0.12, -0.22, -0.12, 0)
                line(-0.12, 0, 0.12, 0.22)
            } else if (kind === "minimizeToBar") {
                // down-into-the-bar: session drops to the taskbar and keeps playing
                line(0, -0.28, 0, 0.06)
                line(-0.15, -0.09, 0, 0.06)
                line(0, 0.06, 0.15, -0.09)
                line(-0.22, 0.24, 0.22, 0.24)
            } else if (kind === "seekBack" || kind === "seekForward") {
                var fwd = kind === "seekForward"
                circleArc(0.27, fwd ? 320 : 220, fwd ? 55 : 140, !fwd)
                if (fwd) {
                    line(0.22, -0.23, 0.35, -0.20)
                    line(0.35, -0.20, 0.27, -0.08)
                } else {
                    line(-0.22, -0.23, -0.35, -0.20)
                    line(-0.35, -0.20, -0.27, -0.08)
                }
                ctx.font = "700 " + Math.round(s * 0.18) + "px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(label, cx, cy + s * 0.02)
            } else if (kind === "nextEpisode") {
                ctx.beginPath()
                ctx.moveTo(cx - 0.24 * s, cy - 0.20 * s)
                ctx.lineTo(cx - 0.24 * s, cy + 0.20 * s)
                ctx.lineTo(cx + 0.04 * s, cy)
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                ctx.moveTo(cx + 0.02 * s, cy - 0.20 * s)
                ctx.lineTo(cx + 0.02 * s, cy + 0.20 * s)
                ctx.lineTo(cx + 0.30 * s, cy)
                ctx.closePath()
                ctx.fill()
                ctx.fillRect(cx + 0.34 * s, cy - 0.20 * s, 0.05 * s, 0.40 * s)
            } else if (kind === "retry") {
                circleArc(0.28, 35, 330, false)
                line(0.24, -0.24, 0.38, -0.24)
                line(0.38, -0.24, 0.34, -0.09)
            } else if (kind === "download") {
                line(0, -0.30, 0, 0.12)
                line(-0.16, -0.04, 0, 0.12)
                line(0.16, -0.04, 0, 0.12)
                line(-0.28, 0.26, 0.28, 0.26)
            } else if (kind === "cancel") {
                line(-0.22, -0.22, 0.22, 0.22)
                line(0.22, -0.22, -0.22, 0.22)
            } else if (kind === "check") {
                line(-0.26, 0.00, -0.08, 0.18)
                line(-0.08, 0.18, 0.28, -0.20)
            } else if (kind === "warning") {
                ctx.beginPath()
                ctx.moveTo(cx, cy - 0.30 * s)
                ctx.lineTo(cx - 0.30 * s, cy + 0.24 * s)
                ctx.lineTo(cx + 0.30 * s, cy + 0.24 * s)
                ctx.closePath()
                ctx.stroke()
                line(0, -0.12, 0, 0.08)
                ctx.beginPath()
                ctx.arc(cx, cy + 0.18 * s, 0.025 * s, 0, 2 * Math.PI)
                ctx.fill()
            } else if (kind === "camera") {
                ctx.strokeRect(cx - 0.28 * s, cy - 0.16 * s, 0.56 * s, 0.38 * s)
                ctx.fillRect(cx - 0.16 * s, cy - 0.25 * s, 0.18 * s, 0.08 * s)
                ctx.beginPath()
                ctx.arc(cx, cy + 0.03 * s, 0.13 * s, 0, 2 * Math.PI)
                ctx.stroke()
            } else if (kind === "gif") {
                ctx.strokeRect(cx - 0.30 * s, cy - 0.20 * s, 0.60 * s, 0.40 * s)
                ctx.font = "800 " + Math.round(s * 0.17) + "px " + theme.ui
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText("GIF", cx, cy + 0.01 * s)
            } else if (kind === "stats") {
                ctx.strokeRect(cx - 0.30 * s, cy - 0.24 * s, 0.60 * s, 0.48 * s)
                line(-0.18, 0.12, -0.18, -0.02)
                line(0, 0.12, 0, -0.16)
                line(0.18, 0.12, 0.18, -0.08)
                line(-0.25, 0.18, 0.25, 0.18)
            } else if (kind === "draw") {
                ctx.save()
                ctx.translate(cx, cy)
                ctx.rotate(-Math.PI / 4)
                ctx.strokeRect(-0.08 * s, -0.28 * s, 0.16 * s, 0.45 * s)
                ctx.beginPath()
                ctx.moveTo(-0.08 * s, 0.17 * s)
                ctx.lineTo(0, 0.32 * s)
                ctx.lineTo(0.08 * s, 0.17 * s)
                ctx.stroke()
                ctx.restore()
            } else if (kind === "stream") {
                circleArc(0.26, 210, 330, false)
                circleArc(0.17, 210, 330, false)
                circleArc(0.08, 210, 330, false)
                ctx.beginPath()
                ctx.arc(cx - 0.28 * s, cy + 0.20 * s, 0.035 * s, 0, 2 * Math.PI)
                ctx.fill()
            } else if (kind === "cast") {
                ctx.strokeRect(cx - 0.28 * s, cy - 0.24 * s, 0.56 * s, 0.36 * s)
                circleArc(0.30, 215, 315, false)
                circleArc(0.20, 215, 315, false)
                circleArc(0.08, 215, 315, false)
                ctx.beginPath()
                ctx.arc(cx - 0.28 * s, cy + 0.24 * s, 0.035 * s, 0, 2 * Math.PI)
                ctx.fill()
            } else if (kind === "live") {
                ctx.beginPath()
                ctx.arc(cx, cy, 0.08 * s, 0, 2 * Math.PI)
                ctx.fill()
                circleArc(0.18, 220, 320, false)
                circleArc(0.30, 220, 320, false)
                circleArc(0.18, 40, 140, false)
                circleArc(0.30, 40, 140, false)
            } else if (kind === "record") {
                ctx.beginPath()
                ctx.arc(cx, cy, 0.22 * s, 0, 2 * Math.PI)
                ctx.fill()
            } else if (kind === "pip") {
                ctx.strokeRect(cx - 0.30 * s, cy - 0.22 * s, 0.60 * s, 0.44 * s)
                ctx.strokeRect(cx + 0.02 * s, cy - 0.02 * s, 0.26 * s, 0.18 * s)
                line(0.02, 0.16, -0.12, 0.16)
                line(-0.12, 0.16, -0.12, 0.04)
            } else if (kind === "room") {
                ctx.beginPath()
                ctx.arc(cx - 0.16 * s, cy - 0.07 * s, 0.11 * s, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx + 0.17 * s, cy - 0.08 * s, 0.09 * s, 0, 2 * Math.PI)
                ctx.stroke()
                circleArc(0.24, 205, 335, false)
                circleArc(0.18, 205, 335, false)
            } else if (kind === "volume" || kind === "mute") {
                ctx.beginPath()
                ctx.moveTo(cx - 0.34 * s, cy - 0.10 * s)
                ctx.lineTo(cx - 0.20 * s, cy - 0.10 * s)
                ctx.lineTo(cx - 0.03 * s, cy - 0.25 * s)
                ctx.lineTo(cx - 0.03 * s, cy + 0.25 * s)
                ctx.lineTo(cx - 0.20 * s, cy + 0.10 * s)
                ctx.lineTo(cx - 0.34 * s, cy + 0.10 * s)
                ctx.closePath()
                ctx.stroke()
                if (kind === "mute") {
                    line(0.15, -0.14, 0.36, 0.14)
                    line(0.36, -0.14, 0.15, 0.14)
                } else {
                    ctx.beginPath()
                    ctx.arc(cx + 0.04 * s, cy, 0.22 * s, -0.7, 0.7)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(cx + 0.04 * s, cy, 0.34 * s, -0.62, 0.62)
                    ctx.stroke()
                }
            } else if (kind === "audio") {
                circleArc(0.20, 0, 360, false)
                line(-0.20, -0.02, -0.36, -0.16)
                line(0.20, -0.02, 0.36, -0.16)
                line(-0.12, 0.18, -0.22, 0.34)
                line(0.12, 0.18, 0.22, 0.34)
            } else if (kind === "subtitle") {
                ctx.strokeRect(cx - 0.30 * s, cy - 0.20 * s, 0.60 * s, 0.40 * s)
                line(-0.20, 0.02, -0.02, 0.02)
                line(0.08, 0.02, 0.22, 0.02)
                line(-0.20, 0.13, 0.20, 0.13)
            } else if (kind === "speed") {
                circleArc(0.30, 205, 335, false)
                line(0, 0, 0.18, -0.13)
                ctx.font = "700 " + Math.round(s * 0.17) + "px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                if (label && label.length) ctx.fillText(label, cx, cy + s * 0.22)
            } else if (kind === "fit") {
                ctx.strokeRect(cx - 0.27 * s, cy - 0.18 * s, 0.54 * s, 0.36 * s)
                line(-0.17, -0.08, -0.27, -0.18)
                line(0.17, 0.08, 0.27, 0.18)
            } else if (kind === "fullscreen") {
                line(-0.30, -0.12, -0.30, -0.30)
                line(-0.30, -0.30, -0.12, -0.30)
                line(0.30, -0.12, 0.30, -0.30)
                line(0.30, -0.30, 0.12, -0.30)
                line(-0.30, 0.12, -0.30, 0.30)
                line(-0.30, 0.30, -0.12, 0.30)
                line(0.30, 0.12, 0.30, 0.30)
                line(0.30, 0.30, 0.12, 0.30)
            } else if (kind === "more") {
                for (var di = -1; di <= 1; di++) {
                    ctx.beginPath()
                    ctx.arc(cx + di * 0.22 * s, cy, 0.055 * s, 0, 2 * Math.PI)
                    ctx.fill()
                }
            } else if (kind === "minimize") {
                line(-0.22, 0.18, 0.22, 0.18)
            }
        }
    }
}
