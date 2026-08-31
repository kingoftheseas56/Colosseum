pragma ComponentBehavior: Bound

// PlayerPage - Harbor/TB3-style fullscreen player chrome on top of Colosseum's mpvqt MpvItem.
// Streaming remains behind the Stream.play -> streamReady seam; this file only owns player UI.
import QtQuick
import QtQuick.Window
import QtCore
import Colosseum.Player
import Colosseum.Activity
import "Subtitles.js" as Subtitles
import "Torrentio.js" as Torrentio
import "AddonClient.js" as AddonClient
import "SkipSegments.js" as SkipSegments
import "TrackLanguage.js" as TrackLanguage
import "PlayerTrackPrefs.js" as PlayerTrackPrefs
import "PlayerHotkeys.js" as PlayerHotkeys
import "EpisodeBrowser.js" as EpisodeBrowser
import "TheatreApi.js" as TheatreApi
import "ActivityLaneHelpers.js" as ActivityLaneHelpers

Item {
    id: root
    anchors.fill: parent
    focus: true

    // ── lanista test bridge (Agent 1's Theatre scenario, chat 47b0d14/80e2f32) ──────
    // Read-only proxies so an automated scenario asserts "playback actually ADVANCED",
    // never the naive "the page opened". Read via `qml-get {object:"player", props:[…]}`
    // (reads THIS item's declared props, not child objects — hence flat proxies over mpv).
    // playbackStarted mirrors finishStartingIfPlaybackAdvanced() so it cannot drift from
    // the app's own truth; the black-screen-buffering failure surfaces as a wait timeout.
    objectName: "player"
    readonly property bool playbackStarted:  !root.starting && !root.errored && mpv.position > 0.25
    readonly property real playbackPosition: mpv.position
    readonly property real playbackDuration: mpv.duration
    readonly property bool playerActive:     root.visible   // player item is the shown surface
    // (root.errored is already a readable bool on this root — no proxy needed.)

    // ── decoded-frame readiness (Agent Visibility Phase 2, Slice J1-Video-Seam) ─────
    // decodedWidth/decodedHeight mirror mpv's own dwidth/dheight (MpvItem.decodedWidth/
    // decodedHeight — mpvitem.cpp, the same two properties MediaAdmissionProbe.cpp:59-60
    // observes on its own headless handle). playerReady is true ONLY when a real frame
    // has decoded (both dimensions positive) AND the authoritative session is in a
    // playable state (root.fileReady — set true on mpv's fileLoaded, cleared on endFile
    // and every error path — and not root.errored). This is deliberately NOT derived
    // from fileReady/fileLoaded alone: fileReady already goes true at fileLoaded time
    // (PlayerPage.qml onFileLoaded, below), before any frame has decoded, and identically
    // for an audio-only source — the exact vacuity MediaAdmissionProbe.cpp closes for the
    // Vault admission gate, reproduced here for the live player (see baseline.json). No
    // timer, sleep, route/log-text derivation, or the separate admission-probe object.
    readonly property int    decodedWidth:    mpv.decodedWidth
    readonly property int    decodedHeight:   mpv.decodedHeight
    readonly property bool   playerReady:     root.fileReady && !root.errored
                                               && mpv.decodedWidth > 0 && mpv.decodedHeight > 0
    // Stable source identity: the same cross-source id Progress keys resume state on
    // (root.mediaId — set by openPlayer(), a tt-id/infoHash/local: path, never derived
    // from a route counter) when known, else mpv's own currentUrl as the fallback.
    readonly property string sourceIdentity:  root.mediaId.length > 0 ? root.mediaId
                                                                       : mpv.currentUrl.toString()
    // Slice 3: flat, read-only Watch Party sync truth for Lanista/qml-get.
    readonly property bool watchPartySyncActive:
        typeof WatchPartySync !== "undefined" && WatchPartySync.active
    readonly property string watchPartySyncStatus:
        typeof WatchPartySync !== "undefined" ? WatchPartySync.syncStatus : "inactive"
    readonly property real watchPartyDriftSeconds:
        typeof WatchPartySync !== "undefined" ? WatchPartySync.driftSeconds : 0
    readonly property bool watchPartyCatchUpAvailable:
        typeof WatchPartySync !== "undefined" && WatchPartySync.catchUpAvailable
    readonly property bool watchPartyCanControlTimeline:
        typeof WatchPartySync !== "undefined" && WatchPartySync.canControlTimeline
    // Slice 6: minimal scalar UI/room observability only. No credentials or chat
    // contents are exposed through these Player 1 diagnostics.
    readonly property bool watchPartyRoomActive:
        typeof WatchPartyUi !== "undefined" && WatchPartyUi.inRoom
    readonly property string watchPartyUiPhase:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.phase : "unavailable"
    readonly property string watchPartyControlMode:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.controlMode : "host"
    // Slice 7: exact scalar lifecycle observability for deterministic/Lanista
    // verification. These values intentionally omit Room ID, participant/source
    // identifiers, chat/reaction contents, and every credential.
    readonly property string watchPartyTransportState:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.transportState : "closed"
    readonly property string watchPartyErrorCategory:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.errorCategory : ""
    readonly property int watchPartyParticipantCount:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.participantCount : 0
    readonly property int watchPartyBufferingParticipantCount:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.bufferingParticipantCount : 0
    readonly property string watchPartyHostIdentityKind:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.hostIdentityKind : "none"
    readonly property bool watchPartyHostGraceActive:
        typeof WatchPartyUi !== "undefined" && WatchPartyUi.hostGraceActive
    readonly property string watchPartyLocalSyncStatus:
        typeof WatchPartyUi !== "undefined" ? WatchPartyUi.localSyncStatus : "inactive"
    readonly property bool watchPartySourceEligible: !!root.watchPartySource.eligible
    readonly property bool watchPartySourceMatchesRoom:
        !root.watchPartyRoomActive
        || root.watchPartyDescriptorsMatch(
            root.watchPartySource.descriptor || ({}),
            (typeof WatchPartyUi !== "undefined" ? WatchPartyUi.roomSource : ({})))
    readonly property bool watchPartyPanelOpen:
        typeof watchPartyMenu !== "undefined" && watchPartyMenu.panelOpen

    Settings {
        id: playerSettings
        location: Qt.resolvedUrl("../player.ini")
        category: "transport"
        property int seekStepSeconds: 10
        property bool showSkipButton: true
        property bool autoSkipIntro: false
        property bool autoSkipRecap: false
        property bool autoSkipCredits: false
        // Feature 6: subtitle/audio automation policy (conservative defaults).
        property string preferredAudioLanguages: "eng,jpn"
        property string preferredSubtitleLanguages: "eng"
        property string blockedTrackWords: "commentary"
        property bool preferEmbeddedSubtitles: true
        property bool subtitleAutoUpgrade: false
        property bool forcedSubsWhenNativeAudio: false
        property bool subtitlesOffByDefault: false
        property string trackPrefsJson: "{}"
        // Loudness normalization (2026-07-20 audit): "off" = smooth (default), "light" =
        // cheap dynaudnorm, "full" = EBU R128 loudnorm. Default off after loudnorm was
        // proven the primary stutter cause on weak hardware.
        property string loudnessMode: "off"
    }

    // --- skip segments (Feature 4): chapter- and AniSkip-derived intro/recap/credits ranges ---
    property var skipSegments: []
    property string skipDiagnostics: ""
    property real skipSafetyOffsetSec: 0.75
    property bool showSkipButton: playerSettings.showSkipButton
    property bool autoSkipIntro: playerSettings.autoSkipIntro
    property bool autoSkipRecap: playerSettings.autoSkipRecap
    property bool autoSkipCredits: playerSettings.autoSkipCredits

    // Loudness normalization — a live global mpv audio filter, applied on change + at load.
    property string loudnessMode: playerSettings.loudnessMode
    onLoudnessModeChanged: mpv.setAudioNormalization(root.loudnessMode)
    function loudnessLabel() {
        return root.loudnessMode === "full" ? "Full (EBU R128)"
             : root.loudnessMode === "light" ? "Light"
             : "Smooth"
    }
    function cycleLoudness() {
        // Smooth -> Light -> Full -> Smooth. Persists (player.ini) and applies live.
        playerSettings.loudnessMode = root.loudnessMode === "off" ? "light"
                                    : root.loudnessMode === "light" ? "full"
                                    : "off"
    }
    property string dismissedSkipKey: ""
    property string autoSkippedKey: ""
    property int skipLoadGeneration: 0

    property Item backdrop
    property string mediaTitle: ""
    property string mediaSubtitle: ""
    property string mediaTransport: ""   // "Torrent stream" | "Direct stream" | "Downloaded" — the tail fallback
    property string mediaYear: ""
    // --- continue/resume identity (set by openPlayer; fed to the Progress store) ---
    property string mediaId: ""           // stable id (Cinemeta ttXXXX if known, else infoHash)
    property string mediaArt: ""          // poster url, for the Continue card cover
    // --- per-show startup-loader identity (Task 4); missing values degrade to poster/subtitle ---
    property string mediaLogo: ""         // show logo (transparent art) centered on the loader
    property string mediaLoadingArt: ""   // episode still / show backdrop behind the loader
    property string mediaLoadingLine: ""  // "S1 · E03 · Name" line on the loader
    property string mediaResumeHash: ""   // resume payload: re-open this torrent...
    property int    mediaResumeFileIdx: 0 //   ...at this file index
    property string mediaLocalPath: ""    // downloaded-file playback: resume by path, not torrent
    property real   pendingSeekSec: -1    // seek here once the file opens (resume / session restore)

    // --- Your Colosseum activity (Lane A, CPP-PORT-CONTRACT.md §9): the identity key
    // ("kind|titleKey|itemKey") the tracker's current open session was begun for, so a
    // reload/recovery/stream-replacement of the SAME item resets the sampling baseline
    // (activityDiscontinuity) instead of fragmenting the session/10s gate, while a genuine
    // item/episode change ends the old session and begins a new one. Empty = no open session.
    property string activityActiveKey: ""

    // --- pre-play stream telemetry (Popcorn Time streamer.js parity, 2026-08-02): while the
    // loading face is up, the engine's stats.json feeds the status line — connecting → peers
    // found → buffering % of a 10 MB head (PT's BUFFERING_SIZE) at a live speed — so a dead
    // torrent reads as "Connecting to peers..." in seconds, not a minute of static text.
    // mpv's own mid-play "Buffering N%" (cache-buffering-state) is untouched.
    readonly property real streamHeadTargetBytes: 10 * 1024 * 1024
    property bool streamStatsSeen: false      // at least one stats sample for THIS stream
    property int  streamStatsPeers: 0
    property real streamStatsSpeedBps: 0
    property real streamStatsDownloaded: 0

    // --- resume choice (Feature 3): first-load-only Resume / Start over overlay ---
    property real resumePromptMinSec: 30       // only prompt after meaningful progress
    property real resumeRestartThreshold: 0.80 // at/over this fraction of duration, start over silently
    property bool resumeChoiceOpen: false      // is the overlay visible
    onResumeChoiceOpenChanged: root.syncWatchPartyPlayerObservation()
    property real resumeChoiceSec: -1          // the saved position the overlay offers
    property bool resumePromptConsumed: false  // prompt only once per source load

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

    // --- track automation (Feature 6): language-ranked audio/subtitle auto-select + per-show memory ---
    property bool   userTouchedAudio: false    // manual audio pick locks automation for this source
    property string trackAutoDoneKey: ""       // show key whose subtitle auto-select already ran
    property string autoAudioTrackId: ""       // last id automation chose (diagnostics)
    property string autoSubtitleTrackId: ""    // last id automation chose (diagnostics)
    property string autoTrackSignature: ""     // track-set+policy fingerprint; skip re-run when unchanged
    property int    trackAutomationRev: 0       // bumps each automation pass (debug/trace)

    // --- hotkeys (Feature 7): keyboard shortcuts sheet visibility ---
    property bool shortcutsOpen: false

    property bool browserOpen: false      // Feature 8: the episode/source drawer
    // Feature 8: the traveling episode queue (playbackContext.episodeQueue) retained so
    // the drawer's Episodes tab has an instant, fetch-free floor.
    property var playbackQueue: []
    property int playbackQueueIndex: -1
    // The ordering the current queue was built in ("absolute" | "seasons" | "").
    // A later AnimeOrder revision upgrades a non-absolute queue to absolute once
    // the cache is ready; an absolute queue is never re-fetched.
    property string playbackQueueOrderingMode: ""
    property int animeOrderRevision: (typeof AnimeOrder !== "undefined") ? AnimeOrder.revision : 0
    onAnimeOrderRevisionChanged: root.retryAnimeOrderHydration()

    property var streamCandidates: []
    property int currentStreamIndex: -1
    // Slice 2: one credential-free Player 1 → Watch Party source seam. The native
    // inspector mirrors playStreamAt's real route decision; unsupported direct URLs
    // stay unsupported until a provider owner supplies verified debrid semantics.
    readonly property var watchPartySource: (typeof WatchPartySource !== "undefined")
        ? WatchPartySource.describeCandidate(root.currentStreamCandidate())
        : ({ "eligible": false, "eligibility": "unsupported",
             "reason": "source_inspector_unavailable", "addonId": "", "descriptor": ({}) })
    property int streamRetryCount: 0
    property int streamWatchdogSeconds: 75

    // --- recovery watch + wake reconnect (Feature 3) ---
    property real positionFrozenSeconds: 18       // early-freeze window before recovering
    property real noVideoGraceSeconds: 20         // let mpv report video dimensions before "no video"
    property real wakeReconnectGapSeconds: 30     // tick gap that means the machine slept/stalled
    property int wakeReconnectTickMs: 2000        // light poll while a stream is open
    property real positionStartedFloorSec: 5      // only auto-recover freezes near startup
    property real positionAdvanceEpsilonSec: 0.3  // ignore tiny position jitter
    property string lastPlaybackErrorCode: ""
    property string lastPlaybackErrorMessage: ""
    property real recoveryLastPosition: 0
    property double recoveryLastMovedAt: 0
    property double recoveryUrlStartedAt: 0
    property bool recoverySawVideo: false
    property double recoveryNoVideoSince: 0
    property double wakeReconnectLastTickAt: 0
    property real wakeReconnectPendingSeek: -1

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
    property bool overflowOpen: false
    property point contextMenuPos: Qt.point(0, 0)
    function openContextMenu(p) {
        var wasOpen = root.overflowOpen
        root.closeMenus()
        root.contextMenuPos = p
        root.overflowOpen = !wasOpen
        root.wakeChrome()
    }
    // Destructive-path guard: closing while media is actively playing asks once
    // (spec 2026-07-06 slice 5 — minimize stays instant, paused/idle close is instant too).
    property bool closeConfirmOpen: false
    property string currentPlaybackUrl: ""
    property bool liveGuideOpen: false
    property bool dvrPanelOpen: false
    property string currentDvrId: ""
    property bool pipMode: typeof WindowMode !== "undefined" && WindowMode.pipMode
    readonly property bool shellWindowed:
        typeof WindowMode !== "undefined" && WindowMode.shellWindowed
    // Harbor pause-on-minimize (pauseMinimized:true default): pause when the window is
    // minimized, resume on restore — but only ever undoing a pause we caused.
    property bool autoPausedInactive: false
    readonly property bool windowMinimized: root.Window.window ? (root.Window.window.visibility === Window.Minimized) : false
    onWindowMinimizedChanged: {
        root.handleWindowMinimize()
        root.syncWatchPartyPlayerObservation()
    }
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

    // ── Glanceable truth (Tier 1, 2026-07-20): the player states what it knows. ──
    // The state line only SPEAKS when there is something to say — silent in plain play.
    property bool showRemaining: false                  // right clock: total <-> remaining
    property string endsAtClock: ""                     // "11:42 PM" — recomputed, not bound to churn
    property string nowClock: ""                         // live wall clock so "Ends" has an anchor
    property string chapterTransient: ""                // set on chapter crossing (Task 2), auto-clears
    // ── Pause info card (Tier 2): on pause, the player tells you what you're watching. ──
    property bool pauseCardShown: false                 // driven by the ~900ms settle timer
    property string mediaPlot: ""                        // lazily hydrated on first pause
    property string mediaEpisodeName: ""                // episode title, hydrated when absent
    property string pauseHydratedId: ""                 // guards one hydrate per media
    readonly property string stateLineText: {
        if (root.starting || root.errored) return ""
        // buffering: cache-buffering-state drops below 100 while the stream refills
        var buf = mpv.mpvProperty("cache-buffering-state")
        if (!mpv.pause && buf !== undefined && buf !== "" && Number(buf) < 100 && Number(buf) >= 0)
            return "Buffering " + Number(buf).toFixed(0) + "%"
        if (mpv.pause) return "Paused"
        if (root.seeking) {
            var d = root.seekPreview - mpv.position
            var sign = d >= 0 ? "+" : "-"
            return "Seek " + sign + root.fmtTime(Math.abs(d)) + " · " + root.fmtTime(root.seekPreview)
        }
        if (mpv.speed && Math.abs(mpv.speed - 1) > 0.01) return root.round2(mpv.speed) + "×"
        if (root.chapterTransient.length) return root.chapterTransient
        return ""
    }
    // Format a Date as a 12-hour wall clock ("6:59 PM").
    function fmtWallClock(date) {
        var h = date.getHours(), m = date.getMinutes()
        var ap = h >= 12 ? "PM" : "AM"
        var h12 = h % 12; if (h12 === 0) h12 = 12
        return h12 + ":" + (m < 10 ? "0" : "") + m + " " + ap
    }
    // The live clock ("6:00 PM now") + "Ends 6:59 PM" (now + remaining/speed). Recomputed on
    // a 1s tick + on the events that move the finish line, never bound to position churn.
    function updateEndsAt() {
        root.nowClock = root.fmtWallClock(new Date(Date.now()))
        if (!(mpv.duration > 0) || root.starting || root.errored || root.mediaLocalPath.indexOf("iptv:") === 0
                || root.mediaId.indexOf("iptv:") === 0) {
            root.endsAtClock = ""
            return
        }
        var rate = (mpv.speed && mpv.speed > 0.05) ? mpv.speed : 1
        var remainingSec = Math.max(0, (mpv.duration - mpv.position) / rate)
        root.endsAtClock = root.fmtWallClock(new Date(Date.now() + remainingSec * 1000))
    }

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
            root.maybeAutoSelectTracks("online-subs")  // Feature 6: automation before pickDefault fallback
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
            "selected": !!track.selected,
            "tech": root.trackTech(track, false),
            "tag": /sdh|hearing|\bhi\b/i.test(hiProbe) ? "SDH"
                 : (!!track.default ? "Default" : (!!track.forced ? "Forced" : ""))
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
            "title": subtitle.title || subtitle.langName || "OpenSubtitles",
            "tech": (subtitle.provider || "OpenSubtitles") + " · fetched",
            "tag": !!subtitle.hearingImpaired ? "SDH" : ""
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
            "selected": !!track.selected,
            // Tier 2 rich rows (2026-07-20): what this track actually carries.
            "tech": root.trackTech(track, true),
            "tag": root.trackTag(track)
        }
    }

    // Shared track-row formatters (Tier 2). mpv passes raw fields through tracksForType,
    // so channel-count/bitrate live under dashed keys reached by bracket access.
    function trackCodecLabel(track) {
        var c = String(track.codec || "").split(" ")[0]
        return c.length ? c.toUpperCase() : ""
    }
    function trackChannels(track) {
        var n = Number(track["demux-channel-count"] || track.channels || 0)
        return n > 0 ? root.channelLabel(n) : ""
    }
    function trackBitrate(track) {
        var b = Number(track["demux-bitrate"] || 0)
        return b > 0 ? Math.round(b / 1000) + " kbps" : ""
    }
    // Audio: codec · channels · bitrate. Subtitle: codec · embedded/external.
    function trackTech(track, isAudio) {
        var parts = []
        var codec = root.trackCodecLabel(track)
        if (codec.length) parts.push(codec)
        if (isAudio) {
            var ch = root.trackChannels(track); if (ch.length) parts.push(ch)
            var br = root.trackBitrate(track); if (br.length) parts.push(br)
        } else {
            parts.push(track.external ? "external" : "embedded")
        }
        return parts.join(" · ")
    }
    // One salient tag: SDH > Default > Forced (Original omitted — not reliably knowable).
    function trackTag(track) {
        var hi = track.hearingImpaired
        if (hi === undefined) hi = !!track["hearing-impaired"]
        if (hi) return "SDH"
        if (track.default) return "Default"
        if (track.forced) return "Forced"
        return ""
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
        // Feature 6: the language-ranked automation (maybeAutoSelectTracks) owns subtitle
        // auto-selection for this show. It stamps trackAutoDoneKey when it picks a sub OR turns
        // subs off; when it finds no language match it leaves the key unset, and maybeAutoSub
        // runs here as the pickDefault fallback. This prevents a double-add (two English subs),
        // since addSubtitle is async and mpv.subtitleTrack is still "" when both would run.
        if (root.trackAutoDoneKey.length && root.trackAutoDoneKey === root.currentShowKey())
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

    // --- track automation (Feature 6) ---
    function currentShowKey() {
        return TrackLanguage.showKey("video", root.subStreamId || root.mediaId || root.currentPlaybackUrl || "")
    }

    function currentTrackPreference() {
        return PlayerTrackPrefs.getPref(playerSettings.trackPrefsJson, root.currentShowKey())
    }

    function saveTrackPreference(patch) {
        playerSettings.trackPrefsJson = PlayerTrackPrefs.upsertPref(
                    playerSettings.trackPrefsJson, root.currentShowKey(), patch, Date.now())
    }

    function trackAutomationExcluded() {
        return root.subStreamId.indexOf("iptv:") === 0
            || root.mediaId.indexOf("iptv:") === 0
            || root.currentPlaybackUrl.indexOf("iptv:") === 0
    }

    function trackPolicyKey() {
        return [
            root.currentShowKey(),
            playerSettings.preferredAudioLanguages,
            playerSettings.preferredSubtitleLanguages,
            playerSettings.blockedTrackWords,
            playerSettings.preferEmbeddedSubtitles,
            playerSettings.subtitleAutoUpgrade,
            playerSettings.forcedSubsWhenNativeAudio,
            playerSettings.subtitlesOffByDefault
        ].join("|")
    }

    function effectiveAudioLanguages(pref) {
        var base = TrackLanguage.parseLanguageList(playerSettings.preferredAudioLanguages, ["eng", "jpn"])
        if (pref && pref.audioLang)
            return [pref.audioLang].concat(base.filter(function(x) { return x !== pref.audioLang }))
        return base
    }

    function effectiveSubtitleLanguages(pref) {
        var base = TrackLanguage.parseLanguageList(playerSettings.preferredSubtitleLanguages, ["eng"])
        if (pref && pref.subtitleLang)
            return [pref.subtitleLang].concat(base.filter(function(x) { return x !== pref.subtitleLang }))
        return base
    }

    function selectedAudioRow() {
        for (var i = 0; i < root.audioRows.length; i++)
            if (String(root.audioRows[i].id) === String(mpv.audioTrack)) return root.audioRows[i]
        return null
    }

    function selectedSubtitleRow() {
        for (var i = 0; i < root.subRows.length; i++)
            if (String(root.subRows[i].id) === String(mpv.subtitleTrack)) return root.subRows[i]
        return null
    }

    // Chip value derivations (native chrome spec 2026-07-08): a 2–3 letter language read
    // of the ACTIVE track, shown in gold on the panel chip so the current audio/subs are
    // legible without opening the menu. Built on the existing selected-row helpers.
    function langChip(row, offText) {
        if (!row)
            return offText
        var lang = String(row.lang || "").toUpperCase()
        return lang.length ? lang.substring(0, 3) : "ON"
    }
    readonly property string audioChipValue: langChip(root.selectedAudioRow(), "—")
    readonly property string subsChipValue: (String(mpv.subtitleTrack) === "")
                                            ? "OFF" : langChip(root.selectedSubtitleRow(), "ON")

    function applySavedTrackDelays(pref) {
        if (pref && typeof pref.audioDelay === "number")
            mpv.audioDelay = root.round2(pref.audioDelay)
        if (pref && typeof pref.subDelay === "number")
            mpv.subDelay = root.round2(pref.subDelay)
    }

    function resetTrackAutomation() {
        root.userTouchedAudio = false
        root.trackAutoDoneKey = ""
        root.autoAudioTrackId = ""
        root.autoSubtitleTrackId = ""
        root.autoTrackSignature = ""
    }

    // Conservative auto-selection: never override a manual pick, run once per show unless
    // upgrades are on, and re-run cheaply on async track-list/online-sub arrival (signature-gated).
    function maybeAutoSelectTracks(reason) {
        root.trackAutomationRev += 1
        if (!root.fileReady || root.trackAutomationExcluded())
            return

        var pref = root.currentTrackPreference()
        root.applySavedTrackDelays(pref)
        var signature = TrackLanguage.trackSignature(
                    root.audioRows, root.subRows, root.onlineSubs, root.trackPolicyKey())
        if (signature === root.autoTrackSignature && reason !== "manual-policy")
            return
        root.autoTrackSignature = signature

        if (!root.userTouchedAudio) {
            var audioPick = TrackLanguage.pickBestAudioTrack(
                        root.audioRows, root.effectiveAudioLanguages(pref), playerSettings.blockedTrackWords)
            if (audioPick && String(audioPick.id) !== String(mpv.audioTrack)) {
                root.autoAudioTrackId = String(audioPick.id)
                mpv.audioTrack = root.autoAudioTrackId
            }
        }

        var subtitlesOff = pref.subtitlesOff === true || (!pref.hasOwnProperty("subtitlesOff") && playerSettings.subtitlesOffByDefault)
        if (subtitlesOff && !root.userTouchedSubs) {
            root.autoSubtitleTrackId = ""
            root.trackAutoDoneKey = root.currentShowKey()
            mpv.subtitleTrack = ""
            return
        }

        if (root.userTouchedSubs)
            return
        if (!playerSettings.subtitleAutoUpgrade && root.trackAutoDoneKey === root.currentShowKey())
            return

        var subOptions = {
            "preferEmbeddedSubtitles": playerSettings.preferEmbeddedSubtitles,
            "blockedTrackWords": playerSettings.blockedTrackWords,
            "forcedOnly": false,
            "subtitleAutoUpgrade": playerSettings.subtitleAutoUpgrade
        }
        var subPick = TrackLanguage.pickBestSubtitleTrack(root.subRows, root.effectiveSubtitleLanguages(pref), subOptions)

        if (!subPick && playerSettings.forcedSubsWhenNativeAudio) {
            var audio = root.selectedAudioRow()
            var audioLang = TrackLanguage.normalizeLang(audio ? audio.lang : "")
            if (root.effectiveSubtitleLanguages(pref).indexOf(audioLang) >= 0) {
                subOptions.forcedOnly = true
                subPick = TrackLanguage.pickBestSubtitleTrack(root.subRows, root.effectiveSubtitleLanguages(pref), subOptions)
            }
        }

        if (!subPick)
            return
        root.autoSubtitleTrackId = String(subPick.id)
        root.trackAutoDoneKey = root.currentShowKey()
        root.pickSubtitle(root.autoSubtitleTrackId)
    }

    function pickAudioTrack(trackId) {
        root.userTouchedAudio = true
        mpv.audioTrack = trackId
        var row = root.selectedAudioRow()
        if (!row || String(row.id) !== String(trackId)) {
            for (var i = 0; i < root.audioRows.length; i++)
                if (String(root.audioRows[i].id) === String(trackId)) row = root.audioRows[i]
        }
        root.saveTrackPreference({
            "audioLang": TrackLanguage.normalizeLang(row ? row.lang : ""),
            "audioTrackTitle": row ? (row.title || row.label || "") : "",
            "audioDelay": mpv.audioDelay
        })
    }

    function pickSubtitleTrack(trackId) {
        root.userTouchedSubs = true
        var row = null
        for (var i = 0; i < root.subRows.length; i++)
            if (String(root.subRows[i].id) === String(trackId)) row = root.subRows[i]
        root.pickSubtitle(trackId)
        root.saveTrackPreference({
            "subtitleLang": TrackLanguage.normalizeLang(row ? row.lang : ""),
            "subtitleTrackTitle": row ? (row.title || row.label || "") : "",
            "subtitlesOff": false,
            "subDelay": mpv.subDelay
        })
    }

    function turnSubtitlesOff() {
        root.userTouchedSubs = true
        mpv.subtitleTrack = ""
        root.saveTrackPreference({ "subtitlesOff": true, "subDelay": mpv.subDelay })
    }

    function adjustAudioDelay(delta) {
        mpv.audioDelay = root.round2(mpv.audioDelay + delta)
        root.saveTrackPreference({ "audioDelay": mpv.audioDelay })
    }

    function adjustSubtitleDelay(delta) {
        mpv.subDelay = root.round2(mpv.subDelay + delta)
        root.saveTrackPreference({ "subDelay": mpv.subDelay })
    }

    function resetSubtitleDelay() {
        mpv.subDelay = 0
        root.saveTrackPreference({ "subDelay": 0 })
    }

    function subtitleAutoStatusText() {
        if (root.trackAutomationExcluded())
            return "Auto off for live playback"
        var pref = root.currentTrackPreference()
        if (pref.subtitlesOff === true)
            return "Auto: subtitles off for this show"
        if (root.userTouchedSubs)
            return "Auto paused after manual subtitle choice"
        var langs = root.effectiveSubtitleLanguages(pref)
        return "Auto: " + (langs.length ? langs[0].toUpperCase() : "ENG") + " subtitles"
    }
    function loadingStatusText() {
        if (root.errored)
            return root.statusMsg
        if (!root.starting)
            return ""
        if (root.statusMsg.length > 0 && root.statusMsg !== "Buffering...")
            return root.statusMsg
        if (root.streamStatsSeen && root.mediaLocalPath.length === 0)
            return root.streamGatherStatusText()
        return root.mediaLocalPath.length > 0 ? "Opening..." : "Starting stream..."
    }
    // The Popcorn Time ladder (streamer.js watchState): connecting → startingDownload →
    // downloading, percent measured against the 10 MB head buffer PT gates playback on.
    // Capped at 99 — 100 would claim done while mpv is still probing the container; the
    // first real frame retires the face (finishStartingIfPlaybackAdvanced), same as today.
    function streamGatherStatusText() {
        if (root.streamStatsDownloaded > 0) {
            var pct = Math.min(99, Math.floor(root.streamStatsDownloaded / root.streamHeadTargetBytes * 100))
            return "Buffering " + pct + "% · " + root.formatStreamSpeed(root.streamStatsSpeedBps)
                    + " · " + root.formatPeerCount(root.streamStatsPeers)
        }
        if (root.streamStatsPeers > 0)
            return "Connected · " + root.formatPeerCount(root.streamStatsPeers)
        return "Connecting to peers..."
    }
    function formatStreamSpeed(bps) {
        if (bps >= 1024 * 1024)
            return (bps / (1024 * 1024)).toFixed(1) + " MB/s"
        return Math.max(0, Math.round(bps / 1024)) + " kB/s"
    }
    function formatPeerCount(n) {
        return n + (n === 1 ? " peer" : " peers")
    }
    function finishStartingIfPlaybackAdvanced() {
        if (!root.starting || root.errored || mpv.pause)
            return
        if (Number(mpv.position || 0) <= 0.25)
            return
        root.starting = false
        root.statusMsg = ""
        root.fileReady = true
        streamWatchdog.stop()
        root.wakeChrome()
        root.syncPowerInhibit()
        // Activity (§9 Lane A): "a new real movie/episode identity becomes the active playing
        // file and playback can begin" — this is exactly that moment (fires once per genuine
        // starting->false transition). A same-identity reload/recovery is a no-op here.
        root.activityBeginIfNeeded()
    }
    property bool starting: false
    property bool errored: false
    property string statusMsg: ""
    property bool controlsShown: true
    property bool seeking: false
    property real seekPreview: mpv.position
    property real seekTargetSec: -1
    readonly property bool seekSettling: seekTargetSec >= 0 && mpv.coreSeeking
    // What the bar/dot/time should show right now: drag preview beats in-flight target beats truth.
    function displayPosition() {
        if (root.seeking) return root.seekPreview
        if (root.seekSettling) return root.seekTargetSec
        return mpv.position
    }
    property real seekBackSeconds: playerSettings.seekStepSeconds
    property real seekForwardSeconds: playerSettings.seekStepSeconds
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

    // Width-honest control bar (2026-07-08, "icon vomit"): the centered transport row and
    // the side clusters share one strip, and nothing used to check whether they FIT —
    // every utility added since the magic 1000/680 thresholds were tuned (stream,
    // download, episodes browser) slid the cluster further under the transport.
    // utilitySpace = the real room between the transport's edge and the bar's margin,
    // from LIVE transport width (prev/next buttons change it). The panel is symmetric,
    // so ONE number governs BOTH sides: left = volume + retry/stream/download (~330px
    // max), right = the chip cluster + overflow. Fold tiers:
    //   barSnug — the chip roster doesn't fit: fold speed/fill (as before) AND
    //             stream/download/browser into the overflow panel (left side shrinks
    //             to volume alone, ~170px — always under the snug threshold).
    //   barTiny — even the snug roster doesn't fit: fold audio/tools too.
    // Reads transportRow.width only (independent of any folding) — no binding loop.
    readonly property real utilitySpace: chromeVisibleWidth / 2 - transportRow.width / 2 - 34
    // Retuned for the chip cluster (native chrome): stream/download round buttons fold
    // first (snug); the four chips hold out until the window is genuinely tiny.
    // Retuned AGAIN 2026-07-12: barSnug's only live consumers ARE those left round
    // buttons, and the old 470 was sized for the chip roster — ~120px oversized for a
    // left row that needs 298px steady (volume 190 + stream 48 + download 48 + spacing),
    // 352 with the transient retry button. Context hydration lighting up prev/next on
    // the Continue-Watching door widened the transport enough to cross 470 on a
    // fullscreen 150%-DPI window and hid change-stream + download for no layout reason.
    readonly property bool barSnug: utilitySpace < 360
    readonly property bool barTiny: utilitySpace < 260
    readonly property bool anyMenuOpen: watchPartyMenu.panelOpen || audioMenu.panelOpen || subMenu.panelOpen || speedMenu.panelOpen || fillMenu.panelOpen || subStyleBar.open || root.liveGuideOpen || root.dvrPanelOpen || root.overflowOpen || root.closeConfirmOpen || root.browserOpen
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
    signal fullscreenRequested()
    signal closeRequested()

    function directStreamUrl(candidate) {
        var c = candidate || ({})
        if (c.url && String(c.url).length)
            return String(c.url)
        var routed = String(c.infoHash || "")
        return routed.indexOf("url:") === 0 ? routed.substring(4) : ""
    }

    function loadDirectStreamUrl(url, headers) {
        var directUrl = String(url || "")
        if (!directUrl.length)
            return false
        var requestHeaders = (headers && typeof headers === "object" && !Array.isArray(headers))
                           ? headers : ({})
        root.currentPlaybackUrl = directUrl
        if (Object.keys(requestHeaders).length)
            mpv.loadFileWithHeaders(directUrl, requestHeaders)
        else
            mpv.loadFile(directUrl)
        return true
    }

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
                // Preserve the source owner's stable identity separately from its display name.
                // Watch Party may inspect this provenance locally; transportUrl never enters the row.
                "addonId": c.addonId || "",
                "addonName": c.addonName || "",
                "streamKind": c.streamKind || "",
                "url": c.url || "",
                // HTTP hosts that gate on a Referer/Origin ride their required headers this far;
                // the play path installs them via mpv.loadFileWithHeaders. Must survive this
                // reshape or the header channel is dead in the app. (House HTTP, slice 1.)
                "headers": (c.headers && typeof c.headers === "object" && !Array.isArray(c.headers)) ? c.headers : ({})
            })
        }
        if (!out.length && infoHash && String(infoHash).length) {
            out.push({
                "infoHash": String(infoHash),
                "fileIdx": fileIdx || 0,
                "title": title || "Stream",
                "quality": "",
                "seeders": -1,
                "sourceName": "Torrentio",
                "addonId": "",
                "addonName": "",
                "streamKind": String(infoHash).indexOf("url:") === 0 ? "Direct" : "Torrent",
                "url": "",
                "headers": ({})
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

    function watchPartyDescriptorsMatch(localDescriptor, roomDescriptor) {
        var local = localDescriptor || ({})
        var room = roomDescriptor || ({})
        if (!local.kind || String(local.kind) !== String(room.kind || ""))
            return false
        if (String(local.kind) === "torrent")
            return String(local.infoHash || "").toLowerCase() === String(room.infoHash || "").toLowerCase()
                    && Number(local.fileIdx || 0) === Number(room.fileIdx || 0)
        if (String(local.kind) === "debrid")
            return String(local.providerId || "") === String(room.providerId || "")
                    && String(local.providerSourceId || "") === String(room.providerSourceId || "")
        return false
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
                out.context = { "year": ctx.year || "", "episodeQueue": queue, "episodeIndex": index }
                return out
            }
            return {
                "prev": idx > 0 ? withQueue(queue[idx - 1], idx - 1) : null,
                "next": (idx >= 0 && idx + 1 < queue.length) ? withQueue(queue[idx + 1], idx + 1) : null
            }
        }
        return ctx.adjacentEpisodes || ({})
    }

    // Bare-door context hydration (2026-07-12): Continue-Watching resume and downloaded
    // files open the player knowing only "this file, this position" — no season queue,
    // no source list — so prev/next-episode and change-stream buttons vanished on those
    // doors while the series-page door showed them. Refetch both in the background from
    // the identity the player always has and light the buttons as data lands. Silent on
    // failure: buttons simply stay hidden, never a guessed queue.
    property int hydrateGen: 0

    function maybeHydrateContext() {
        root.hydrateGen += 1
        var myGen = root.hydrateGen
        // Episode neighbors — only when the door passed none.
        if (EpisodeBrowser.isEpisodeId(root.mediaId)
                && !root.hasAdjacentEpisode("prev") && !root.hasAdjacentEpisode("next")) {
            var requestedRoot = EpisodeBrowser.seriesRootId(root.mediaId)
            TheatreApi.loadMeta("series", requestedRoot, function(meta) {
                var vids = (meta && meta.videos) ? meta.videos : []
                // Canonical absolute order first (native service), same-season meta second.
                var identities = { "resolvedId": requestedRoot, "imdbIds": [requestedRoot] }
                var order = (typeof AnimeOrder !== "undefined")
                          ? AnimeOrder.resolve(identities, vids) : ({})
                var ctx = EpisodeBrowser.queueContextFromOrder(
                            order, root.mediaId,
                            EpisodeBrowser.showTitleFrom(root.mediaTitle),
                            root.mediaArt, root.mediaYear)
                if (!ctx)
                    ctx = EpisodeBrowser.queueContextFromMeta(
                            vids, root.mediaId,
                            EpisodeBrowser.showTitleFrom(root.mediaTitle),
                            root.mediaArt, root.mediaYear)
                // Reject a stale generation or a callback for another title.
                if (myGen !== root.hydrateGen
                        || requestedRoot !== EpisodeBrowser.seriesRootId(root.mediaId))
                    return
                if (!ctx)
                    return
                root.playbackQueue = ctx.episodeQueue
                root.playbackQueueIndex = ctx.episodeIndex
                root.adjacentEpisodes = root.resolveAdjacentContext(ctx)
                root.playbackQueueOrderingMode =
                    (order && order.absoluteComplete === true) ? "absolute" : "seasons"
            })
        }
        // Source list — streamed sessions that arrived with just the one saved torrent.
        // Downloaded files keep their empty list: change-stream isn't a local-file verb.
        if (root.mediaLocalPath.length || root.streamCandidates.length > 1)
            return
        var subType = root.subStreamType.length ? root.subStreamType : "movie"
        var subId = root.subStreamId
        if (!subId.length || subId.indexOf("iptv:") === 0)
            return
        var applyRows = function(rows) {
            if (myGen !== root.hydrateGen)
                return
            var merged = EpisodeBrowser.mergeHydratedCandidates(
                        root.currentStreamCandidate(),
                        root.normalizeStreamCandidates("", 0, root.mediaTitle, rows))
            if (!merged)
                return
            root.streamCandidates = merged.list
            root.currentStreamIndex = merged.index
            root.updateMediaSubtitle()
        }
        // Same source ladder as the picker and the episode jump: every installed
        // extension, ranked the same way, Torrentio as the fallback — but only while
        // it is still installed AND enabled, so Remove and the off-switch mean
        // something. Removed and nothing else answering: leave the candidate list
        // exactly as it is (this pass only ENRICHES the picker's rows; it is not the
        // path that starts playback). (2026-07-25, A5 — Torrentio-honesty fix.)
        var installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        var exts = AddonClient.streamExtensions(installed, subType, subId)
        var lastResort = function() {
            if (AddonClient.torrentioEnabled(installed))
                Torrentio.loadStreams(subType, subId, applyRows)
        }
        if (exts.length) {
            AddonClient.loadStreams(exts, subType, subId, function() {}, function(rows) {
                if (myGen !== root.hydrateGen)
                    return
                if (rows && rows.length) applyRows(rows)
                else lastResort()
            })
        } else {
            lastResort()
        }
    }

    // A later service revision can upgrade a bare-door queue to the canonical
    // absolute order. Retry only while the current media is still an episode and
    // the queue isn't already absolute; bump hydrateGen first so an earlier
    // metadata callback cannot land afterward. Never downgrades: if the mapping
    // still isn't complete, queueContextFromOrder returns null and the existing
    // queue stands.
    function retryAnimeOrderHydration() {
        if (!EpisodeBrowser.isEpisodeId(root.mediaId))
            return
        // Only upgrade a bare-door queue: an empty one, or one hydrated from the
        // same-season meta fallback (mode "seasons"). A queue a door supplied
        // (mode "") that the user deliberately opened is never silently swapped.
        if (root.playbackQueue.length > 0 && root.playbackQueueOrderingMode !== "seasons")
            return
        root.hydrateGen += 1
        var myGen = root.hydrateGen
        var requestedRoot = EpisodeBrowser.seriesRootId(root.mediaId)
        TheatreApi.loadMeta("series", requestedRoot, function(meta) {
            var vids = (meta && meta.videos) ? meta.videos : []
            var identities = { "resolvedId": requestedRoot, "imdbIds": [requestedRoot] }
            var order = (typeof AnimeOrder !== "undefined")
                      ? AnimeOrder.resolve(identities, vids) : ({})
            var ctx = EpisodeBrowser.queueContextFromOrder(
                        order, root.mediaId,
                        EpisodeBrowser.showTitleFrom(root.mediaTitle),
                        root.mediaArt, root.mediaYear)
            if (!ctx)
                return
            if (myGen !== root.hydrateGen
                    || requestedRoot !== EpisodeBrowser.seriesRootId(root.mediaId))
                return
            root.playbackQueue = ctx.episodeQueue
            root.playbackQueueIndex = ctx.episodeIndex
            root.adjacentEpisodes = root.resolveAdjacentContext(ctx)
            root.playbackQueueOrderingMode = "absolute"
        })
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
        // Mid-play stream switch: carry the live position into the replacement stream.
        // No overlay mid-watch — you were just watching, so the seek is silent.
        if (reason === "switch" && mpv.position > 0) {
            root.pendingSeekSec = mpv.position
            root.resumePromptConsumed = true   // silent carry — skip the resume overlay
        }
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = reason === "switch" ? "Switching stream..."
                       : reason === "retry" ? "Retrying stream..."
                       : "Starting stream..."
        root.resetRecoveryWatch()
        streamWatchdog.restart()
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        // Direct-url streams (debrid / HTTP hosts / live-tv extensions) skip the
        // torrent engine — mpv plays the url natively. They arrive either as an
        // explicit url field or under the "url:" infoHash routing prefix (the
        // resume path carries only the hash). Extensions spec Phase 2, slice G.
        var directUrl = root.directStreamUrl(c)
        if (directUrl.length) {
            root.mediaTransport = "Direct stream"
            root.updateMediaSubtitle()
            // HTTP hosts that gate on a Referer/Origin need those headers on every request,
            // including retries/reconnects. Header-free rows take the plain path, which also
            // clears any prior mpv http-header-fields.
            root.loadDirectStreamUrl(directUrl, c.headers)
            return
        }
        root.mediaTransport = "Torrent stream"
        root.updateMediaSubtitle()
        Stream.play(c.infoHash, c.fileIdx || 0)
    }

    function playTorrent(infoHash, fileIdx, title, posterUrl, subType, subId, streamCandidates, playbackContext) {
        root.arrivingStreamUrl = ""
        root.arrivingStreamHeaders = ({})
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.resetSkipSegments()
        root.resetTrackAutomation()
        root.mediaLocalPath = ""
        root.pendingSeekSec = -1
        root.resumeChoiceOpen = false
        root.resumeChoiceSec = -1
        root.resumePromptConsumed = false
        root.mediaTitle = title || ""
        root.mediaTransport = "Torrent stream"
        root.mediaYear = String((playbackContext || ({})).year || "")
        root.mediaArt = posterUrl || ""
        // Stable id: series episodes use the exact stream id (tt:season:episode) so the
        // detail page can paint per-episode progress; movies keep the base Cinemeta id.
        var m = String(posterUrl || "").match(/\/(tt\d+)\//)
        root.mediaId = (subType === "series" && subId) ? subId
                     : ((m && m[1]) ? m[1] : (infoHash + ":" + fileIdx))
        // Cross-stream resume (spec 2026-07-08): the store keys progress by IDENTITY
        // (tt / tt:season:episode), so a position saved while watching one torrent is
        // valid for every other torrent of the same episode. Seed it here; onFileLoaded's
        // resume-choice overlay (Feature 3) decides ask / seek / start-over. The
        // Continue-tile path still calls restoreState() after open — it just overwrites
        // this seed with the same-or-fresher value, so the two doors never fight.
        var roomJoin = !!((playbackContext || ({})).watchPartyJoin)
        var prog = (!roomJoin && typeof Progress !== "undefined") ? Progress.get("video", root.mediaId) : ({})
        var savedPos = Number(((prog || ({})).resume || ({})).position || 0)
        if (savedPos > 0)
            root.pendingSeekSec = savedPos
        root.playbackQueue = (playbackContext || ({})).episodeQueue || []
        root.playbackQueueIndex = (playbackContext || ({})).episodeIndex !== undefined
                                  ? Number((playbackContext || ({})).episodeIndex) : -1
        // A door-provided queue's ordering is unknown; leave it upgradeable so a
        // ready AnimeOrder generation can lift it to absolute.
        root.playbackQueueOrderingMode = ""
        root.deadStreamKeys = ({})
        root.stubCheckedKey = ""
        root.autoPausedInactive = false
        root.cancelUpNext()
        root.streamCandidates = root.normalizeStreamCandidates(infoHash, fileIdx, title, streamCandidates)
        root.currentStreamIndex = root.findStreamIndex(infoHash, fileIdx)
        root.adjacentEpisodes = root.resolveAdjacentContext(playbackContext)
        root.updateMediaSubtitle()
        // Per-show loader identity (Task 4): logo + episode still/line feed PlayerLoadingScreen.
        // The producer (TheatreSeries) adds logo/episodeStill/loaderBackdrop/episodeLine to the
        // context; local files and metadata-less contexts fall back to poster/subtitle here.
        // Logo: prefer the context's, else DERIVE the metahub logo from the imdb id already present
        // in the art URL. Many open paths (universe Watch, continue, etc.) pass an EMPTY context, so
        // deriving here is what makes the Stremio logo appear everywhere, not only from the series page.
        var _ttMatch = String(posterUrl || "").match(/\/(tt\d+)\//)
        var _ttId = _ttMatch ? _ttMatch[1] : ""
        root.mediaLogo        = (playbackContext || ({})).logo
            || (_ttId ? "https://live.metahub.space/logo/medium/" + _ttId + "/img" : "")
        root.mediaLoadingArt  = (playbackContext || ({})).episodeStill || (playbackContext || ({})).loaderBackdrop || posterUrl || ""
        // Loader subline = the CLEAN episode line only (never the torrent source/quality string).
        root.mediaLoadingLine = (playbackContext || ({})).episodeLine || ""
        // Online subtitles for this exact title/episode (Harbor-style).
        root.subStreamType = subType || ""
        root.subStreamId = subId || ""
        root.fetchSubtitles()
        root.playStreamAt(root.currentStreamIndex, "start")
        root.maybeHydrateContext()
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
        // Reset recovery clocks: without this the retry inherits the previous stream's stale
        // frozen/no-start timers and gets killed within ~1 tick. [review fix 2026-07-07]
        root.resetRecoveryWatch()
        streamWatchdog.restart()
        root.wakeChrome()
        var directUrl = root.directStreamUrl(c)
        if (directUrl.length) {
            root.loadDirectStreamUrl(directUrl, c.headers)
            return
        }
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

    // --- recovery watch + wake reconnect (Feature 3) ---
    // Local downloaded files and live streams are excluded from automatic source switching
    // and reconnection: locals fail honestly, live has no seekable resume point.
    function recoveryExcluded() {
        if (root.mediaLocalPath.length)
            return true
        if (root.subStreamId.indexOf("iptv:") === 0 || root.mediaId.indexOf("iptv:") === 0)
            return true
        return false
    }

    function resetRecoveryWatch() {
        var now = Date.now()
        root.recoveryLastPosition = 0
        root.recoveryLastMovedAt = now
        root.recoveryUrlStartedAt = now
        root.recoverySawVideo = false
        root.recoveryNoVideoSince = 0
        root.wakeReconnectLastTickAt = now
        root.wakeReconnectPendingSeek = -1
    }

    function handlePlaybackIssue(code, message) {
        if (root.arrivingStreamUrl !== "") {
            // The .part would not open or died mid-read (e.g. an MP4 whose index sits at
            // the file's end — a prefix of it can't probe). The stream url is the honest
            // fallback and was the pre-2026-07-31 behaviour for arriving plays.
            root.switchArrivingToStream()
            return
        }
        root.lastPlaybackErrorCode = code || "unknown"
        root.lastPlaybackErrorMessage = message || ""
        if (root.lastPlaybackErrorCode === "network")
            root.handlePlaybackFailure("network")
        else if (root.lastPlaybackErrorCode === "decode" || root.lastPlaybackErrorCode === "codec")
            root.handlePlaybackFailure(root.lastPlaybackErrorCode)
        else
            root.handlePlaybackFailure(root.lastPlaybackErrorMessage || root.lastPlaybackErrorCode)
    }

    function tickRecoveryWatch() {
        if (root.recoveryExcluded())
            return
        if (root.starting || root.errored || mpv.pause || !root.fileReady)
            return

        var now = Date.now()
        var pos = Number(mpv.position || 0)
        if (pos > root.recoveryLastPosition + root.positionAdvanceEpsilonSec) {
            root.recoveryLastPosition = pos
            root.recoveryLastMovedAt = now
        }

        // Recovery must use decoded-frame truth, not container/header dimensions.
        // `width`/`height` can be known before a frame renders; dwidth/dheight are
        // already cached by MpvItem as decodedWidth/decodedHeight for this purpose.
        var width = Number(mpv.decodedWidth || 0)
        var height = Number(mpv.decodedHeight || 0)
        if (width > 0 && height > 0) {
            root.recoverySawVideo = true
            root.recoveryNoVideoSince = 0
        } else if (!root.recoverySawVideo) {
            if (root.recoveryNoVideoSince <= 0)
                root.recoveryNoVideoSince = now
            if (now - root.recoveryNoVideoSince > root.noVideoGraceSeconds * 1000) {
                root.handlePlaybackFailure("no video")
                return
            }
        }

        if (pos < 0.5 && now - root.recoveryUrlStartedAt > root.streamWatchdogSeconds * 1000) {
            root.handlePlaybackFailure("source did not start")
            return
        }

        if (pos < root.positionStartedFloorSec &&
                now - root.recoveryLastMovedAt > root.positionFrozenSeconds * 1000) {
            root.handlePlaybackFailure("position frozen")
        }
    }

    function tickWakeReconnect() {
        if (root.recoveryExcluded())
            return
        if (!root.currentPlaybackUrl.length || root.errored)
            return
        var now = Date.now()
        if (root.wakeReconnectLastTickAt <= 0) {
            root.wakeReconnectLastTickAt = now
            return
        }
        var gap = now - root.wakeReconnectLastTickAt
        root.wakeReconnectLastTickAt = now
        // Don't auto-reconnect (and auto-play) a stream the user parked/minimized. A paused
        // player still ticks, so pause alone can't fake a gap; this only guards the
        // paused-through-real-sleep case. [review fix 2026-07-07]
        if (mpv.pause)
            return
        if (gap < root.wakeReconnectGapSeconds * 1000)
            return
        if (mpv.duration <= 0 && mpv.position <= 0)
            return
        var pos = mpv.position > 1 ? mpv.position : -1
        // Fresh recovery clocks for the reconnected load (this path bypasses playStreamAt).
        root.resetRecoveryWatch()
        root.wakeReconnectPendingSeek = pos
        root.statusMsg = "Reconnecting stream..."
        streamWatchdog.restart()
        var c = root.currentStreamCandidate()
        var directUrl = root.directStreamUrl(c)
        if (directUrl.length)
            root.loadDirectStreamUrl(directUrl, c.headers)
        else
            mpv.loadFile(root.currentPlaybackUrl)
    }

    function handlePlaybackFailure(reason) {
        streamWatchdog.stop()
        root.recordProgress()
        if (root.mediaLocalPath.length) {
            // A local file has no retry ladder or alternate candidates — fail honestly.
            root.errored = true
            root.starting = false
            root.statusMsg = "Couldn't play this file. It may have been moved or deleted."
            root.wakeChrome()
            return
        }
        if (!root.streamCandidates.length && !root.mediaResumeHash.length
                && root.currentPlaybackUrl.length) {
            // Direct-url (Arriving) session: no candidate ladder to walk —
            // retryCurrentStream() would early-return forever. One reconnect
            // against the same url, then fail honestly; the download itself
            // is a separate connection and keeps going either way.
            if (root.streamRetryCount < 1) {
                root.streamRetryCount += 1
                root.errored = false
                root.starting = true
                root.fileReady = false
                root.statusMsg = "Reconnecting stream..."
                root.resetRecoveryWatch()
                streamWatchdog.restart()
                mpv.loadFile(root.currentPlaybackUrl)
                return
            }
            root.errored = true
            root.starting = false
            root.statusMsg = "The stream dropped. The download keeps going — play it from Downloads once it lands."
            root.wakeChrome()
            return
        }
        if (root.streamRetryCount < 1) {
            root.retryCurrentStream()
            return
        }
        // The current candidate has now failed twice. Retire it before walking the
        // ladder, otherwise A -> B -> C eventually wraps back to A and can loop forever.
        if (root.currentStreamIndex >= 0 && root.currentStreamIndex < root.streamCandidates.length)
            root.markStreamDead(root.currentStreamIndex, reason || "playback_failed")
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
        // FILE_LOADED is not playback proof. `starting` retires only after position
        // genuinely advances, so a loaded-but-frozen/black source must still time out.
        if (!root.starting)
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
        root.jumpToEpisode(ep, which === "next" ? "Next episode..." : "Previous episode...",
                           which === "next" ? "next episode." : "previous episode.")
    }

    function downloadedEpisodeTarget(ep) {
        if (!ep || !ep.id || typeof Download === "undefined")
            return null
        var rows = Download.downloadedVideos() || []
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i] || ({})
            if (String(row.id || "") !== String(ep.id) || row.missing === true
                    || !row.path || !String(row.path).length)
                continue
            return {
                "localPath": String(row.path),
                "id": String(ep.id),
                "title": ep.title || row.title || "Episode",
                "art": ep.backdrop || row.art || root.mediaArt || "",
                "year": (ep.context || ({})).year || root.mediaYear || "",
                "kind": "episode",
                "position": 0,
                "playbackContext": ep.context || ({})
            }
        }
        return null
    }

    // Feature 8: the Next-Episode pipeline, generalized to ANY episode target
    // ({id, title, type, backdrop, context}) — the drawer's episode taps land here.
    function jumpToEpisode(ep, startLabel, failLabel) {
        if (!ep || !ep.id)
            return
        root.cancelUpNext()
        root.adjacentResolveGen += 1
        var myGen = root.adjacentResolveGen
        root.recordProgress()
        var localTarget = root.downloadedEpisodeTarget(ep)
        if (localTarget) {
            root.playLocalFile(localTarget)
            return
        }
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = startLabel || "Loading episode..."
        streamWatchdog.restart()
        root.wakeChrome()
        var startWith = function(list) {
            if (myGen !== root.adjacentResolveGen)
                return
            if (!list || !list.length) {
                streamWatchdog.stop()
                root.errored = true
                root.starting = false
                root.statusMsg = "No stream found for " + (failLabel || "this episode.")
                root.wakeChrome()
                return
            }
            // Torrent continuity (spec 2026-07-11): if the torrent we're already
            // playing also carries the target episode (season pack), stay on it —
            // near-instant start, same quality. Otherwise rank-best, as ever.
            var cur = root.currentStreamCandidate()
            var first = EpisodeBrowser.pickContinuityRow(list, cur.infoHash || "", cur.fileIdx || 0)
            root.playTorrent(first.infoHash, first.fileIdx || 0,
                             ep.title || root.mediaTitle, ep.backdrop || root.mediaArt,
                             ep.type || "series", ep.id, list, ep.context || ({}))
        }
        // Same pipeline as the main picker: every installed extension, ranked the same
        // way. Torrentio stays as the fallback while it is installed AND enabled —
        // never worse than before. Removed and nothing else answering: startWith([])
        // is already the honest landing ("No stream found for …"), not a dead spinner.
        // (2026-07-25, A5 — Torrentio-honesty fix.)
        var installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        var exts = AddonClient.streamExtensions(installed, ep.type || "series", ep.id)
        var lastResort = function() {
            if (!AddonClient.torrentioEnabled(installed)) {
                startWith([])
                return
            }
            streamWatchdog.restart()   // fallback gets the same full budget the old direct path had
            Torrentio.loadStreams(ep.type || "series", ep.id, startWith)
        }
        if (exts.length) {
            AddonClient.loadStreams(exts, ep.type || "series", ep.id, function() {}, function(rows) {
                if (myGen !== root.adjacentResolveGen)
                    return
                if (rows && rows.length) startWith(rows)
                else lastResort()
            })
        } else {
            lastResort()
        }
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









    function currentCastUrl() {
        if (root.currentPlaybackUrl.length)
            return root.currentPlaybackUrl
        if (typeof Stream !== "undefined" && root.mediaResumeHash.length)
            return Stream.streamUrl(root.mediaResumeHash, root.mediaResumeFileIdx)
        return ""
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
            root.seekTo(Math.max(0, mpv.duration - 1))
        root.wakeChrome()
    }

    function handleWindowMinimize() {
        // Don't fight PiP / casting / room-sync — those are meant to keep playing
        // in the background even when the main window is minimized.
        if (root.pipMode)
            return
        if (root.windowMinimized) {
            if (!mpv.pause && root.fileReady && !root.starting && !root.errored) {
                root.autoPausedInactive = true
                mpv.pause = true
            }
        } else {
            root.healAudio()   // heal even if the pause was his, not ours
            if (root.autoPausedInactive) {
                root.autoPausedInactive = false
                if (mpv.pause)
                    mpv.pause = false
            }
        }
    }

    // Windows can kill our idle audio stream while parked (see audio-stream-silence in
    // mpvitem.cpp); if that still slips through, resuming plays video with NO sound and no
    // way back short of an app restart. Rebuilding the audio chain (drop track, re-pick the
    // same one) forces a fresh device connection. Cheap (~100ms, same track, paused), so run
    // it on every un-minimize rather than trying to detect a dead device mpv won't report.
    function healAudio() {
        if (!root.fileReady)
            return
        var aid = mpv.audioTrack
        if (!aid || aid === "no")
            return
        mpv.command(["set", "aid", "no"])
        mpv.command(["set", "aid", aid])
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
        var source = root.currentStreamCandidate()
        var sourceHeaders = (source.headers && typeof source.headers === "object" && !Array.isArray(source.headers))
                          ? source.headers : ({})
        Download.startDownload({
            "url": url,
            "headers": sourceHeaders,
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
    // Encode runs off the UI thread now: this only kicks it off. The "encoding" pill
    // stays up until mpv's gifSaved/gifFailed signal lands (handlers on the MpvItem).
    function stopGifRecording() {
        if (root.gifState !== "recording")
            return
        gifElapsedTimer.stop()
        root.gifState = "encoding"
        root.wakeChrome()
        mpv.stopGifRecording(root.mediaTitle || mpv.mediaTitle || "Video",
                             root.mediaSubtitle || root.fmtTime(mpv.position))
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
    // Pass silent=true (the 5s playback tick) to persist for crash-resume WITHOUT emitting
    // changed() — re-rendering the Continue row every 5s was the proven stutter source
    // (2026-07-29 A/B: +100 -> +3 output drops/60s). No-arg calls (the lifecycle sites: stop,
    // stream-death, playback-failure, episode-advance, EOF) still notify via record(), so the
    // row refreshes when you actually leave the player or move on. NOTE: seek and pause do NOT
    // call recordProgress at all (pre-existing, intentional — scrubbing fires often, and a
    // notify-per-seek would re-introduce a smaller cascade); their position is picked up by the
    // next 5s tick or the next lifecycle write.
    function recordProgress(silent) {
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
        var entry = {
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
                        "localPath": root.mediaLocalPath,
                        "subType": root.subStreamType,
                        "subId": root.subStreamId,
                        "position": mpv.position }
        }
        if (silent) Progress.recordSilent(entry)
        else Progress.record(entry)
    }

    // --- Your Colosseum activity (Lane A) ------------------------------------------------
    // A dedicated ActivityPlaybackTracker sampler beside recordProgress()/Progress, never
    // inside it (CPP-PORT-CONTRACT.md §9 Lane A: "Do not: count calls to recordProgress").
    // sink is guarded null so a missing/unbound ActivityStore can never break playback (§25).
    //
    // Identity derivation (§7) and the begin/no-op/end state-transition rule (§9 Lane A
    // "item/episode identity change") both live in the shared ActivityLaneHelpers.js module —
    // also used by AudiobookSession.qml's Lane E and covered directly by
    // tests/qml/tst_player1_activity.qml, so the QuickTest exercises the exact code this hook
    // runs, not a parallel reimplementation.
    // Begin session: called once playback genuinely begins on a file (see
    // finishStartingIfPlaybackAdvanced — "a new real movie/episode identity becomes the
    // active playing file and playback can begin", §9 Lane A). A reload/recovery/stream-
    // replacement of the SAME item (mediaId unchanged) is a no-op here — activityDiscontinuity()
    // at the load/reload hook resets its baseline instead, preserving gate progress already
    // earned. A genuine identity change ends the old session (tracker.begin() also does this
    // itself if one is still open — belt and suspenders) and starts a fresh one.
    function activityBeginIfNeeded() {
        var idf = ActivityLaneHelpers.videoIdentityFor(root.mediaId, EpisodeBrowser)
        var action = ActivityLaneHelpers.decideTransition(root.activityActiveKey, idf)
        if (action === "noop")
            return
        root.activityEndSession()
        if (action === "end")
            return
        root.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
        var sink = (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
        var sessionId = (sink && sink.newSessionId) ? sink.newSessionId() : ""
        activityTracker.begin({
            "world": "theatre",
            "kind": idf.kind,
            "titleKey": idf.titleKey,
            "itemKey": idf.itemKey,
            "title": root.mediaTitle,
            "itemLabel": idf.kind === "episode" ? root.mediaSubtitle : "",
            "cover": root.mediaArt || "",
            "syncable": true,
            "source": "player1"
        }, sessionId)
    }
    // Sampling source: the existing five-second playing timer below, in addition to (never
    // instead of) its Continue write — changing that timer's cadence never changes the
    // tracker's measured wall/media-delta total (§9 Lane A).
    function activitySample() {
        if (!root.activityActiveKey.length)
            return
        var rateMilli = (mpv.speed && mpv.speed > 0) ? Math.round(mpv.speed * 1000) : 1000
        var consuming = !mpv.pause && root.fileReady && !root.starting && !root.errored
        activityTracker.sample(Math.round(mpv.position * 1000), Math.round(mpv.duration * 1000),
                                rateMilli, consuming)
    }
    // Discontinuity: central seekTo()/seekStep(), pause/resume, media load/reload, stream
    // replacement, recovery/reseek, speed change, error — every §9 Lane A reset bullet except
    // item/episode change (activityBeginIfNeeded's own end+begin already covers that).
    // `atSec`, when given, is the authoritative post-jump position (e.g. a seek target that
    // mpv.position has not caught up to yet); otherwise the current mpv.position is used.
    function activityDiscontinuity(atSec) {
        if (!root.activityActiveKey.length)
            return
        var posSec = (atSec !== undefined) ? atSec : mpv.position
        var rateMilli = (mpv.speed && mpv.speed > 0) ? Math.round(mpv.speed * 1000) : 1000
        activityTracker.discontinuity(Math.round(posSec * 1000), Math.round(mpv.duration * 1000), rateMilli)
    }
    // Natural EOF: the real end of the item (not the arriving-.part handover, which is a
    // discontinuity — same item, playback continues via switchArrivingToStream). Ends the
    // session too (tracker header: "call endSession() afterward for lifecycle symmetry with
    // begin()") so a later replay begins a fresh session rather than silently reusing this one.
    function activityNaturalEof() {
        if (!root.activityActiveKey.length)
            return
        activityTracker.naturalEof()
        activityTracker.endSession()
        root.activityActiveKey = ""
    }
    function activityEndSession() {
        if (!root.activityActiveKey.length)
            return
        activityTracker.endSession()
        root.activityActiveKey = ""
    }
    // One transient tracker for this lane (§8/§9 Lane A). sink is re-evaluated whenever
    // ProfileActivity (profile switch/suspend) or the context property's own existence changes;
    // activity NEVER breaks playback if it is absent (§25).
    ActivityPlaybackTracker {
        id: activityTracker
        sink: (typeof ProfileActivity !== "undefined") ? ProfileActivity : null
    }

    // Tick the watch position into the store every few seconds while actually playing.
    // Silent: persists for crash-resume via recordSilent() without emitting changed(), so the
    // Continue row is not re-rendered every 5s (that cascade was the proven video-stutter
    // source, 2026-07-29). Lifecycle writes (stop / stream-death / playback-failure /
    // episode-advance / EOF) still notify via recordProgress(); seek and pause intentionally do
    // not (see recordProgress above).
    Timer {
        interval: 5000; repeat: true
        running: !root.starting && !root.errored && !mpv.pause && mpv.duration > 0
        onTriggered: {
            root.recordProgress(true)
            root.activitySample()
        }
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

    // Ends-at ticker: keeps "Ends 11:42 PM" honest as time passes and on any pause/speed
    // change. Cheap 1Hz; only runs while a real duration is known and playing.
    Timer {
        id: endsAtTick
        interval: 1000
        repeat: true
        running: root.visible && root.fileReady
        triggeredOnStart: true
        onTriggered: root.updateEndsAt()
    }
    Connections {
        target: mpv
        function onPauseChanged() { root.updateEndsAt() }
        function onSpeedChanged() { root.updateEndsAt() }
        function onDurationChanged() { root.updateEndsAt() }
        // Chapter crossing rides here (not the direct onPositionChanged handler, which is
        // the loader-dismiss contract) so both fire independently every position tick.
        function onPositionChanged() { root.chapterCrossWatch() }
    }


    function playUrl(url, title) {
        root.hydrateGen += 1   // identity reset: a late hydration callback must not land here
        root.playbackQueueOrderingMode = ""
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.resetSkipSegments()
        root.resetTrackAutomation()
        root.mediaLocalPath = ""
        root.pendingSeekSec = -1
        root.mediaTitle = title || ""
        root.mediaTransport = "Direct file"
        // No stale chips from the previous media: playUrl playbacks carry no identity
        // of their own (no caller establishes a mediaId), so clear the S/E + year
        // sources before rebuilding the line. recordProgress() bails on an empty
        // mediaId, so this also stops the previous media's Continue card from being
        // overwritten by identity-less playback (it was — stale id + stale resume).
        // The candidate list goes too (mirrors playLocalFile) — otherwise
        // updateMediaSubtitle prefers the previous stream's quality/sourceName
        // over this playback's transport.
        root.mediaId = ""
        root.playbackQueue = []
        root.playbackQueueIndex = -1
        root.mediaYear = ""
        root.streamCandidates = []
        root.currentStreamIndex = -1
        root.updateMediaSubtitle()
        root.currentPlaybackUrl = url || ""
        root.errored = false
        root.starting = true
        root.statusMsg = "Opening..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        root.resetRecoveryWatch()
        mpv.loadFile(url)
    }

    // Downloaded-file playback with STREAM-GRADE identity (spec 2026-07-06 downloaded-video
    // parity). Same check-in playTorrent gives a stream: Continue store id + art, online
    // subtitles for the exact episode, resume seek. target: { localPath, id, title, art,
    // kind ("episode"|"movie"), position }.
    // Non-empty = we are reading a download's growing .part straight off disk. Holds the
    // stream url to hand over to if the watcher outruns the download frontier. Landing
    // needs NO handling: the .part→final rename succeeds under mpv's open handle
    // (verified on this machine 2026-07-31) and the handle keeps reading seamlessly.
    property string arrivingStreamUrl: ""
    property var arrivingStreamHeaders: ({})

    function switchArrivingToStream() {
        var url = root.arrivingStreamUrl
        if (!url.length) return
        var headers = root.arrivingStreamHeaders
        root.arrivingStreamUrl = ""
        root.arrivingStreamHeaders = ({})
        var pos = Math.max(0, mpv.position - 2)   // small overlap so the cut lands on a frame he's seen
        root.playRemoteUrl({ "streamUrl": url, "id": root.mediaId, "title": root.mediaTitle,
                             "art": root.mediaArt, "headers": headers,
                             "kind": root.subStreamType === "series" ? "episode" : "movie",
                             "position": pos })
        root.resumePromptConsumed = true   // mid-watch handover, not a fresh open — never prompt
    }

    Timer {
        id: arrivingFrontierWatch
        interval: 2500
        repeat: true
        running: root.arrivingStreamUrl !== "" && root.fileReady
        onTriggered: {
            var rows = (typeof Download !== "undefined") ? (Download.jobs() || []) : []
            var job = null
            for (var i = 0; i < rows.length; i++)
                if (rows[i].id === root.mediaId) { job = rows[i]; break }
            if (!job) {              // landed or cancelled — either way the disk file is all there is
                root.arrivingStreamUrl = ""
                root.arrivingStreamHeaders = ({})
                return
            }
            var ratio = Number(job.ratio || 0)
            if (mpv.duration > 0 && ratio > 0 && ratio < 1
                    && mpv.position > mpv.duration * ratio - 12)
                root.switchArrivingToStream()   // about to outrun the download — go live
        }
    }

    function playLocalFile(target) {
        var t = target || ({})
        var localCtx = t.playbackContext || ({})
        root.arrivingStreamUrl = String(t.arrivingUrl || "")
        root.arrivingStreamHeaders = (t.headers && typeof t.headers === "object" && !Array.isArray(t.headers))
                                    ? t.headers : ({})
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.resetSkipSegments()
        root.resetTrackAutomation()
        root.cancelUpNext()
        root.autoPausedInactive = false
        root.deadStreamKeys = ({})
        root.stubCheckedKey = ""
        root.streamCandidates = []
        root.currentStreamIndex = -1
        root.adjacentEpisodes = root.resolveAdjacentContext(localCtx)
        root.mediaTitle = t.title || ""
        root.mediaTransport = "Downloaded"
        root.mediaYear = String(t.year || "")
        root.mediaArt = t.art || ""
        root.mediaLocalPath = String(t.localPath || "")
        root.mediaId = (t.id && String(t.id).length) ? String(t.id) : ("local:" + root.mediaLocalPath)
        // Show wordmark parity with streaming (playTorrent): a downloaded episode's pause card + loader
        // must show ITS show, not whatever was streamed last. Prefer a context-supplied logo, else DERIVE
        // the metahub wordmark from the tt id in the identity (mediaId is "tt…:S:E" / "tt…"); a bare
        // "local:" path yields no id, so the card falls back to the title text — never a stale logo.
        var _localLogoTt = String(root.mediaId).match(/^(tt\d+)/)
        root.mediaLogo = (localCtx.logo && String(localCtx.logo).length)
            ? String(localCtx.logo)
            : (_localLogoTt ? "https://live.metahub.space/logo/medium/" + _localLogoTt[1] + "/img" : "")
        // Loader backdrop + episode line, same parity as playTorrent (episode still, else backdrop,
        // else the poster; line only if the context carries a clean one) — never the last stream's.
        root.mediaLoadingArt  = localCtx.episodeStill || localCtx.loaderBackdrop || t.art || ""
        root.mediaLoadingLine = localCtx.episodeLine || ""
        root.playbackQueue = localCtx.episodeQueue || []
        root.playbackQueueIndex = localCtx.episodeIndex !== undefined
                                  ? Number(localCtx.episodeIndex) : -1
        root.playbackQueueOrderingMode = ""
        root.updateMediaSubtitle()
        root.mediaResumeHash = ""
        root.mediaResumeFileIdx = 0
        root.currentPlaybackUrl = root.mediaLocalPath
        root.subStreamType = t.kind === "episode" ? "series" : "movie"
        root.subStreamId = (t.id && String(t.id).length) ? String(t.id) : ""
        root.fetchSubtitles()
        root.pendingSeekSec = Number(t.position || 0) > 0 ? Number(t.position) : -1
        root.resumeChoiceOpen = false
        root.resumeChoiceSec = -1
        root.resumePromptConsumed = false
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = "Opening..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        root.resetRecoveryWatch()
        mpv.loadFile(root.mediaLocalPath)
        root.maybeHydrateContext()
    }

    // Play-while-arriving (2026-07-20): watch a LIVE download now — mpv streams the
    // same resolved url DownloadStore is pulling (server holds the complete file, so
    // seeking is free); the download keeps its own connection. Identity is stream-
    // grade like playLocalFile: subtitles + progress key off the video id, so the
    // landed copy resumes where this live watch leaves off.
    function playRemoteUrl(target) {
        var t = target || ({})
        root.arrivingStreamUrl = ""
        root.arrivingStreamHeaders = ({})
        root.clearAbLoop()
        root.cancelSleepTimer()
        root.resetSkipSegments()
        root.resetTrackAutomation()
        root.cancelUpNext()
        root.autoPausedInactive = false
        root.deadStreamKeys = ({})
        root.stubCheckedKey = ""
        root.streamCandidates = []
        root.currentStreamIndex = -1
        root.adjacentEpisodes = ({})
        root.mediaTitle = t.title || ""
        root.mediaTransport = "Arriving"
        root.mediaYear = String(t.year || "")
        root.mediaArt = t.art || ""
        root.mediaLocalPath = ""
        root.mediaId = (t.id && String(t.id).length) ? String(t.id) : ("arriving:" + String(t.streamUrl || ""))
        // Wordmark parity (see playLocalFile): the play-while-downloading card must show ITS show,
        // not the last stream's. Context logo, else metahub-from-tt in the identity, else blank text.
        var _arrCtxLogo = String((t.playbackContext || ({})).logo || "")
        var _arrLogoTt = String(root.mediaId).match(/^(tt\d+)/)
        root.mediaLogo = _arrCtxLogo.length ? _arrCtxLogo
            : (_arrLogoTt ? "https://live.metahub.space/logo/medium/" + _arrLogoTt[1] + "/img" : "")
        root.mediaLoadingArt  = (t.playbackContext || ({})).episodeStill || (t.playbackContext || ({})).loaderBackdrop || t.art || ""
        root.mediaLoadingLine = (t.playbackContext || ({})).episodeLine || ""
        root.playbackQueue = []
        root.playbackQueueIndex = -1
        root.playbackQueueOrderingMode = ""
        root.updateMediaSubtitle()
        root.mediaResumeHash = ""
        root.mediaResumeFileIdx = 0
        root.currentPlaybackUrl = String(t.streamUrl || "")
        root.subStreamType = t.kind === "episode" ? "series" : "movie"
        root.subStreamId = (t.id && String(t.id).length) ? String(t.id) : ""
        root.fetchSubtitles()
        root.pendingSeekSec = Number(t.position || 0) > 0 ? Number(t.position) : -1
        root.resumeChoiceOpen = false
        root.resumeChoiceSec = -1
        root.resumePromptConsumed = false
        root.errored = false
        root.starting = true
        root.fileReady = false
        root.statusMsg = "Opening..."
        root.closeMenus()
        root.wakeChrome()
        root.forceActiveFocus()
        root.resetRecoveryWatch()
        root.loadDirectStreamUrl(root.currentPlaybackUrl, t.headers)
        root.maybeHydrateContext()
    }

    // Title's second line: WHAT you're watching, not just how it arrived.
    // "S2 E5 · 2019 · 1080p · Torrentio" — parts drop out silently when unknown;
    // worst case this reads exactly like the old transport-only line. Never blank
    // while a transport is known.
    function updateMediaSubtitle() {
        var parts = []
        var m = String(root.mediaId || "").match(/^tt\d+:(\d+):(\d+)$/)
        if (m) parts.push("S" + m[1] + " E" + m[2])
        if (root.mediaYear.length) parts.push(root.mediaYear)
        var c = (root.currentStreamIndex >= 0 && root.currentStreamIndex < root.streamCandidates.length)
                ? root.streamCandidates[root.currentStreamIndex] : null
        if (c && c.quality && String(c.quality).length) parts.push(String(c.quality))
        var tail = (c && c.sourceName && String(c.sourceName).length) ? String(c.sourceName) : root.mediaTransport
        if (tail && tail.length && parts.indexOf(tail) < 0) parts.push(tail)
        root.mediaSubtitle = parts.join(" · ")
    }

    // Session precision (Main.qml's pre-wired "Task 5" hooks): minimize captures the exact
    // spot, restore seeks back to it once the file re-opens. Works for streams AND local files.
    function captureState() {
        return { "position": mpv.position > 0 ? mpv.position : 0 }
    }
    function restoreState(st) {
        var p = Number((st || ({})).position || 0)
        if (p > 0)
            root.pendingSeekSec = p
    }

    // --- resume choice (Feature 3) ---
    // On first load only, if there is a meaningful saved position, offer Resume / Start over
    // instead of silently seeking. Near-finished content restarts from zero without prompting.
    function shouldSkipResumePrompt() {
        if (root.resumePromptConsumed)
            return true
        if (root.subStreamId.indexOf("iptv:") === 0 || root.mediaId.indexOf("iptv:") === 0)
            return true
        if (root.pendingSeekSec <= root.resumePromptMinSec)
            return true
        return false
    }

    function prepareResumeChoice() {
        if (root.shouldSkipResumePrompt())
            return false
        root.resumePromptConsumed = true
        root.resumeChoiceSec = root.pendingSeekSec
        if (mpv.duration > 0 && root.resumeChoiceSec / mpv.duration >= root.resumeRestartThreshold) {
            root.pendingSeekSec = 0
            root.resumeChoiceSec = -1
            root.resumeChoiceOpen = false
            return false
        }
        root.pendingSeekSec = -1
        root.resumeChoiceOpen = true
        mpv.pause = true
        root.wakeChrome()
        return true
    }

    function armPlaybackAfterResumeChoice(label, seekSec) {
        // prepareResumeChoice() deliberately retired the initial start state while UI owned
        // the decision. Re-arm BEFORE the seek so that seek-driven position notifications
        // cannot certify playback as started; only later post-unpause advancement may clear it.
        root.starting = true
        root.errored = false
        root.statusMsg = label || "Resuming playback..."
        root.resetRecoveryWatch()
        streamWatchdog.restart()
        if (seekSec >= 0)
            mpv.seekExact(seekSec)
        if (mpv.pause)
            mpv.pause = false
    }

    function acceptResumeChoice() {
        // The file is already loaded and paused — onFileLoaded has fired and will NOT fire
        // again on unpause, so re-arm startup proof, then seek/unpause through one path.
        var seekSec = root.resumeChoiceSec > 0 ? root.resumeChoiceSec : -1
        root.resumeChoiceOpen = false
        root.resumeChoiceSec = -1
        root.armPlaybackAfterResumeChoice("Resuming playback...", seekSec)
    }

    function startOverFromResumeChoice() {
        root.pendingSeekSec = 0
        root.resumeChoiceOpen = false
        root.resumeChoiceSec = -1
        root.armPlaybackAfterResumeChoice("Starting playback...", 0)
    }

    function stop() {
        root.hydrateGen += 1   // player closing: cancel any in-flight context hydration
        root.recordProgress()   // capture where we left off BEFORE mpv clears position
        root.activityEndSession()   // Activity (§9 Lane A): close/lifecycle exit ends the session
        root.closeMenus()
        mpv.command(["stop"])
        root.starting = false
        root.errored = false
        root.statusMsg = ""
    }

    // In-app minimize keeps the stream ALIVE (never stop()): pause so we stop consuming,
    // but the mpv host + torrent stay warm for an instant, reload-free resume.
    function suspendForMinimize() {
        root.closeMenus()
        if (root.fileReady && !mpv.pause) {
            root.autoPausedInactive = true
            mpv.pause = true
        }
    }
    function resumeFromMinimize() {
        root.wakeChrome()
        root.healAudio()   // same dead-AO guard as the taskbar-restore path
        if (root.autoPausedInactive) {
            root.autoPausedInactive = false
            if (mpv.pause)
                mpv.pause = false
        }
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
        var pos = root.displayPosition()
        return mpv.duration > 0 ? root.clamp(pos / mpv.duration, 0, 1) : 0
    }
    function previewAt(mouseX, width) {
        return mpv.duration * root.clamp(mouseX / Math.max(1, width), 0, 1)
    }
    function seekTo(sec) {
        var target = root.clamp(sec, 0, Math.max(0, mpv.duration))
        root.seekTargetSec = target
        seekSettleGuard.restart()
        root.activityDiscontinuity(target)   // Activity (§9 Lane A): central seekTo() path
        mpv.seekExact(target)
        root.wakeChrome()
    }
    function seekStep(delta) {
        if (mpv.duration > 0) {
            root.seekTargetSec = root.clamp(mpv.position + delta, 0, mpv.duration)
            seekSettleGuard.restart()
            root.activityDiscontinuity(root.seekTargetSec)   // Activity: same reset as seekTo()
        }
        mpv.seekStep(delta)
        root.wakeChrome()
    }

    // Slice 3: Player 1 remains the playback state machine. These helpers separate
    // explicit local user intent from authoritative room application so ordinary
    // mpv lifecycle/property changes can never echo back as room commands.
    function syncWatchPartyPlayerObservation() {
        var locallyPlayable = root.fileReady && !root.starting && !root.errored
                && (!root.windowMinimized || root.pipMode)
                && !root.resumeChoiceOpen
        var exactRoomSource = root.watchPartyRoomActive
                && root.watchPartySourceMatchesRoom
                && root.watchPartySourceEligible
        var roomReady = locallyPlayable && exactRoomSource

        if (typeof WatchPartyUi !== "undefined")
            WatchPartyUi.setLocalSourceReady(roomReady)

        if (typeof WatchPartySync === "undefined" || !WatchPartySync.active) {
            if (typeof WatchPartyUi !== "undefined" && root.watchPartyRoomActive)
                WatchPartyUi.updateLocalParticipantState(false, "unknown")
            return
        }

        var buffering = roomReady && !mpv.pause
                && mpv.cacheBufferingState >= 0
                && mpv.cacheBufferingState < 100
        WatchPartySync.observePlayer(mpv.position, mpv.pause, roomReady, buffering,
                                     mpv.coreSeeking, Date.now())
        if (typeof WatchPartyUi !== "undefined")
            WatchPartyUi.updateLocalParticipantState(roomReady, WatchPartySync.syncStatus)
    }
    function requestUserSeekTo(sec) {
        var target = root.clamp(sec, 0, Math.max(0, mpv.duration))
        if (typeof WatchPartySync !== "undefined" && WatchPartySync.active) {
            WatchPartySync.requestLocalSeek(target)
            root.wakeChrome()
            return
        }
        root.seekTo(target)
    }
    function requestUserSeekStep(delta) {
        if (typeof WatchPartySync !== "undefined" && WatchPartySync.active) {
            var target = Math.max(0, mpv.position + delta)
            if (mpv.duration > 0)
                target = Math.min(target, mpv.duration)
            root.requestUserSeekTo(target)
            return
        }
        root.seekStep(delta)
    }
    function catchUpWatchParty() {
        if (typeof WatchPartySync !== "undefined" && WatchPartySync.active)
            WatchPartySync.catchUp(Date.now())
    }

    // --- skip segments (Feature 4) ---
    function skipSegmentsExcluded() {
        if (root.subStreamId.indexOf("iptv:") === 0 || root.mediaId.indexOf("iptv:") === 0)
            return true
        return false
    }

    function resetSkipSegments() {
        root.skipSegments = []
        root.skipDiagnostics = ""
        root.dismissedSkipKey = ""
        root.autoSkippedKey = ""
        root.skipLoadGeneration += 1
        root.lastChapterIdx = -1          // fresh media: no stale chapter to "cross" from
        root.chapterTransient = ""
        root.pauseHydratedId = ""         // fresh media: re-hydrate the pause card on next pause
        root.mediaPlot = ""
        root.mediaEpisodeName = ""
        root.mediaLogo = ""               // fresh media: drop the previous show's wordmark. playTorrent
                                          // is the ONLY path that sets mediaLogo, so without this a
                                          // downloaded/direct video keeps the last stream's logo on the
                                          // pause card + loader (One Piece over Hawkeye). Non-stream
                                          // paths repopulate it below from their own identity.
        root.mediaLoadingArt = ""         // fresh media: same blind spot as mediaLogo — the loader
        root.mediaLoadingLine = ""        // backdrop + episode line are playTorrent-only, so clear
                                          // them here too, else a downloaded/direct video flashes the
                                          // previous stream's art + "S.. E.." line on the loader.
        root.pauseCardShown = false
    }

    function currentSkipSegment() {
        if (root.upNextVisible)
            return null
        if (!root.showSkipButton)
            return null
        var seg = SkipSegments.activeSegment(root.skipSegments, mpv.position)
        if (!seg || seg.key === root.dismissedSkipKey)
            return null
        return seg
    }

    function skipLabel(seg) {
        if (!seg) return ""
        if (seg.kind === "intro") return "Skip Intro"
        if (seg.kind === "recap") return "Skip Recap"
        return "Skip Credits"
    }

    function performSegmentSkip(seg) {
        var skip = seg || root.currentSkipSegment()
        if (!skip || mpv.duration <= 0)
            return
        root.seekTo(Math.min(mpv.duration - 0.25, skip.endSec + root.skipSafetyOffsetSec))
        root.dismissedSkipKey = skip.key
    }

    function maybeAutoSkipSegment() {
        var seg = SkipSegments.activeSegment(root.skipSegments, mpv.position)
        if (!seg || seg.key === root.autoSkippedKey)
            return
        var shouldSkip = (seg.kind === "intro" && root.autoSkipIntro)
                      || (seg.kind === "recap" && root.autoSkipRecap)
                      || (seg.kind === "outro" && root.autoSkipCredits)
        if (!shouldSkip)
            return
        root.autoSkippedKey = seg.key
        root.performSegmentSkip(seg)
    }

    function malEpisodeIdentity() {
        var id = String(root.subStreamId || root.mediaId || "")
        var parts = id.split(":")
        if (parts.length < 3 || parts[0] !== "mal")
            return null
        return { "malId": Number(parts[1]), "episode": Number(parts[2]) }
    }

    function fetchAniSkipSegments(done) {
        var ident = root.malEpisodeIdentity()
        if (!ident || !(ident.malId > 0) || !(ident.episode > 0) || !(mpv.duration > 0)) {
            done([])
            return
        }
        var xhr = new XMLHttpRequest()
        var params = "types=op&types=ed&types=mixed-op&types=mixed-ed&types=recap&episodeLength=" + Math.round(mpv.duration)
        xhr.open("GET", "https://api.aniskip.com/v2/skip-times/" + ident.malId + "/" + ident.episode + "?" + params)
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return
            if (xhr.status < 200 || xhr.status >= 300) {
                root.skipDiagnostics = "AniSkip unavailable"
                done([])
                return
            }
            try {
                done(SkipSegments.parseAniSkipResults(JSON.parse(xhr.responseText)))
            } catch (e) {
                root.skipDiagnostics = "AniSkip parse failed"
                done([])
            }
        }
        root.skipDiagnostics = "Loading skip data"
        xhr.send()
    }

    // Loads chapter- and AniSkip-derived segments and merges them. Safe to call repeatedly:
    // mpv's chapter-list arrives asynchronously and stream duration is often still 0 at
    // onFileLoaded, so this is re-run on mpv.onChaptersChanged and mpv.onDurationChanged too.
    // The skipLoadGeneration counter (bumped by resetSkipSegments) drops any stale AniSkip
    // fetch that returns after a newer load began. [Feature 4 review adaptation]
    function loadSkipSegments() {
        if (root.skipSegmentsExcluded() || !(mpv.duration > 0))
            return
        var gen = root.skipLoadGeneration
        var chapterSegments = SkipSegments.chaptersToSegments(mpv.chapters, mpv.duration)
        root.fetchAniSkipSegments(function(aniSegments) {
            if (gen !== root.skipLoadGeneration)
                return
            root.skipSegments = SkipSegments.mergeSegments([aniSegments, chapterSegments], mpv.duration)
            root.skipDiagnostics = root.skipSegments.length ? "Skip data ready" : ""
        })
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
            // "decoder / output" — mpv's property naming is inverted from the plain reading:
            // `frame-drop-count` is the OUTPUT (VO) drop count and `decoder-frame-drop-count` is
            // the decoder's. This pair used to read frame-drop-count into the DECODER slot and
            // ask for a `vo-drop-frame-count` that mpv does not have, so the card showed the
            // output count under the decoder label and "NaN" under the output label (the invalid
            // property comes back as an ErrorReturn object, and Number(object) is NaN). Fixed
            // 2026-07-29 to match native/player/mpvitem.cpp statsPayload(), which is the mapping
            // the zero-drop gate measures against.
            "frameDropDecoder": mpv.mpvProperty("decoder-frame-drop-count"),
            "frameDropOutput": mpv.mpvProperty("frame-drop-count"),
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
    function togglePlayPause() {
        if (!root.starting && !root.errored) {
            if (typeof WatchPartySync !== "undefined" && WatchPartySync.active)
                WatchPartySync.requestLocalPlayback(mpv.pause, mpv.position)
            else
                mpv.pause = !mpv.pause
        }
        root.wakeChrome()
    }
    // Volume is a plain linear 0..100 (Hemanth 2026-07-09: "make the volume make sense...
    // no proper progression"). The old scale ran to 600% with a piecewise slider (0..62%
    // of travel = 0..100, the rest blasting to 600 and flipping red) — that was the
    // "loud as hell / yellow then red" mess. 100 = source loudness (mpv 0dB); no boost.
    function setVolumeFromFraction(f) {
        root.adjustVolumeTo(f * 100)
    }
    function adjustVolume(delta) {
        root.adjustVolumeTo(mpv.volume + delta)
    }
    function adjustVolumeTo(v) {
        mpv.volume = Math.round(root.clamp(v, 0, 100))
        if (mpv.volume > 0)
            mpv.mute = false
        root.wakeChrome()
    }
    function volumeFraction() {
        return root.clamp(mpv.volume, 0, 100) / 100
    }
    function closeMenus() {
        watchPartyMenu.panelOpen = false
        audioMenu.panelOpen = false
        subMenu.panelOpen = false
        subStyleBar.open = false
        speedMenu.panelOpen = false
        fillMenu.panelOpen = false
        root.liveGuideOpen = false
        root.dvrPanelOpen = false
        root.overflowOpen = false
        root.closeConfirmOpen = false
        root.shortcutsOpen = false
        root.browserOpen = false
    }
    // One Escape contract for both the player's focused key handler and Main.qml's
    // application Shortcut: temporary controls close first, otherwise Back minimizes
    // through the existing session-aware player seam.
    function requestEscape() {
        if (root.shortcutsOpen) { root.shortcutsOpen = false; return }
        if (root.anyMenuOpen) root.closeMenus()
        else root.backRequested()
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
    // Feature 7: key presses resolve to a registry action id, then dispatch through ONE switch
    // that preserves each shortcut's exact prior behavior. (Fullscreen toggle retired — the app
    // is always fullscreen.)
    function handlePlayerHotkey(event) {
        root.wakeChrome()
        var action = PlayerHotkeys.actionForEvent(event)
        if (!action)
            return false
        event.accepted = true
        root.runHotkeyAction(action.id, event)
        return true
    }

    function runHotkeyAction(actionId, event) {
        switch (actionId) {
        case "space":
            if (event.isAutoRepeat)
                return
            root.spaceBaseSpeed = mpv.speed
            root.spaceHoldFired = false
            spaceHoldTimer.restart()
            return
        case "escape": root.requestEscape(); return
        case "seekBack": root.requestUserSeekStep(-root.seekBackSeconds); return
        case "seekForward": root.requestUserSeekStep(root.seekForwardSeconds); return
        case "frameBack": if (mpv.pause) mpv.frameBackStep(); else root.requestUserSeekStep(-30); return
        case "frameForward": if (mpv.pause) mpv.frameStep(); else root.requestUserSeekStep(30); return
        case "seekStart": root.requestUserSeekTo(0); return
        case "seekEnd": if (mpv.duration > 0) root.requestUserSeekTo(mpv.duration - 0.5); return
        case "seekPercent":
            var digit = event.key - Qt.Key_0
            root.requestUserSeekTo(digit === 0 ? 0 : mpv.duration * digit / 10)
            return
        case "mute": mpv.mute = !mpv.mute; return
        case "volumeUp": root.adjustVolume(event.modifiers & Qt.ShiftModifier ? 1 : 5); return
        case "volumeDown": root.adjustVolume(event.modifiers & Qt.ShiftModifier ? -1 : -5); return
        case "speedDown": mpv.speed = root.clamp(root.round2(mpv.speed - 0.25), 0.25, 3); return
        case "speedUp": mpv.speed = root.clamp(root.round2(mpv.speed + 0.25), 0.25, 3); return
        case "subtitleDelayDown": mpv.subDelay = root.round2(mpv.subDelay - (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return
        case "subtitleDelayUp": mpv.subDelay = root.round2(mpv.subDelay + (event.modifiers & Qt.ShiftModifier ? 0.05 : 0.1)); return
        case "cycleSubtitle": root.cycleSubtitle(); return
        case "abLoopA": root.setAbLoopA(); return
        case "abLoopB": root.setAbLoopB(); return
        case "abLoopClear": root.clearAbLoop(); return
        case "stats":
            root.statsOverlayOpen = !root.statsOverlayOpen
            if (root.statsOverlayOpen)
                root.refreshPlaybackStats()
            return
        case "browser": {
            var wasOpen = root.browserOpen
            root.closeMenus()
            root.browserOpen = !wasOpen
            root.wakeChrome()
            return
        }
        case "shortcuts": root.shortcutsOpen = true; return
        }
    }
    // Right-hand value for a "More controls" row: what that control is currently set to, so the
    // menu reports state instead of just listing verbs. Computed per row in the delegate (NOT in the
    // Repeater's model array) -- a model that read mpv.speed would rebuild every delegate on each
    // speed change, which is the reactive-cascade shape that caused the 2026-07-29 video stutter.
    function overflowValue(kind) {
        if (kind === "loudness") return root.loudnessLabel()
        if (kind === "speed") return (Math.round(mpv.speed * 100) / 100) + "×"
        if (kind === "fill") {
            var m = root.fillModes[root.fillModeIndex]
            return m ? m.label : ""
        }
        if (kind === "audio") {
            var a = root.firstSelectedTrack(root.audioRows)
            return a ? String(a.lang || a.title || a.label || "") : ""
        }
        if (kind === "stats") return root.statsOverlayOpen ? "On" : ""
        if (kind === "pip") return root.pipMode ? "On" : ""
        if (kind === "shortcuts") return "?"
        if (kind === "gif") return root.gifState === "recording" ? root.fmtTime(root.gifElapsedSec) : ""
        return ""
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
        mpv.setAudioNormalization(root.loudnessMode)   // apply the persisted mode at startup
    }
    Component.onDestruction: if (typeof Power !== "undefined") Power.release()
    onVisibleChanged: {
        if (visible)
            root.forceActiveFocus()
        else if (typeof Stream !== "undefined") {
            // backed out mid-load on the kept-alive page: don't leave the 1 Hz stats poll running
            Stream.unwatchStats()
            root.streamStatsSeen = false
        }
        root.syncPowerInhibit()
    }
    onStartingChanged: {
        if (starting)
            root.wakeChrome()
        else if (typeof Stream !== "undefined") {
            // loading face retired (first frame, error path, or back-out) → stop the 1 Hz stats poll
            Stream.unwatchStats()
            root.streamStatsSeen = false
        }
        root.syncPowerInhibit()
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; z: -1; color: "#000000" }

    // F9 seek thumbnails: hover state + the extraction seam (C++ owns ffmpeg/cache).
    property string hoverThumbUrl: ""
    property real hoverThumbBucket: -1
    function thumbBucketOf(t) { return Math.floor(Math.max(0, t) / 5) * 5 }
    function chapterTitleAt(t) {
        var r = root.chapterAtFraction(t)
        return r.idx >= 0 ? r.title : ""
    }
    // The chapter covering time t: {idx (0-based, -1 if none), count, title, startSec}.
    function chapterAtFraction(t) {
        var list = mpv.chapters || []
        var idx = -1
        for (var i = 0; i < list.length; i++) {
            if ((list[i].startSec || 0) <= t) idx = i
            else break
        }
        return {
            "idx": idx,
            "count": list.length,
            "title": idx >= 0 ? (list[idx].title || "") : "",
            "startSec": idx >= 0 ? (list[idx].startSec || 0) : 0
        }
    }
    // "Chapter 3 · Judicial" for the seek-hover tag (numbered, per the approved mock).
    function chapterHoverLabel(t) {
        var r = root.chapterAtFraction(t)
        if (r.idx < 0 || r.count < 2) return ""
        var name = r.title.length ? (" · " + r.title) : ""
        return "Chapter " + (r.idx + 1) + name
    }
    // Crossing watch: when the played-through chapter changes during play, the state
    // line briefly speaks it ("Chapter 3 of 5 — Judicial"), then clears after 4s.
    property int lastChapterIdx: -1
    function chapterCrossWatch() {
        if (root.starting || root.errored || root.seeking) return
        var r = root.chapterAtFraction(mpv.position)
        if (r.idx === root.lastChapterIdx) return
        var prev = root.lastChapterIdx
        root.lastChapterIdx = r.idx
        // Announce only real, named, non-first crossings while actually playing.
        if (prev >= 0 && r.idx > 0 && r.count > 1 && (r.startSec || 0) > 1 && !mpv.pause) {
            var name = r.title.length ? (" — " + r.title) : ""
            root.chapterTransient = "Chapter " + (r.idx + 1) + " of " + r.count + name
            chapterTransientClear.restart()
        }
    }
    Timer {
        id: chapterTransientClear
        interval: 4000
        onTriggered: root.chapterTransient = ""
    }

    // ── Pause info card machinery (Tier 2) ──
    readonly property bool pauseCardEligible:
        mpv.pause && !root.starting && !root.errored && !root.seeking && !root.anyMenuOpen && root.fileReady
    onPauseCardEligibleChanged: {
        if (root.pauseCardEligible) { root.hydratePauseCard(); pauseCardDelay.restart() }
        else { pauseCardDelay.stop(); root.pauseCardShown = false }
    }
    Timer {
        id: pauseCardDelay
        interval: 900
        onTriggered: if (root.pauseCardEligible) root.pauseCardShown = true
    }
    // The facts line — episode/season, name, year, runtime, ends clock. NO rating
    // (Hemanth veto 2026-07-20: "I don't want to see the imdb rating of that episode").
    function pauseFactsLine() {
        var parts = []
        // S/E + episode name: prefer the producer's loader line, else derive.
        if (root.mediaLoadingLine.length) parts.push(root.mediaLoadingLine)
        else {
            var m = String(root.mediaId || "").match(/^tt\d+:(\d+):(\d+)$/)
            if (m) parts.push("S" + m[1] + " E" + m[2])
            if (root.mediaEpisodeName.length) parts.push(root.mediaEpisodeName)
        }
        if (root.mediaYear.length) parts.push(root.mediaYear)
        if (mpv.duration > 0) parts.push(Math.round(mpv.duration / 60) + " min")
        if (root.endsAtClock.length) parts.push("ends " + root.endsAtClock)
        return parts.join("  ·  ")
    }
    // mpvProperty can return an unavailable/error-wrapped QVariant that stringifies to
    // "QVariant(ErrorReturn, ...)" — never let that leak into the UI. Clean to "" instead.
    function mpvClean(key) {
        var v = mpv.mpvProperty(key)
        if (v === undefined || v === null) return ""
        var s = String(v)
        if (!s.length || s.indexOf("QVariant") >= 0 || s.indexOf("ErrorReturn") >= 0) return ""
        return s
    }
    // The quality line — resolution · video codec · audio codec · channels · HDR, all
    // from mpv (sanitized), inline lettering (no chips — transparent tablets read cheap).
    function pauseQualityLine() {
        var out = []
        var h = Number(root.mpvClean("height"))
        if (h > 0) out.push(h + "p")
        var vc = root.mpvClean("video-codec").split(" ")[0]
        if (vc.length) out.push(vc.toUpperCase())
        var ac = root.mpvClean("audio-codec").split(" ")[0]
        if (ac.length) out.push(ac.toUpperCase())
        var ch = Number(root.mpvClean("audio-params/channel-count"))
        if (ch > 0) out.push(root.channelLabel(ch))
        var transfer = root.mpvClean("video-params/transfer").toLowerCase()
        if (transfer.indexOf("pq") >= 0 || transfer.indexOf("smpte2084") >= 0) out.push("HDR")
        else if (transfer.indexOf("hlg") >= 0) out.push("HLG")
        return out.join("  ·  ")
    }
    function channelLabel(n) {
        if (n >= 8) return "7.1"
        if (n >= 6) return "5.1"
        if (n >= 3) return (n - 1) + ".1"
        if (n === 2) return "2.0"
        return "1.0"
    }
    // Lazy plot/episode-name hydration — one call per media, generation-guarded so a
    // late callback for a since-switched source is dropped.
    function hydratePauseCard() {
        var id = String(root.mediaId || "")
        if (!id.length || root.pauseHydratedId === id) return
        root.pauseHydratedId = id
        if (typeof TheatreApi === "undefined") return
        var epMatch = id.match(/^(tt\d+):(\d+):(\d+)$/)
        if (epMatch) {
            var seriesId = epMatch[1], seasonN = Number(epMatch[2]), epN = Number(epMatch[3])
            TheatreApi.loadMeta("series", seriesId, function(meta) {
                if (root.pauseHydratedId !== id) return       // media switched under us
                var vids = (meta && meta.videos) ? meta.videos : []
                for (var i = 0; i < vids.length; i++) {
                    var v = vids[i]
                    if (Number(v.season) === seasonN && Number(v.episode) === epN) {
                        root.mediaPlot = String(v.overview || v.description || "")
                        if (!root.mediaEpisodeName.length)
                            root.mediaEpisodeName = String(v.title || v.name || "")
                        return
                    }
                }
                if (!root.mediaPlot.length) root.mediaPlot = String(meta && meta.description || "")
            })
        } else if (id.indexOf("tt") === 0) {
            TheatreApi.loadMeta("movie", id, function(meta) {
                if (root.pauseHydratedId !== id) return
                root.mediaPlot = String(meta && meta.description || "")
            })
        }
    }
    function requestSeekThumb() {
        if (mpv.duration > 0 && !root.seeking && mpv.currentUrl.toString().length > 0)
            seekThumbs.request(mpv.currentUrl, root.seekPreview)
    }
    SeekThumbnailer {
        id: seekThumbs
        onThumbReady: function(bucketSec, imageUrl) {
            // Always keep the newest frame (ThumbFast rule): a slightly-behind picture
            // held steady beats a card that blinks empty between buckets. Rejecting
            // off-bucket arrivals here is what made the first cut flicker (2026-07-18).
            root.hoverThumbBucket = bucketSec
            root.hoverThumbUrl = imageUrl
        }
    }
    MpvItem {
        id: mpv
        objectName: "playerMpv"
        anchors.fill: parent
        z: 0
        Component.onCompleted: {
            // mpv's own --profile=fast, applied option-by-option (the named profile isn't
            // guaranteed across libmpv versions). Rationale: on this machine one integrated GPU
            // serves BOTH mpv's per-frame video shaders AND Qt's scene compositing; every pass
            // mpv spends on luxury scaling/dither/HDR-peak analysis is a pass stolen from
            // putting the frame on screen. Upstream reports this exact preset curing stutter on
            // low-spec hardware (mpv#9417 family). Costs some scaling finesse — Hemanth's eyes
            // are the gate; revert this one block to restore mpv's quality defaults.
            mpv.setProperty("scale", "bilinear")
            mpv.setProperty("cscale", "bilinear")
            mpv.setProperty("dscale", "bilinear")
            mpv.setProperty("dither", "no")
            mpv.setProperty("correct-downscaling", "no")
            mpv.setProperty("linear-downscaling", "no")
            mpv.setProperty("sigmoid-upscaling", "no")
            mpv.setProperty("hdr-compute-peak", "no")
        }
        onCurrentUrlChanged: {
            seekThumbs.reset()          // new file = new frames; stale thumbs must not survive
            root.hoverThumbUrl = ""
            root.hoverThumbBucket = -1
        }
        onFileStarted: {
            root.starting = true
            root.statusMsg = "Buffering..."
            root.wakeChrome()
            root.syncPowerInhibit()
            root.syncWatchPartyPlayerObservation()
        }
        onFileLoaded: {
            // fileLoaded means mpv OPENED the file (its track list + duration are now available) —
            // NOT that playback has started. fileReady is set here because the track/subtitle
            // automation below needs the track list, but the loading state (starting / statusMsg /
            // the startup watchdog) retires only when playback genuinely ADVANCES —
            // finishStartingIfPlaybackAdvanced() via onPositionChanged — EXCEPT when a UI/intent
            // state below owns this load (the Resume overlay or a wake-restore), where the pause is
            // deliberate and there is nothing left to wait for. Treating "loaded" as "playing" is
            // what stranded a buffered stream at 00:00 paused when a prior pause carried over.
            root.errored = false
            root.seekPreview = mpv.position
            root.fileReady = true
            var startupOwnedByUi = false
            if (root.wakeReconnectPendingSeek > 1) {
                // Reconnected after a system-wake gap — restore the pre-sleep position and honor
                // whatever pause state carried (a playing stream resumes, a deliberate pause stays).
                var pos = root.wakeReconnectPendingSeek
                root.wakeReconnectPendingSeek = -1
                mpv.seekExact(pos)
                startupOwnedByUi = true
            } else if (root.pendingSeekSec > 0 && root.prepareResumeChoice()) {
                // The Resume overlay is up and owns a deliberate pause.
                startupOwnedByUi = true
            } else if (root.pendingSeekSec > 0) {     // resume / session-restore precision
                mpv.seekExact(root.pendingSeekSec)
                root.pendingSeekSec = -1
            } else if (root.pendingSeekSec === 0) {
                mpv.seekExact(0)
                root.pendingSeekSec = -1
            }
            if (startupOwnedByUi) {
                // A UI/intent state owns this load — startup is over even without playback
                // advancement, so the startup watchdog must not fire on it.
                root.starting = false
                root.statusMsg = ""
                streamWatchdog.stop()
            } else if (mpv.pause) {
                // Fresh play that inherited a STALE pause from a prior deliberate pause (a previous
                // minimize / sleep-timer / manual pause / dismissed resume). mpv keeps its pause
                // property across `loadfile replace`, so clear it here so the buffered stream
                // autoplays. Startup still retires on genuine advancement, so a stream that opens
                // but never plays is caught as a failed start by the watchdog, not silently "ready".
                mpv.pause = false
            }
            // Feature 6: language-ranked automation runs first; maybeAutoSub is the pickDefault
            // fallback that only acts when automation found no language match (guarded by trackAutoDoneKey).
            root.maybeAutoSelectTracks("file-loaded")
            root.maybeAutoSub()      // file is open → safe to sub-add the auto/online subtitle
            root.wakeChrome()
            root.syncPowerInhibit()
            root.detectStubStream()
            root.loadSkipSegments()  // first attempt; re-runs on chapters/duration settle
            root.syncWatchPartyPlayerObservation()
            // Activity (§9 Lane A): "media load/reload"/"stream replacement"/"recovery/reseek"
            // reset bullets. A no-op when no session is open yet (a genuinely new identity's
            // session is begun later, in finishStartingIfPlaybackAdvanced, once playback
            // actually advances) — this only resets the baseline for a reload of the SAME item.
            root.activityDiscontinuity()
        }
        onPlaybackError: function(code, message) {
            root.activityDiscontinuity()   // Activity (§9 Lane A): "error" reset bullet
            root.handlePlaybackIssue(code, message)
        }
        onEndFile: function(reason) {
            root.starting = false
            root.fileReady = false
            root.syncPowerInhibit()
            root.syncWatchPartyPlayerObservation()
            // error/other now route through onPlaybackError → handlePlaybackIssue (typed code),
            // which owns the recovery ladder. Calling handlePlaybackFailure here too would
            // double-fire the retry (both signals emit on the same error). [Feature 3]
            if (reason === "eof") {
                if (root.arrivingStreamUrl !== "" && mpv.duration > 0
                        && mpv.position < mpv.duration - 5) {
                    // Hit the end of the .part, not the end of the film — the frontier
                    // watcher's predictive handover missed (burst download, deep seek).
                    // Activity: same item continues via switchArrivingToStream, so this is a
                    // discontinuity (handover bridge discarded), never naturalEof().
                    root.activityDiscontinuity()
                    root.switchArrivingToStream()
                    return
                }
                root.recordProgress()
                root.activityNaturalEof()   // Activity (§9 Lane A): real end of the item
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
            root.syncWatchPartyPlayerObservation()
            root.activityDiscontinuity()   // Activity (§9 Lane A): pause/resume transition
        }
        onSpeedChanged: root.activityDiscontinuity()   // Activity (§9 Lane A): speed change
        onPositionChanged: {
            root.finishStartingIfPlaybackAdvanced()
            root.syncWatchPartyPlayerObservation()
        }
        onGifSaved: function(path) {
            root.gifState = "idle"
            root.gifElapsedSec = 0
            root.showGifToast(true, path)
        }
        onGifFailed: {
            root.gifState = "idle"
            root.gifElapsedSec = 0
            root.showGifToast(false, "")
        }
        onDurationChanged: { root.detectStubStream(); root.loadSkipSegments() }
        onChaptersChanged: root.loadSkipSegments()
        onTrackListChanged: root.maybeAutoSelectTracks("track-list")
        onCacheBufferingStateChanged: root.syncWatchPartyPlayerObservation()
        onCoreSeekingChanged: {
            if (!mpv.coreSeeking) {
                root.seekTargetSec = -1
                seekSettleGuard.stop()
            }
            root.syncWatchPartyPlayerObservation()
        }
    }

    Connections {
        target: typeof WatchPartySync !== "undefined" ? WatchPartySync : null

        function onPlayerObservationRequested() {
            root.syncWatchPartyPlayerObservation()
        }
        function onSeekRequested(positionSeconds) {
            // Authoritative room application uses the raw Player 1 seam. It never
            // passes through requestUserSeek* and therefore cannot echo outbound.
            root.seekTo(positionSeconds)
        }
        function onPauseRequested(paused) {
            if (mpv.pause !== paused)
                mpv.pause = paused
            root.wakeChrome()
        }
    }

    Connections {
        target: typeof WatchPartyUi !== "undefined" ? WatchPartyUi : null

        function onRoomChanged() {
            root.syncWatchPartyPlayerObservation()
        }
        function onRoomActivated() {
            root.syncWatchPartyPlayerObservation()
            root.wakeChrome()
        }
    }

    onWatchPartySourceChanged: root.syncWatchPartyPlayerObservation()

    // Per-show cinematic loader (Task 5): replaces the old black blanker + title card. Covers the
    // stale mpv frame while starting/errored and exits on the truthful first-frame advance. It
    // yields (active:false) when the resume-choice prompt must become interactive, but `starting`
    // is retained until the choice leads to playback. Fed by the loader identity from playTorrent.
    PlayerLoadingScreen {
        id: playerLoadingScreen
        anchors.fill: parent
        z: 4
        active: (root.starting && !root.resumeChoiceOpen) || root.errored
        errored: root.errored
        // Clean show name for the loader — strip a trailing " - S3E4" so the episode line owns S/E.
        title: (root.mediaTitle || mpv.mediaTitle || "").replace(/\s+[-–—]\s+S\d+\s*E\d+.*$/i, "")
        episodeLine: root.mediaLoadingLine
        logoUrl: root.mediaLogo
        backdropUrl: root.mediaLoadingArt
        statusText: root.loadingStatusText()
        errorText: root.loadingStatusText()
        hudFamily: theme.hud
        onCancelRequested: {
            root.closeMenus()
            root.backRequested()
        }
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
            root.streamStatsSeen = false
            root.streamStatsPeers = 0
            root.streamStatsSpeedBps = 0
            root.streamStatsDownloaded = 0
            Stream.watchStats(infoHash, fileIdx)
            mpv.loadFile(url)
        }
        function onStreamStats(infoHash, fileIdx, stats) {
            if (!root.starting)
                return
            root.streamStatsSeen = true
            root.streamStatsPeers = Number(stats.peers || 0)
            root.streamStatsSpeedBps = Number(stats.downloadSpeed || 0)
            root.streamStatsDownloaded = Number(stats.downloaded || 0)
        }
        function onStreamError(message) {
            root.statusMsg = message
            root.handlePlaybackFailure("stream")
        }
    }


    Connections {
        target: Live
        function onChannelSwitchRequested(channel) {
            root.liveGuideOpen = false
            root.configureLiveChannel(channel)
            root.playUrl(channel.url || "", channel.name || "Live channel")
            root.mediaTransport = channel.group || "Live channel"
            root.updateMediaSubtitle()
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

    // Recovery watch (Feature 3): while a non-local, non-live stream is actually playing,
    // sample position + video dimensions to catch frozen/black streams the start watchdog misses.
    Timer {
        id: recoveryWatchTimer
        interval: 1000
        repeat: true
        running: !root.recoveryExcluded() && !root.starting && !root.errored && !mpv.pause && root.fileReady
        onTriggered: root.tickRecoveryWatch()
    }

    // Wake reconnect (Feature 3): a light tick whose skipped gap reveals a system sleep/stall,
    // after which the current stream URL is reloaded at the last known position.
    Timer {
        id: wakeReconnectTimer
        interval: root.wakeReconnectTickMs
        repeat: true
        running: !root.recoveryExcluded() && root.fileReady
        onTriggered: root.tickWakeReconnect()
    }

    // 2s = longest a never-acknowledged seek may pin the display before falling back to truth
    Timer {
        id: seekSettleGuard
        interval: 2000
        repeat: false
        onTriggered: root.seekTargetSec = -1
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

    Timer {
        id: skipSegmentTimer
        interval: 500
        repeat: true
        running: root.skipSegments.length > 0 && !root.starting && !root.errored && !mpv.pause
        onTriggered: root.maybeAutoSkipSegment()
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Cursor vanishes WITH the HUD (Hemanth 2026-07-09: no sore-thumb arrow floating
        // mid-screen after the bar fades). Mirrors the chrome's own visibility expression
        // (controlsShown && !starting) so they hide/show as one. Any pointer motion fires
        // wakeChrome → controlsShown true → the arrow is back instantly.
        cursorShape: (root.controlsShown && !root.starting) ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: root.wakeChrome()
        onClicked: function(mouse) {
            // Right-click = More controls at the cursor (native-player convention,
            // Hemanth 2026-07-18) — the ⋯ button it replaces is gone from the bar.
            if (mouse.button === Qt.RightButton) {
                root.openContextMenu(mapToItem(chrome, mouse.x, mouse.y))
                return
            }
            // A click that only dismisses an open menu must NOT also toggle play/pause.
            if (root.anyMenuOpen) {
                root.closeMenus()
                return
            }
            root.togglePlayPause()
        }
    }

    // Scroll anywhere over the video to change volume (Hemanth 2026-07-09), 5% per notch.
    // Disabled while a menu/list is open so the wheel scrolls that panel, not the volume.
    WheelHandler {
        enabled: !root.anyMenuOpen
        acceptedModifiers: Qt.NoModifier
        onWheel: function(event) {
            root.adjustVolume(event.angleDelta.y > 0 ? 5 : -5)
        }
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
        root.handlePlayerHotkey(event)
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

    // Branded buffering face (Hemanth 2026-07-09, Variant 1 "title card, pure type";
    // ring + percentage removed 2026-07-09 — "just let the title indicate buffering").
    // While a stream loads the show introduces itself: title big in the house serif +
    // the titlebar's meta line. The card's mere presence (video not yet playing) IS the
    // The old title-card + status Column was folded into PlayerLoadingScreen (Task 5) — the loader
    // now owns the title/logo, episode line, status text, and the indeterminate bar.

    Rectangle {
        id: chrome
        x: 0
        y: 0
        width: root.chromeVisibleWidth
        height: root.chromeVisibleHeight
        z: 99999
        color: Qt.rgba(0, 0, 0, 0.001)
        // Clean loading screen (Hemanth 2026-07-08): while a stream is LOADING, step the
        // whole chrome aside so the centered spinner+status overlay (visible: starting)
        // owns the screen instead of stacking on top of the centered transport buttons.
        // Gate on `starting` only — the error state keeps the chrome up because retry
        // lives in the control bar. Chrome fades back in the instant playback begins.
        opacity: (root.controlsShown && !root.starting) ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // Harbor-parity top overlay (approved 2026-07-19): no fused bar. A restrained
        // transparent->black scrim carries edge readability; free-floating groups sit over it.
        // Left: circular back + NOW PLAYING micro-label, title, episode/metadata. Right:
        // minimize + close only. Fullscreen lives ONCE, in the bottom transport group.
        Item {
            id: playerTopScrim
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 112
            transform: Translate {
                y: root.controlsShown && !root.starting ? 0 : -8
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.60) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: tight ? 16 : 28
                anchors.top: parent.top
                anchors.topMargin: tight ? 12 : 18
                spacing: 14
                RoundButton {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 38
                    icon: "back"
                    tooltip: "Back"
                    onClicked: {
                        root.closeMenus()
                        root.backRequested()
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "NOW PLAYING"
                        visible: !tight
                        color: theme.inkDimmer
                        font.family: theme.hud
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2
                    }
                    Text {
                        text: root.mediaTitle || mpv.mediaTitle
                        color: theme.ink
                        font.family: theme.hud
                        font.pixelSize: tight ? 16 : 19
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, chrome.width * 0.5)
                    }
                    Text {
                        text: root.mediaSubtitle
                        visible: text.length > 0 && !tight
                        color: theme.inkDim
                        font.family: theme.hud
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, chrome.width * 0.4)
                    }
                }
            }

            // Live wall clock (2026-07-20, Hemanth: "Ends" needs a now to anchor to). Sits
            // left of the window verbs; the one place the player tells you the actual time.
            Text {
                id: nowClockLabel
                anchors.right: titleBarVerbs.left
                anchors.rightMargin: tight ? 14 : 20
                anchors.verticalCenter: titleBarVerbs.verticalCenter
                visible: !tight && root.nowClock.length > 0
                text: root.nowClock
                color: theme.inkDim
                font.family: theme.hud; font.pixelSize: 13; font.letterSpacing: 0.5
                font.features: ({ "tnum": 1 })
                style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
            }

            Row {
                id: titleBarVerbs
                anchors.right: parent.right
                anchors.rightMargin: tight ? 12 : 20
                anchors.top: parent.top
                anchors.topMargin: tight ? 10 : 16
                spacing: 6
                RoundButton {
                    size: 38
                    icon: "minimizeToBar"
                    tooltip: "Minimize — paused in the taskbar, resumes with no reload"
                    onClicked: {
                        root.closeMenus()
                        root.minimizeRequested()
                    }
                }
                RoundButton {
                    size: 38
                    icon: "cancel"
                    tooltip: "Close"
                    onClicked: {
                        var playing = root.fileReady && !mpv.pause
                        root.closeMenus()
                        if (playing) { root.closeConfirmOpen = true; root.wakeChrome() }
                        else root.closeRequested()
                    }
                }
            }
        }


        // Resume choice overlay — first-load only. Real labeled buttons (no PlayerActionButton
        // component exists; RoundButton is icon-only). [Feature 3 / review adaptation 2026-07-07]
        Rectangle {
            id: resumeChoicePanel
            visible: root.resumeChoiceOpen
            z: 30
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 380)
            height: resumeChoiceColumn.implicitHeight + 36
            radius: 10
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            // swallow body clicks so the fullscreen tap-catcher can't dismiss it
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }

            Column {
                id: resumeChoiceColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Text {
                    width: parent.width
                    text: "Resume from " + root.fmtTime(root.resumeChoiceSec)
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: "Pick up where you left off, or start this video from the beginning."
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 10
                    ResumeChoiceButton {
                        objectName: "playerResumeChoiceResume"
                        label: "Resume"
                        primary: true
                        onClicked: root.acceptResumeChoice()
                    }
                    ResumeChoiceButton {
                        objectName: "playerResumeChoiceStartOver"
                        label: "Start over"
                        primary: false
                        onClicked: root.startOverFromResumeChoice()
                    }
                }
            }
        }

        Rectangle {
            id: closeConfirmPanel
            visible: root.closeConfirmOpen
            z: 10
            anchors.centerIn: parent
            width: 380
            height: 150
            radius: 18
            color: Qt.rgba(0.04, 0.05, 0.07, 0.95)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            // Absorb background clicks (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

            Text {
                x: 18
                y: 16
                text: "End this session?"
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                x: 18
                y: 46
                width: parent.width - 36
                text: "Your spot stays in Continue Watching."
                color: theme.inkDim
                font.family: theme.hud
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: 14
                spacing: 8
                Rectangle {
                    width: 130
                    height: 34
                    radius: 9
                    color: keepArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.14)
                    Text {
                        anchors.centerIn: parent
                        text: "Keep watching"
                        color: theme.ink
                        font.family: theme.hud
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: keepArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeConfirmOpen = false
                    }
                }
                Rectangle {
                    width: 110
                    height: 34
                    radius: 9
                    color: endArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.12)
                    border.width: 1
                    border.color: theme.gold
                    Text {
                        anchors.centerIn: parent
                        text: "End session"
                        color: theme.ink
                        font.family: theme.hud
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: endArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.closeConfirmOpen = false
                            root.closeRequested()
                        }
                    }
                }
            }
        }

        Rectangle {
            id: overflowPanel
            visible: root.overflowOpen
            // Fades rather than snapping in (2026-07-30 restyle, carried from the Player 2 menu).
            opacity: root.overflowOpen ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 110 } }
            z: 9
            width: 300
            height: overflowColumn.implicitHeight + 30
            radius: 14
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            // Context menu (2026-07-18): opens at the right-click cursor, clamped to the
            // window like every OS menu — the ⋯ button (and its tail) is gone from the bar.
            onVisibleChanged: if (visible) {
                x = root.clamp(root.contextMenuPos.x, 10, chrome.width - width - 10)
                y = root.clamp(root.contextMenuPos.y, 10, chrome.height - height - 10)
            }

            // Absorb background clicks so the panel body never dismisses itself (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

            Column {
                id: overflowColumn
                x: 16
                y: 15
                width: parent.width - 32
                // Rows sit flush and light up on hover instead of each being its own grey slab with
                // a 10px gutter. That gutter plus a permanent fill is what made this menu read flat.
                spacing: 2

                Text {
                    text: "More controls"
                    color: theme.inkDimmer
                    font.family: theme.hud
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    bottomPadding: 4
                }

                VolumeControl { visible: root.tight }

                Repeater {
                    model: [
                        { "label": "Screenshot", "kind": "screenshot", "when": true },
                        { "label": root.gifState === "recording" ? "Stop GIF" : "Record GIF", "kind": "gif", "when": true },
                        { "label": "Playback stats", "kind": "stats", "when": true },
                        { "label": "Loudness · " + root.loudnessLabel(), "kind": "loudness", "when": true },
                        { "label": "Live guide", "kind": "liveGuide", "when": (typeof Live !== "undefined" && Live.isLive) },
                        { "label": "DVR record", "kind": "dvr", "when": (typeof Live !== "undefined" && Live.isLive) },
                        { "label": "Jump to live edge", "kind": "liveEdge", "when": (typeof Live !== "undefined" && Live.isLive) },
                        { "label": "Episodes & sources", "kind": "browser", "when": root.barTiny && root.mediaId.indexOf("iptv:") !== 0 },
                        { "label": "Pick another stream", "kind": "stream", "when": root.barSnug && root.streamCandidates.length > 1 },
                        { "label": "Download", "kind": "download", "when": root.barSnug && root.currentCastUrl().length > 0 },
                        { "label": "Audio tracks", "kind": "audio", "when": root.barTiny },
                        { "label": "Speed", "kind": "speed", "when": root.barTiny },
                        { "label": "Aspect ratio", "kind": "fill", "when": root.barSnug },
                        // The two rows Player 2's menu carried and this one never did. Both features
                        // already existed here (WindowMode.enterPip/exitPip, root.shortcutsOpen) --
                        // they were simply never offered anywhere in this menu.
                        { "label": root.pipMode ? "Exit picture-in-picture" : "Picture-in-picture",
                          "kind": "pip", "when": true },
                        { "label": "Keyboard shortcuts", "kind": "shortcuts", "when": true }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property string rowValue: root.overflowValue(modelData.kind)
                        visible: modelData.when
                        width: overflowColumn.width
                        height: 38
                        radius: 8
                        // Transparent at rest, lit on hover -- the row reads as a target rather than
                        // as a permanent grey slab. Matches the Player 2 menu Hemanth preferred.
                        color: rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            width: parent.width - x - (valueText.width > 0 ? valueText.width + 22 : 12)
                            elide: Text.ElideRight
                            text: modelData.label
                            color: theme.ink
                            font.family: theme.hud
                            font.pixelSize: 13
                        }
                        // What this control is currently set to. Gold, matching the seek bar and
                        // volume accent, so the menu carries information and not only verbs.
                        Text {
                            id: valueText
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.rowValue
                            visible: text.length > 0
                            color: theme.gold
                            font.family: theme.hud
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var kind = modelData.kind
                                // Loudness cycles in place (menu stays open) so you can see
                                // the mode change and hear it apply live.
                                if (kind === "loudness") { root.cycleLoudness(); root.wakeChrome(); return }
                                root.closeMenus()
                                if (kind === "shortcuts") { root.shortcutsOpen = true; return }
                                if (kind === "pip") {
                                    // Real toggle, not a decoration: WindowMode owns the PiP surface
                                    // and reports back through onPipEntered/onPipExited.
                                    if (typeof WindowMode === "undefined") return
                                    var w = root.Window.window
                                    if (!w) return
                                    if (root.pipMode) WindowMode.exitPip(w)
                                    else WindowMode.enterPip(w)
                                    return
                                }
                                if (kind === "audio") audioMenu.panelOpen = true
                                else if (kind === "speed") speedMenu.panelOpen = true
                                else if (kind === "fill") fillMenu.panelOpen = true
                                else if (kind === "browser") root.browserOpen = true
                                else if (kind === "stream") root.pickAnotherStream()
                                else if (kind === "download") root.handleDownloadAction()
                                else if (kind === "screenshot") root.captureFrameGrab()
                                else if (kind === "gif") {
                                    if (root.gifState === "recording") root.stopGifRecording()
                                    else if (root.gifState === "idle") root.startGifRecording()
                                }
                                else if (kind === "stats") {
                                    root.statsOverlayOpen = !root.statsOverlayOpen
                                    if (root.statsOverlayOpen) root.refreshPlaybackStats()
                                }
                                else if (kind === "liveGuide") {
                                    if (!root.liveGuideOpen) root.openLiveGuide(); else root.liveGuideOpen = false
                                }
                                else if (kind === "dvr") root.dvrPanelOpen = !root.dvrPanelOpen
                                else if (kind === "liveEdge") root.goLiveEdge()
                                root.wakeChrome()
                            }
                        }
                    }
                }
            }
        }



        Rectangle {
            id: liveGuide
            visible: root.liveGuideOpen
            z: 11
            anchors.fill: parent
            color: Qt.rgba(4 / 255, 6 / 255, 10 / 255, 0.96)

            // Absorb background clicks (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 92
                color: Qt.rgba(0.04, 0.05, 0.07, 0.96)
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
                        font.family: theme.hud
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: (typeof Live !== "undefined" && Live.activeChannel.name) ? Live.activeChannel.name : "Live channel"
                        color: theme.inkDim
                        font.family: theme.hud
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
                        font.family: theme.hud
                        font.pixelSize: 14
                        clip: true
                        onTextChanged: if (typeof Live !== "undefined") Live.setQuery(text)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Search channels"
                            visible: liveSearch.text.length === 0
                            color: theme.inkDimmer
                            font.family: theme.hud
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
                        font.family: theme.hud
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
                        font.family: theme.hud
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
            color: Qt.rgba(0.04, 0.05, 0.07, 0.96)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            // Absorb background clicks (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

            Text {
                x: 24
                y: 22
                text: "DVR record"
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                x: 24
                y: 52
                width: parent.width - 48
                text: (typeof Live !== "undefined" && Live.activeChannel.name) ? Live.activeChannel.name : "Live channel"
                color: theme.inkDim
                font.family: theme.hud
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Text {
                x: 24
                y: 72
                width: parent.width - 48
                text: (typeof Live !== "undefined" && Live.defaultRecordingDir) ? Live.defaultRecordingDir : ""
                color: theme.inkDim
                font.family: theme.hud
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
                        font.family: theme.hud
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
                        font.family: theme.hud
                        font.pixelSize: 12
                    }
                    Text {
                        x: 14
                        y: 50
                        width: parent.width - 28
                        text: modelData.error || modelData.outputPath || ""
                        color: modelData.error ? theme.danger : theme.inkDim
                        font.family: theme.hud
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
            color: Qt.rgba(0.04, 0.05, 0.07, root.controlsShown ? 0.82 : 0.46)
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
                    font.family: theme.hud
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.abLoopA >= 0 ? root.fmtTime(root.abLoopA) : "--:--")
                          + " -> "
                          + (root.abLoopB >= 0 ? root.fmtTime(root.abLoopB) : "--:--")
                    color: theme.inkDim
                    font.family: theme.hud; font.features: ({ "tnum": 1 })
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
            color: Qt.rgba(0.04, 0.05, 0.07, 0.86)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            // Absorb background clicks so tapping the stats card never toggles pause (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }

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
                    font.family: theme.hud
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
                            font.family: theme.hud
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width * 0.54
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.statsValue(modelData)
                            color: theme.ink
                            font.family: theme.hud; font.features: ({ "tnum": 1 })
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                        }
                    }
                }
            }
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
                                        : Qt.rgba(0.04, 0.05, 0.07, 0.88)
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
                    font.family: theme.hud
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: !root.frameGrabFailed && root.frameGrabPath.length > 0
                    text: "Open folder"
                    color: theme.gold
                    font.family: theme.hud
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
                                                : Qt.rgba(0.04, 0.05, 0.07, 0.88)
            border.width: 1
            border.color: root.subtitleDropToastFailed ? Qt.rgba(1, 0.35, 0.35, 0.40)
                                                       : Qt.rgba(1, 1, 1, 0.18)
            Text {
                id: subtitleDropToastText
                anchors.centerIn: parent
                text: root.subtitleDropToastText
                color: theme.ink
                font.family: theme.hud
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
            color: Qt.rgba(0.04, 0.05, 0.07, 0.88)
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
                    font.family: theme.hud
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: root.gifState === "recording"
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Stop"
                    color: theme.gold
                    font.family: theme.hud
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
                    font.family: theme.hud
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

        // Feature 8: in-player episode & source browser, opened with 'E' or the control-bar
        // button. Declared before the ? sheet so the shortcuts overlay stays on top.
        BrowserDrawer {
            id: browserDrawer
            open: root.browserOpen
            queue: root.playbackQueue
            queueIndex: root.playbackQueueIndex
            nowId: root.mediaId
            mediaTitle: root.mediaTitle
            mediaYear: root.mediaYear
            backdropUrl: root.mediaArt
            subType: root.subStreamType.length ? root.subStreamType : "series"
            candidates: root.streamCandidates
            currentStreamIndex: root.currentStreamIndex
            isDead: function(i) { return root.isStreamDead(i) }
            onDismissed: root.browserOpen = false
            onSourcePicked: function(index) {
                root.browserOpen = false
                root.playStreamAt(index, "switch")
            }
            onEpisodePicked: function(target) {
                root.browserOpen = false
                root.jumpToEpisode(target, "Loading episode...", "this episode.")
            }
        }

        // Feature 7: keyboard shortcuts reference, opened with '?'.
        ShortcutsSheet {
            id: shortcutsSheet
            open: root.shortcutsOpen
            groups: PlayerHotkeys.groups()
            onDismissed: root.shortcutsOpen = false
        }

        // Feature 4 skip pill: appears while playback sits inside an intro/recap/credits
        // segment. Monochrome, dismissible, and yields to the Up Next card.
        Rectangle {
            id: skipPill
            property var activeSkip: root.currentSkipSegment()
            visible: !!activeSkip && root.controlsShown && !root.upNextVisible
            z: 31
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 176
            width: skipPillRow.implicitWidth + 28
            height: 42
            radius: 8
            color: Qt.rgba(0, 0, 0, 0.82)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)

            Row {
                id: skipPillRow
                anchors.centerIn: parent
                spacing: 10
                Text {
                    text: root.skipLabel(skipPill.activeSkip)
                    color: theme.ink
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Skip"
                    color: theme.inkDim
                    font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.performSegmentSkip(skipPill.activeSkip)
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
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
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
                            font.family: theme.hud
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.upNextTitle()
                            color: theme.ink
                            font.family: theme.hud
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
                            font.family: theme.hud
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
                            font.family: theme.hud
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

        // ── Pause info card (Tier 2, 2026-07-20): on pause, the player tells you what
        //    you're watching. Glass card, lower-left, above the dock. No buttons — it is
        //    information, not a menu. NO episode rating (Hemanth veto). ──
        Rectangle {
            id: pauseCard
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.barTiny ? 28 : 40
            anchors.bottomMargin: (root.tight ? 116 : 126) + 26
            width: Math.min(root.width - 80, 520)
            height: pauseCardCol.implicitHeight + 40
            radius: 12
            color: Qt.rgba(0.04, 0.04, 0.05, 0.72)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.09)
            visible: opacity > 0.01
            opacity: root.pauseCardShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            transform: Translate {
                y: root.pauseCardShown ? 0 : 10
                Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            }

            Column {
                id: pauseCardCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                spacing: 9

                // Show wordmark: the fetched logo art when we have it, else the title
                // in letterspaced caps (same fallback the loader uses).
                Image {
                    id: pauseCardLogo
                    visible: root.mediaLogo.length > 0 && status === Image.Ready
                    source: root.mediaLogo
                    fillMode: Image.PreserveAspectFit
                    height: 46
                    width: Math.min(implicitWidth, parent.width)
                    asynchronous: true
                }
                Text {
                    visible: !pauseCardLogo.visible
                    width: parent.width
                    text: root.mediaTitle
                    color: theme.ink
                    font.family: theme.hud; font.pixelSize: 26; font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase; font.letterSpacing: 4
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: root.pauseFactsLine()
                    visible: text.length > 0
                    color: theme.inkDim
                    font.family: theme.hud; font.pixelSize: 12; font.features: ({ "tnum": 1 })
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.pauseQualityLine()
                    visible: text.length > 0
                    color: theme.inkDimmer
                    font.family: theme.hud; font.pixelSize: 11; font.letterSpacing: 2
                    font.features: ({ "tnum": 1 })
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.mediaPlot
                    visible: text.length > 0
                    topPadding: 4
                    color: theme.inkDim
                    font.family: theme.hud; font.pixelSize: 12
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
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
                //
                // Native chrome (spec 2026-07-08): the dock is FUSED to the bottom edge —
                // a KDE-Plasma panel in the house glass, not floating Harbor furniture.
                // Top hairline only; the panel "breathes" (slides down 8px) as chrome hides.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 0
            anchors.rightMargin: 0
            anchors.bottomMargin: 0
            height: tight ? 116 : 126
            radius: 0
            color: "transparent"
            border.width: 0

            // Harbor-parity bottom overlay: a restrained transparent->black scrim (bottom darkest)
            // instead of the old fused glass panel + hairline. Gold stays timeline/focus only.
            Rectangle {
                id: playerBottomScrim
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0;  color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.45) }
                    GradientStop { position: 1.0;  color: Qt.rgba(0, 0, 0, 0.85) }
                }
            }

            transform: Translate {
                id: panelBreath
                y: root.controlsShown && !root.starting ? 0 : 8
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }

            // Glanceable state row (Tier 1, 2026-07-20): rides just above the timeline,
            // over the video (the dock doesn't clip — same escape hatch the popovers use).
            // Left = the player's state (only when it has something to say); right = the
            // wall-clock finish time. Fades with the chrome; silent in plain playback.
            Item {
                id: stateRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: seekRow.top
                anchors.bottomMargin: 3
                anchors.leftMargin: tight ? 16 : 20
                anchors.rightMargin: tight ? 16 : 20
                height: 22
                visible: root.stateLineText.length > 0 || root.endsAtClock.length > 0
                Text {
                    id: stateLineLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stateLineText
                    visible: text.length > 0
                    color: theme.ink
                    font.family: theme.hud; font.pixelSize: 14; font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.endsAtClock.length > 0 ? "ENDS  " + root.endsAtClock : ""
                    visible: root.endsAtClock.length > 0
                    color: theme.inkDim
                    font.family: theme.hud; font.pixelSize: 12; font.letterSpacing: 1.5
                    font.features: ({ "tnum": 1 })
                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
                }
            }

            Row {
                id: seekRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: tight ? 14 : 18
                anchors.rightMargin: tight ? 14 : 18
                anchors.topMargin: 10
                height: 42
                spacing: 12

                Text {
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(root.displayPosition())
                    color: theme.ink
                    font.family: theme.hud; font.features: ({ "tnum": 1 })
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
                        height: seekBar.hovered || root.seeking ? 5 : 3
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }
                    Rectangle {
                        // cacheTime is an ABSOLUTE cache-end timestamp (not a fraction);
                        // 0.0 when the stream doesn't report it -> width 0 -> no fill, by design.
                        // Local (downloaded) playback: hidden entirely — the strip is a streaming
                        // affordance, and mpv's forced 60s disk read-ahead would paint phantom
                        // "buffering" over a file that is fully on disk (Hemanth eyes-on 2026-07-20).
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: mpv.duration > 0 && isFinite(mpv.cacheTime) ? parent.width * root.clamp(mpv.cacheTime / mpv.duration, 0, 1) : 0
                        height: seekBar.hovered || root.seeking ? 5 : 3
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, 0.30)
                        visible: width > 2 && root.mediaLocalPath.length === 0
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * root.seekFraction()
                        height: seekBar.hovered || root.seeking ? 5 : 3
                        radius: height / 2
                        color: theme.gold
                    }
                    Repeater {
                        model: root.skipSegments
                        Rectangle {
                            visible: mpv.duration > 0
                            x: seekBar.width * root.clamp(modelData.startSec / mpv.duration, 0, 1)
                            width: Math.max(2, seekBar.width * root.clamp((modelData.endSec - modelData.startSec) / mpv.duration, 0, 1))
                            anchors.verticalCenter: parent.verticalCenter
                            height: seekBar.hovered || root.seeking ? 5 : 3
                            radius: 2
                            color: Qt.rgba(1, 1, 1, 0.34)
                        }
                    }
                    Repeater {
                        // Jellyfin-style chapter marks: a notch at each chapter start (F4's
                        // mpv.chapters data), slightly taller than the bar so it reads at rest.
                        model: mpv.chapters
                        Rectangle {
                            required property var modelData
                            visible: mpv.duration > 0 && (modelData.startSec || 0) > 1
                            x: seekBar.width * root.clamp((modelData.startSec || 0) / mpv.duration, 0, 1) - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: seekBar.hovered || root.seeking ? 8 : 5
                            radius: 1
                            color: Qt.rgba(1, 1, 1, 0.55)
                        }
                    }
                    Rectangle {
                        x: parent.width * root.seekFraction() - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.seeking || root.seekSettling ? 14 : 11
                        height: width
                        radius: width / 2
                        color: theme.gold
                        border.width: 1
                        border.color: Qt.rgba(0, 0, 0, 0.32)
                        visible: mpv.duration > 0
                    }
                    Rectangle {
                        // F9: timestamp-only until the FIRST frame lands, then the card stays
                        // grown and the picture swaps in place as frames arrive — never
                        // collapsing between buckets (that collapse read as "shaky").
                        readonly property bool hasThumb: root.hoverThumbUrl !== ""
                        readonly property string chapterTitle: root.chapterHoverLabel(root.seekPreview)
                        readonly property bool hasChapter: chapterTitle !== ""
                        visible: seekBar.hovered && !root.seeking && mpv.duration > 0
                        x: root.clamp(seekHover.mouseX - width / 2, 0, parent.width - width)
                        y: -(height + 2)
                        width: hasThumb ? 216
                                        : Math.min(240, Math.max(previewText.implicitWidth, chapterText.implicitWidth) + 16)
                        height: (hasThumb ? previewThumb.height + 36 : 28) + (hasChapter ? 17 : 0)
                        radius: 7
                        color: Qt.rgba(0, 0, 0, 0.86)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.10)
                        Image {
                            id: previewThumb
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.topMargin: 8
                            width: 200
                            height: Math.round(width * (sourceSize.height > 0 ? sourceSize.height / sourceSize.width : 9 / 16))
                            visible: parent.hasThumb
                            source: parent.hasThumb ? root.hoverThumbUrl : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        Text {
                            id: chapterText
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: previewText.top
                            anchors.bottomMargin: 1
                            visible: parent.hasChapter
                            text: parent.chapterTitle
                            width: Math.min(implicitWidth, parent.width - 16)
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: Qt.rgba(1, 1, 1, 0.72)
                            font.family: theme.hud
                            font.pixelSize: 11
                        }
                        Text {
                            id: previewText
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: parent.hasThumb || parent.hasChapter ? 7 : (parent.height - implicitHeight) / 2
                            text: root.fmtTime(root.seekPreview)
                            color: theme.ink
                            font.family: theme.hud; font.features: ({ "tnum": 1 })
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
                        onEntered: { seekBar.hovered = true; root.wakeChrome(); root.requestSeekThumb() }
                        onExited: {
                            seekBar.hovered = false
                            if (!root.seeking)
                                root.seekPreview = mpv.position
                        }
                        onPositionChanged: {
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                            root.requestSeekThumb()
                        }
                        onPressed: {
                            root.seeking = true
                            root.seekPreview = root.previewAt(mouseX, width)
                            root.wakeChrome()
                        }
                        onReleased: {
                            root.requestUserSeekTo(root.seekPreview)
                            root.seeking = false
                        }
                    }
                }

                Text {
                    id: durationLabel
                    width: tight ? 0 : 58
                    visible: !tight
                    anchors.verticalCenter: parent.verticalCenter
                    // Click to flip total <-> remaining (Tier 1, 2026-07-20). Remaining is
                    // speed-aware wall time to the end from the current position.
                    text: root.showRemaining
                          ? "-" + root.fmtTime(Math.max(0, (mpv.duration - root.displayPosition())
                                / ((mpv.speed && mpv.speed > 0.05) ? mpv.speed : 1)))
                          : root.fmtTime(mpv.duration)
                    color: durationMa.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.hud; font.features: ({ "tnum": 1 })
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignRight
                    MouseArea {
                        id: durationMa
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showRemaining = !root.showRemaining
                    }
                }
            }

            Item {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: tight ? 14 : 18
                anchors.rightMargin: tight ? 14 : 18
                anchors.bottomMargin: 8
                height: tight ? 56 : 64

                // Utility icons live LEFT of the playback arrows, chips live right — the
                // two sides stay apart and nothing may crowd the transport (Hemanth
                // 2026-07-08: "they should stay apart, on either side of the playback
                // arrows"). Fold rules unchanged: stream/download still duck into the
                // overflow panel at barSnug.
                Row {
                    id: leftUtilityRow
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    VolumeControl {
                        id: volumeControl
                        visible: !tight
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    RoundButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.errored || root.starting
                        size: 48
                        icon: "retry"
                        tooltip: "Retry stream"
                        onClicked: root.retryCurrentStream()
                    }

                    RoundButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.streamCandidates.length > 1 && !root.barSnug
                        size: 48
                        icon: "stream"
                        tooltip: "Pick another stream"
                        onClicked: root.pickAnotherStream()
                    }

                    RoundButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.currentCastUrl().length > 0 && !root.barSnug
                        size: 48
                        icon: root.downloadIcon()
                        active: typeof Download !== "undefined" && Download.status.kind !== "idle"
                        label: (typeof Download !== "undefined" && Download.status.kind === "downloading")
                               ? Math.round((Download.status.ratio || 0) * 100)
                               : ""
                        tooltip: root.downloadTooltip()
                        onClicked: root.handleDownloadAction()
                    }

                    FillMenuButton {
                        id: fillMenu
                        // Main HUD icon (Hemanth 2026-07-18), LEFT cluster: the right side
                        // is ~700px of text chips and collided (his screenshot); the left
                        // has the free room and its budget absorbs one more 48px button
                        // under the snug threshold. Folds to the overflow at snug, like
                        // stream/download beside it. (Harbor ships this control hidden by
                        // default in bottom-right — no precedent worth colliding for.)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !root.barSnug || fillMenu.panelOpen
                    }
                }

                Row {
                    id: transportRow
                    anchors.centerIn: parent
                    spacing: 8
                    RoundButton {
                        visible: root.hasAdjacentEpisode("prev")
                        size: tight ? 38 : 40
                        icon: "prevEpisode"
                        tooltip: "Previous episode"
                        onClicked: root.goToAdjacentEpisode("prev")
                    }
                    RoundButton {
                        size: tight ? 38 : 40
                        icon: "seekBack"
                        label: root.seekBackSeconds
                        tooltip: "Back " + root.seekBackSeconds + "s"
                        onClicked: root.requestUserSeekStep(-root.seekBackSeconds)
                    }
                    RoundButton {
                        objectName: "playerPlayPauseButton"
                        size: tight ? 46 : 48
                        icon: mpv.pause ? "play" : "pause"
                        hero: true
                        tooltip: mpv.pause ? "Play" : "Pause"
                        onClicked: root.togglePlayPause()
                    }
                    RoundButton {
                        size: tight ? 38 : 40
                        icon: "seekForward"
                        label: root.seekForwardSeconds
                        tooltip: "Forward " + root.seekForwardSeconds + "s"
                        onClicked: root.requestUserSeekStep(root.seekForwardSeconds)
                    }
                    RoundButton {
                        visible: root.hasAdjacentEpisode("next")
                        size: tight ? 38 : 40
                        icon: "nextEpisode"
                        tooltip: "Next episode"
                        onClicked: root.goToAdjacentEpisode("next")
                    }
                }

                Row {
                    id: playerRightControlCluster
                    objectName: "playerRightControlCluster"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // retry/stream/download moved to leftUtilityRow (2026-07-08) — the
                    // right side is the chip cluster's alone.

                    RoundButton {
                        visible: root.mediaId.indexOf("iptv:") !== 0 && !root.barTiny
                        anchors.verticalCenter: parent.verticalCenter
                        size: 40
                        icon: "episodes"
                        active: root.browserOpen
                        tooltip: "Episodes & sources (E)"
                        onClicked: {
                            var wasOpen = root.browserOpen
                            root.closeMenus()
                            root.browserOpen = !wasOpen
                            root.wakeChrome()
                        }
                    }

                    WatchPartyPanel {
                        id: watchPartyMenu
                        anchors.verticalCenter: parent.verticalCenter
                        overlayParent: chrome
                        controller: typeof WatchPartyUi !== "undefined" ? WatchPartyUi : null
                        syncController: typeof WatchPartySync !== "undefined" ? WatchPartySync : null
                        sourceInfo: root.watchPartySource
                        sourceCandidate: root.currentStreamCandidate()
                        localSourceMatches: root.watchPartySourceMatchesRoom
                        onToggleRequested: function(wasOpen) {
                            root.closeMenus()
                            watchPartyMenu.panelOpen = !wasOpen
                            root.wakeChrome()
                        }
                        onCopyRequested: function(text) {
                            if (typeof Clipboard !== "undefined")
                                Clipboard.copy(text)
                        }
                    }

                    AudioMenu {
                        id: audioMenu
                        anchors.verticalCenter: parent.verticalCenter
                        overlayParent: chrome
                        visible: !root.barTiny || audioMenu.panelOpen
                        onToggleRequested: function(wasOpen) {
                            root.closeMenus()
                            audioMenu.panelOpen = !wasOpen
                            root.wakeChrome()
                        }
                        icon: "audio"
                        title: "Audio"
                        chipValue: root.audioChipValue
                        count: mpv.audioTracks.length
                        panelWidth: 360
                        panelHeight: Math.min(310, 86 + Math.max(1, mpv.audioTracks.length) * 48 + 42)
                        delegateModel: root.audioRows
                        selectedId: mpv.audioTrack
                        emptyText: "No alternate audio tracks in this file."
                        syncValue: mpv.audioDelay
                        onTrackPicked: function(trackId) { root.pickAudioTrack(trackId) }
                        onDelayStep: function(delta) { root.adjustAudioDelay(delta) }
                        onResetDelay: { mpv.audioDelay = 0; root.saveTrackPreference({ "audioDelay": 0 }) }
                    }

                    SubtitleMenu {
                        id: subMenu
                        anchors.verticalCenter: parent.verticalCenter
                        overlayParent: chrome
                        onToggleRequested: function(wasOpen) {
                            root.closeMenus()
                            subMenu.panelOpen = !wasOpen
                            root.wakeChrome()
                        }
                        icon: "subtitle"
                        title: "Subtitles"
                        chipValue: root.subsChipValue
                        // Combined: embedded/loaded mpv tracks + online subs (OpenSubtitles).
                        count: root.subRows.length
                        panelWidth: 380
                        panelHeight: Math.min(360, 124 + Math.max(1, root.subRows.length) * 48 + 42)
                        delegateModel: root.subRows
                        selectedId: mpv.subtitleTrack
                        searchType: root.subtitleSearchMeta.type
                        searchId: root.subtitleSearchMeta.imdbId.length ? root.subStreamId : ""
                        autoStatusText: root.subtitleAutoStatusText()
                        emptyText: root.subsLoading ? "Finding subtitles…" : "No subtitles found for this title."
                        offRow: true
                        syncValue: mpv.subDelay
                        active: mpv.subtitleTrack !== ""
                        onTrackPicked: function(trackId) { root.pickSubtitleTrack(trackId) }
                        onOffPicked: root.turnSubtitlesOff()
                        onDelayStep: function(delta) { root.adjustSubtitleDelay(delta) }
                        onResetDelay: root.resetSubtitleDelay()
                        onStyleRequested: {
                            subStyleBar.open = !subStyleBar.open
                            root.wakeChrome()
                        }
                        onFileLoaded: function(fileUrl) { root.loadSubtitleFile(fileUrl) }
                        onOnlinePicked: function(fileUrl, title, lang) { root.addOnlineSubtitle(fileUrl, title, lang) }
                    }

                    SpeedMenuButton {
                        id: speedMenu
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !root.barTiny || speedMenu.panelOpen
                    }

                    // Fullscreen — relocated here from the top window group so it exists ONCE,
                    // in the bottom-right transport (approved 2026-07-19). Keeps the F11 toggle.
                    RoundButton {
                        id: bottomFullscreenButton
                        anchors.verticalCenter: parent.verticalCenter
                        size: 40
                        icon: root.shellWindowed ? "fullscreen" : "fullscreenExit"
                        tooltip: root.shellWindowed ? "Enter fullscreen (F11)" : "Exit fullscreen (F11)"
                        onClicked: root.fullscreenRequested()
                    }

                    // More-controls (and the narrow-bar folds) live on the video's
                    // RIGHT-CLICK menu now (Hemanth 2026-07-18) — no ⋯ button on the bar.

                    // Window verbs (minimize · close) moved to the titlebar (native chrome
                    // spec 2026-07-08) — one home for them, like every OS. See id: titleBarVerbs.
                }
            }
        }
        }

    }

    // Resume choice overlay button — plain labeled button (RoundButton is icon-only). [Feature 3]
    component ResumeChoiceButton: Rectangle {
        id: rcb
        property string label: ""
        property bool primary: false
        signal clicked()
        width: Math.max(96, rcbText.implicitWidth + 28)
        height: 38
        radius: 8
        color: rcb.primary ? theme.gold
                           : (rcbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.08))
        Text {
            id: rcbText
            anchors.centerIn: parent
            text: rcb.label
            color: rcb.primary ? "#111111" : theme.ink
            font.family: theme.hud
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: rcbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rcb.clicked()
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
            font.family: theme.hud
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
            // hero (play/pause) = solid white circle with a dark glyph, per the approved mock;
            // everything else stays a translucent white hover chip. Gold is timeline/focus only.
            color: rb.hero ? (press.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.92))
                           : rb.active ? Qt.rgba(1, 1, 1, 0.16)
                           : press.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
            border.width: rb.active ? 1 : 0
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }
        // Lucide glyphs: seek kinds get the composed rotate+numeral, everything else one SVG glyph.
        PlayerIcon {
            anchors.fill: parent
            visible: rb.icon !== "seekBack" && rb.icon !== "seekForward"
            kind: rb.icon
            ink: rb.hero ? "#101014" : (rb.active ? theme.gold : theme.ink)
            accessibleName: rb.tooltip
        }
        PlayerSeekIcon {
            anchors.fill: parent
            visible: rb.icon === "seekBack" || rb.icon === "seekForward"
            forward: rb.icon === "seekForward"
            seconds: (rb.label && rb.label.length) ? parseInt(rb.label) : 10
            ink: rb.hero ? "#101014" : (rb.active ? theme.gold : theme.ink)
            hudFamily: theme.hud
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
        width: 190
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
                color: theme.gold
            }
            Rectangle {
                x: parent.width * root.volumeFraction() - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: theme.gold
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
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Text {
                id: menuTitle
                x: 18
                y: 15
                text: menu.title
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: menuTitle.right
                anchors.leftMargin: 8
                anchors.verticalCenter: menuTitle.verticalCenter
                text: menu.count
                color: theme.inkDimmer
                font.family: theme.hud
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
                    font.family: theme.hud
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
                        font.family: theme.hud
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
                        font.family: theme.hud
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
                font.family: theme.hud
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
                    font.family: theme.hud
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                DelayButton { text: "-0.1"; onClicked: menu.delayStep(-0.1) }
                Text {
                    width: 88
                    anchors.verticalCenter: parent.verticalCenter
                    text: (menu.syncValue >= 0 ? "+" : "") + menu.syncValue.toFixed(2) + "s"
                    color: theme.ink
                    font.family: theme.hud; font.features: ({ "tnum": 1 })
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
        property bool nonDefault: Math.abs(mpv.speed - 1) > 0.001
        width: 40
        height: 40
        RoundButton {
            anchors.fill: parent
            size: 40
            icon: "speed"
            active: sm.panelOpen || sm.nonDefault
            tooltip: "Speed & sleep"
            onClicked: {
                var wasOpen = sm.panelOpen
                root.closeMenus()
                sm.panelOpen = !wasOpen
                root.wakeChrome()
            }
        }
        // Playback-speed VALUE shown ONLY when non-default (semantic audit) — small gold badge.
        Rectangle {
            visible: sm.nonDefault
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 1
            anchors.bottomMargin: 3
            width: spdVal.implicitWidth + 6
            height: 13
            radius: 6.5
            color: theme.gold
            Text {
                id: spdVal
                anchors.centerIn: parent
                text: (Math.round(mpv.speed * 100) / 100) + "×"
                color: "#101014"
                font.family: theme.hud
                font.features: ({ "tnum": 1 })
                font.pixelSize: 8
                font.weight: Font.DemiBold
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
            font.family: theme.hud; font.features: ({ "tnum": 1 })
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
            // Two speed/sleep columns, then a full-width "Skip step" footer section below both.
            readonly property int columnsHeight: 46 + Math.max(root.speedChoices.length, root.sleepPresets.length + (root.sleepTimerActive ? 1 : 0)) * 38
            readonly property int skipStepTop: columnsHeight + 8
            height: skipStepTop + 66
            onVisibleChanged: if (visible) {
                // Native chrome: rise centered on the chip, clamped to the window.
                var p = sm.mapToItem(chrome, 0, 0)
                x = root.clamp(p.x + sm.width / 2 - width / 2, 10, chrome.width - width - 10)
                y = p.y - height - 12
            }
            radius: 14
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)
            // Absorb background clicks (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }
            Rectangle {
                // appletTail — the Plasma pointer: this popover belongs to that chip.
                width: 8; height: 8
                rotation: 45
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom
                color: parent.color
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
            }
            // Harbor's exact section title (uppercase eyebrow).
            Text {
                x: 18
                y: 16
                text: "Playback speed"
                color: theme.inkDimmer
                font.family: theme.hud
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
                font.family: theme.hud
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.6
            }
            Rectangle {
                x: parent.width / 2
                y: 8
                width: 1
                height: parent.columnsHeight - 16
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
                        font.family: theme.hud
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
                        font.family: theme.hud
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
                        font.family: theme.hud
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
                        font.family: theme.hud; font.features: ({ "tnum": 1 })
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
                    font.family: theme.hud
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
            Rectangle {
                x: 18
                y: parent.columnsHeight
                width: parent.width - 36
                height: 1
                color: Qt.rgba(1, 1, 1, 0.10)
            }
            Text {
                x: 18
                y: parent.skipStepTop
                text: "Skip step"
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.6
            }
            Row {
                x: 18
                y: parent.skipStepTop + 22
                spacing: 6
                Repeater {
                    model: [5, 10, 30, 60]
                    delegate: Rectangle {
                        required property int modelData
                        width: 46
                        height: 28
                        radius: 7
                        color: playerSettings.seekStepSeconds === modelData ? Qt.rgba(1, 1, 1, 0.16) : (pillArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
                        border.width: 1
                        border.color: playerSettings.seekStepSeconds === modelData ? theme.gold : Qt.rgba(1, 1, 1, 0.10)
                        Text {
                            anchors.centerIn: parent
                            text: modelData + "s"
                            color: theme.ink
                            font.family: theme.hud
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: pillArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: playerSettings.seekStepSeconds = modelData
                        }
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
            tooltip: "Aspect ratio"
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
                x = root.clamp(p.x + fm.width / 2 - width / 2, 10, chrome.width - width - 10)
                y = p.y - height - 12
            }
            radius: 14
            color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)
            // Absorb background clicks (parity spec F2).
            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }
            Rectangle {
                width: 8; height: 8
                rotation: 45
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom
                color: parent.color
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
            }
            Text {
                x: 18
                y: 15
                text: "Video"
                color: theme.ink
                font.family: theme.hud
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
                        font.family: theme.hud
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
            font.family: theme.hud; font.features: ({ "tnum": 1 })
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

}
