import QtQuick
import "../TheatreApi.js" as TheatreApi
import "../EpisodeBrowser.js" as EpisodeBrowser
import "../AddonClient.js" as AddonClient
import "../Torrentio.js" as Torrentio
import "../Subtitles.js" as Subtitles
import "../SkipSegments.js" as SkipSegments

// The PRODUCTION host services for Player 2 — the same orchestration seam the lab's C++
// HarnessHostServices implements, but answering from the real app instead of fixtures.
//
// Why this is QML and not C++ (decision, Hemanth, 2026-07-25): every one of these eight policies —
// episode ordering, source ranking, subtitle wells, AniSkip — already exists in this app as the JS
// libraries imported above, and the shipped player calls exactly them. Re-deriving that in C++ would
// create a SECOND copy of the same policy that drifts from the first the next time an addon changes.
// So Player 2 asks the same brain. The engine (demux/decode/clock/audio/video) stays pure C++; only
// the "which episode / which source" orchestration lives here, which is precisely what the seam was
// defined to hold. See Player2HostServices.h for the contract this satisfies.
//
// The shell binds to this by duck-typing (`Connections { target: hostServices; ignoreUnknownSignals }`),
// so the method and SIGNAL NAMES BELOW ARE THE CONTRACT — they must match Player2HostServices.h exactly.
//
// Contract rule inherited from the C++ seam: every request resolves EXACTLY ONCE via its matching
// signal, carrying data, an empty collection, or {"error": "..."}. The one documented exception is
// downloadStateChanged, which is a state stream.
QtObject {
    id: host

    // ---- what the page feeds in (the identity/state the engine deliberately does not carry) ----
    property var playbackContext: ({})
    property string mediaTitle: ""
    property string mediaArt: ""
    property string subStreamType: ""      // "series" | "movie"
    property string subStreamId: ""        // the addon-facing id (may be mal:/kitsu:/tt…)
    property var streamCandidates: []      // normalized rows, production shape
    property int currentStreamIndex: -1
    property var deadStreamKeys: ({})
    property real durationSeconds: 0
    property var chapters: []              // from the Player 2 session, for chapter-derived skips
    property string currentPlaybackUrl: ""
    property string mediaResumeHash: ""
    property int mediaResumeFileIdx: 0
    property string mediaLocalPath: ""

    // ---- the seam (names must match Player2HostServices.h) ----
    signal adjacentEpisodeResolved(string mediaId, int direction, var episode)
    signal seasonEpisodesResolved(string mediaId, int season, var episodes)
    signal alternateSourcesResolved(string mediaId, var sources)
    signal onlineSubtitlesResolved(string mediaId, var subtitles)
    signal skipSegmentsResolved(string mediaId, var segments)
    signal downloadStateChanged(string mediaId, var state)
    signal metadataResolved(string mediaId, var metadata)

    // Cinemeta meta is fetched once per series and reused for every season pill + the plot line,
    // mirroring the drawer's one-fetch-per-session rule rather than re-hitting the network per pill.
    property var _metaCache: ({})
    // Generation guards: a season/source/subtitle answer that arrives after the viewer moved on is
    // dropped rather than resolving a stale request onto the current media.
    property int _generation: 0

    function invalidate() {
        host._generation += 1
        host._metaCache = ({})
    }

    // ---------------------------------------------------------------------------------------------
    // 1. Adjacent episode. Production precomputes prev/next into the playbackContext rather than
    //    asking anyone — mirrors PlayerPage.resolveAdjacentContext (qml/PlayerPage.qml:925).
    // ---------------------------------------------------------------------------------------------
    function requestAdjacentEpisode(mediaId, direction) {
        var ctx = host.playbackContext || ({})
        var target = null

        if (ctx.episodeQueue && ctx.episodeIndex !== undefined) {
            var queue = ctx.episodeQueue || []
            var idx = Number(ctx.episodeIndex)
            var wanted = idx + (direction < 0 ? -1 : 1)
            if (wanted >= 0 && wanted < queue.length)
                target = queue[wanted]
        } else {
            var adj = ctx.adjacentEpisodes || ({})
            target = (direction < 0) ? adj.prev : adj.next
        }

        // A series boundary is not an error — it is a real, resolvable answer meaning "nothing there".
        if (!target) {
            host.adjacentEpisodeResolved(mediaId, direction, { "dead": true })
            return
        }
        host.adjacentEpisodeResolved(mediaId, direction, host._episodeFromTarget(target))
    }

    // A production queue target ({type,id,title,backdrop,season,episode}) in the shape the shell reads.
    function _episodeFromTarget(target) {
        var t = target || ({})
        var id = String(t.id || "")
        var rec = host._progressFor(id)
        return {
            "mediaId": id,
            "title": String(t.title || ""),
            "season": Number(t.season || 0),
            "episode": Number(t.episode || 0),
            "durationSeconds": 0,          // production queue targets carry no duration
            "poster": String(t.backdrop || ""),
            "progressFraction": rec.frac,
            "watched": rec.watched
        }
    }

    // ---------------------------------------------------------------------------------------------
    // 2. Season episodes — the drawer's season pills. Same Cinemeta path the shipped drawer uses
    //    (qml/BrowserDrawer.qml:55), so the two players list identical episodes.
    // ---------------------------------------------------------------------------------------------
    function requestSeasonEpisodes(mediaId, season) {
        var rootId = EpisodeBrowser.seriesRootId(String(mediaId))
        var generation = host._generation

        function deliver(meta) {
            if (generation !== host._generation)
                return
            var rows = EpisodeBrowser.episodesFor((meta && meta.videos) || [], Number(season), rootId)
            var out = []
            for (var i = 0; i < rows.length; i++) {
                var r = rows[i]
                var rec = host._progressFor(r.id)
                out.push({
                    "mediaId": r.id,
                    "title": r.title,
                    "season": r.season,
                    "episode": r.num,
                    "durationSeconds": 0,
                    "poster": "",
                    "progressFraction": rec.frac,
                    "watched": rec.watched
                })
            }
            // The contract says a season list is never an error — an unknown season is simply empty.
            host.seasonEpisodesResolved(mediaId, season, out)
        }

        if (host._metaCache[rootId]) {
            deliver(host._metaCache[rootId])
            return
        }
        TheatreApi.setExtensions(Extensions.installed())
        TheatreApi.loadMeta("series", rootId, function(meta) {
            if (meta)
                host._metaCache[rootId] = meta
            deliver(meta)
        })
    }

    // ---------------------------------------------------------------------------------------------
    // 3. Alternate sources. The player already holds the ranked list the door handed it; the drawer
    //    shows those. Mirrors PlayerPage.normalizeStreamCandidates (qml/PlayerPage.qml:815) plus the
    //    session's dead/current marks, which live on the page, not on the row.
    // ---------------------------------------------------------------------------------------------
    function requestAlternateSources(mediaId) {
        var requestedId = String(mediaId || "")
        var rows = host.streamCandidates || []
        // Reuse the carried candidate list only for the exact media it belongs to. A request for a
        // different episode must resolve that episode instead of replaying the current one's rows.
        if (rows.length && (!String(host.subStreamId || "").length
                            || requestedId === String(host.subStreamId))) {
            host.alternateSourcesResolved(mediaId, host._sourceRows(rows))
            return
        }

        // Bare door / episode switch: fetch the requested identity through the same source ladder.
        var generation = host._generation
        var type = host.subStreamType || "movie"
        var id = requestedId.length ? requestedId : String(host.subStreamId || "")
        var exts = AddonClient.streamExtensions(Extensions.installed(), type, id)

        function finish(fetched) {
            if (generation !== host._generation)
                return
            // Keep transport-grade rows authoritative; _sourceRows is display-only projection.
            host.streamCandidates = fetched || []
            host.currentStreamIndex = -1
            host.alternateSourcesResolved(mediaId, host._sourceRows(host.streamCandidates))
        }

        if (exts && exts.length) {
            AddonClient.loadStreams(exts, type, id, null, function(all) {
                if (all && all.length) { finish(all); return }
                Torrentio.loadStreams(type, id, finish)
            })
            return
        }
        Torrentio.loadStreams(type, id, finish)
    }

    function _sourceRows(rows) {
        var out = []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            // A row is playable with either a direct URL (debrid/HTTP) or an infoHash (torrent).
            var url = String(c.url || "")
            var hash = String(c.infoHash || "")
            if (!url.length && !hash.length)
                continue
            var fileIdx = (c.fileIdx !== undefined) ? Number(c.fileIdx) : 0
            var key = host._sourceKey(hash, fileIdx, url)
            out.push({
                "id": key,
                "sourceIndex": i,
                "title": String(c.release || c.title || host.mediaTitle || "Stream"),
                "url": url,
                "quality": String(c.qualityLine || c.quality || ""),
                "sizeBytes": 0,             // production carries size as a display string, not bytes
                "seeders": (c.seeders !== undefined) ? Number(c.seeders) : -1,
                "dead": host.deadStreamKeys[key] === true,
                "current": i === host.currentStreamIndex
            })
        }
        return out
    }

    // Stable identity for a candidate, matching the page's dead-key convention (infoHash:fileIdx,
    // lowercased); a direct/debrid link has no hash, so the URL itself is the key.
    function _sourceKey(infoHash, fileIdx, url) {
        if (infoHash && infoHash.length)
            return infoHash.toLowerCase() + ":" + fileIdx
        return "url:" + url
    }

    // ---------------------------------------------------------------------------------------------
    // 4. Online subtitles — the same wells (OpenSubtitles v3 + installed subtitle extensions) the
    //    shipped player searches (qml/Subtitles.js:207).
    // ---------------------------------------------------------------------------------------------
    function requestOnlineSubtitles(mediaId) {
        var generation = host._generation
        var type = host.subStreamType || "movie"
        var id = host.subStreamId || String(mediaId)

        Subtitles.setExtensions(Extensions.installed())
        Subtitles.fetch(type, id, function(list) {
            if (generation !== host._generation)
                return
            var rows = list || []
            var out = []
            for (var i = 0; i < rows.length; i++) {
                var s = rows[i] || ({})
                out.push({
                    "id": String(s.id || ""),
                    "url": String(s.url || ""),
                    "lang": String(s.lang || ""),
                    "langName": String(s.langName || ""),
                    "provider": String(s.source || ""),   // production calls the provider `source`
                    "downloads": Number(s.downloads || 0),
                    "external": true
                })
            }
            host.onlineSubtitlesResolved(mediaId, out)
        })
    }

    // ---------------------------------------------------------------------------------------------
    // 5. Skip segments — AniSkip (MAL ids only) merged with chapter-derived segments, exactly the two
    //    providers and the ranking the shipped player uses (qml/PlayerPage.qml:2248, SkipSegments.js).
    // ---------------------------------------------------------------------------------------------
    function requestSkipSegments(mediaId) {
        var generation = host._generation
        var duration = Number(host.durationSeconds || 0)
        var fromChapters = SkipSegments.chaptersToSegments(host.chapters || [], duration)

        function finish(aniSegments) {
            if (generation !== host._generation)
                return
            var merged = SkipSegments.mergeSegments([aniSegments || [], fromChapters], duration)
            var out = []
            for (var i = 0; i < merged.length; i++) {
                var s = merged[i]
                out.push({
                    // Production says "outro"; the Player 2 shell says "credits". Same thing.
                    "kind": (s.kind === "outro") ? "credits" : s.kind,
                    "startSeconds": Number(s.startSec || 0),
                    "endSeconds": Number(s.endSec || 0),
                    // Auto-skip is a viewer setting in production, never a property of the segment;
                    // the page passes the setting down, so the segment itself never claims it.
                    "autoSkip": false
                })
            }
            host.skipSegmentsResolved(mediaId, out)
        }

        host._fetchAniSkip(duration, finish)
    }

    // Mirrors PlayerPage.fetchAniSkipSegments (qml/PlayerPage.qml:2248) — MAL identity + duration are
    // both required, and any failure resolves to an empty list rather than an error.
    function _fetchAniSkip(duration, done) {
        var id = String(host.subStreamId || "")
        var parts = id.split(":")
        if (parts.length < 3 || parts[0] !== "mal" || !(duration > 0)) {
            done([])
            return
        }
        var malId = Number(parts[1])
        var episode = Number(parts[2])
        if (!(malId > 0) || !(episode > 0)) {
            done([])
            return
        }
        var params = "types=op&types=ed&types=mixed-op&types=mixed-ed&types=recap&episodeLength=" + Math.round(duration)
        var xhr = new XMLHttpRequest()
        xhr.open("GET", "https://api.aniskip.com/v2/skip-times/" + malId + "/" + episode + "?" + params)
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return
            if (xhr.status < 200 || xhr.status >= 300) {
                done([])
                return
            }
            try {
                done(SkipSegments.parseAniSkipResults(JSON.parse(xhr.responseText)))
            } catch (e) {
                done([])
            }
        }
        xhr.send()
    }

    // ---------------------------------------------------------------------------------------------
    // 6. Download — the real C++ DownloadStore, same call the shipped player makes
    //    (qml/PlayerPage.qml:1672). A debrid/direct link downloads straight from its URL.
    // ---------------------------------------------------------------------------------------------
    function requestDownload(mediaId, sourceId) {
        var row = String(sourceId || "").length ? host._candidateForKey(sourceId) : host._currentCandidate()
        var url = (row && row.url && String(row.url).length) ? String(row.url) : host.currentPlaybackUrl
        if (!url || !String(url).length) {
            host.downloadStateChanged(mediaId, { "sourceId": sourceId, "state": "failed",
                                                 "progress": 0, "error": "no downloadable URL for this source" })
            return
        }
        var meta = host._episodeMeta()
        var sourceHeaders = (row && row.headers && typeof row.headers === "object" && !Array.isArray(row.headers))
                          ? row.headers : ({})
        Download.startDownload({
            "url": String(url),
            "headers": sourceHeaders,
            "title": host.mediaTitle,
            "subtitle": "",
            "id": String(mediaId),
            "season": meta.season,
            "episode": meta.episode,
            "kind": meta.isEpisode ? "episode" : "movie",
            "seriesTitle": host.mediaTitle,
            "art": host.mediaArt
        })
        host._downloadSourceId = String(sourceId)
        host._downloadMediaId = String(mediaId)
        host._downloadPeakProgress = 0
        host.downloadStateChanged(mediaId, { "sourceId": sourceId, "state": "queued", "progress": 0 })
    }

    // Cancel the in-flight download. This did NOT exist while the HUD button was already calling it
    // behind a truthiness check (cross-model review, 2026-07-26, P0): the button said "click to
    // cancel", the call silently evaluated to undefined, and nothing happened. A control that lies
    // is worse than a control that is absent.
    function cancelDownload(mediaId) {
        if (typeof Download === "undefined" || !host._downloadMediaId.length)
            return
        if (mediaId && String(mediaId) !== host._downloadMediaId)
            return
        Download.cancelDownload()
        const cancelled = host._downloadMediaId
        host._downloadMediaId = ""
        host._downloadSourceId = ""
        host.downloadStateChanged(cancelled, { "sourceId": "", "state": "idle", "progress": 0 })
    }

    property string _downloadSourceId: ""
    property string _downloadMediaId: ""
    // Highest progress seen for the in-flight job. The store PRUNES a job the moment it succeeds, so
    // "the job vanished" is the only signal a completion ever gives us; without remembering that we
    // saw it running, a vanished job is indistinguishable from one that never started.
    property real _downloadPeakProgress: 0

    // The store's per-job view is the only place "done"/"failed" appear; its `status` property never
    // emits them (native/player/downloadstore.cpp:34). So the stream is driven from jobs().
    property Connections _downloadWatch: Connections {
        target: (typeof Download !== "undefined") ? Download : null
        ignoreUnknownSignals: true
        function onChanged() {
            if (!host._downloadMediaId.length)
                return
            var jobs = Download.jobs() || []
            for (var i = 0; i < jobs.length; i++) {
                var j = jobs[i] || ({})
                if (String(j.id) !== host._downloadMediaId)
                    continue
                host._downloadPeakProgress = Math.max(host._downloadPeakProgress,
                                                      Number(j.ratio || 0))
                host.downloadStateChanged(host._downloadMediaId, {
                    "sourceId": host._downloadSourceId,
                    "state": host._downloadState(String(j.state || "")),
                    "progress": Number(j.ratio || 0),
                    "error": String(j.error || "")
                })
                return
            }
            // The job is GONE. The store prunes on success, so a job we watched running and can no
            // longer find has finished - and if we never reported that, the HUD sits on "cancel"
            // forever for a download that is already on disk (cross-model review 2026-07-26, P0).
            // Guarded on having actually seen progress, so a job pruned for any other reason before
            // it ever ran is not announced as a success.
            if (host._downloadPeakProgress > 0) {
                const finished = host._downloadMediaId
                const sourceId = host._downloadSourceId
                host._downloadMediaId = ""
                host._downloadSourceId = ""
                host._downloadPeakProgress = 0
                host.downloadStateChanged(finished, { "sourceId": sourceId, "state": "done",
                                                      "progress": 1 })
            }
        }
    }

    function _downloadState(state) {
        if (state === "downloading") return "active"
        if (state === "done") return "ready"
        if (state === "failed") return "failed"
        return "queued"   // queued | resolving | paused all read as "not started yet"
    }

    function _currentCandidate() {
        var rows = host.streamCandidates || []
        if (host.currentStreamIndex >= 0 && host.currentStreamIndex < rows.length)
            return rows[host.currentStreamIndex] || null
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            if (c.url && String(c.url) === String(host.currentPlaybackUrl || ""))
                return c
            if (host.mediaResumeHash.length
                    && String(c.infoHash || "") === host.mediaResumeHash
                    && Number(c.fileIdx || 0) === Number(host.mediaResumeFileIdx || 0))
                return c
        }
        return null
    }

    function _candidateForKey(sourceId) {
        var rows = host.streamCandidates || []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i] || ({})
            var fileIdx = (c.fileIdx !== undefined) ? Number(c.fileIdx) : 0
            if (host._sourceKey(String(c.infoHash || ""), fileIdx, String(c.url || "")) === String(sourceId))
                return c
        }
        return null
    }

    // ---------------------------------------------------------------------------------------------
    // 7. Metadata. Production assembles this from four places rather than one call: the context's
    //    logo/backdrop, Cinemeta for seasons + plot, and ProgressStore for the resume point.
    // ---------------------------------------------------------------------------------------------
    function requestMetadata(mediaId) {
        var ctx = host.playbackContext || ({})
        var rootId = EpisodeBrowser.seriesRootId(String(mediaId))
        var generation = host._generation
        var resume = host._resumeSecondsFor(String(mediaId))

        function deliver(meta) {
            if (generation !== host._generation)
                return
            var seasons = meta ? EpisodeBrowser.seasonsFrom(meta.videos || []) : []
            host.metadataResolved(mediaId, {
                "mediaId": String(mediaId),
                "title": host.mediaTitle,
                "logo": String(ctx.logo || host._derivedLogo()),
                "backdrop": String(ctx.episodeStill || ctx.loaderBackdrop || host.mediaArt || ""),
                "seasons": seasons,
                "year": String(ctx.year || ""),
                "plot": host._plotFor(meta, String(mediaId)),
                "resumeSeconds": resume
            })
        }

        if (host._metaCache[rootId]) {
            deliver(host._metaCache[rootId])
            return
        }
        TheatreApi.setExtensions(Extensions.installed())
        TheatreApi.loadMeta(host.subStreamType || "series", rootId, function(meta) {
            if (meta)
                host._metaCache[rootId] = meta
            deliver(meta)
        })
    }

    // Most doors hand the player an empty context, so production derives the logo from the imdb id
    // sitting inside the artwork URL (qml/PlayerPage.qml:1170).
    function _derivedLogo() {
        var m = String(host.mediaArt || "").match(/(tt\d+)/)
        return m ? ("https://live.metahub.space/logo/medium/" + m[1] + "/img") : ""
    }

    function _plotFor(meta, mediaId) {
        if (!meta)
            return ""
        var vids = meta.videos || []
        var parts = String(mediaId).split(":")
        if (parts.length >= 3) {
            var season = Number(parts[parts.length - 2])
            var episode = Number(parts[parts.length - 1])
            for (var i = 0; i < vids.length; i++) {
                var v = vids[i] || ({})
                var vs = (v.season !== undefined) ? v.season : v.seasonNumber
                var ve = (v.episode !== undefined) ? v.episode : v.number
                if (Number(vs) === season && Number(ve) === episode)
                    return String(v.overview || v.description || "")
            }
        }
        return String(meta.description || "")
    }

    // ---------------------------------------------------------------------------------------------
    // 8. Progress. The real ProgressStore, with production's guards and entry shape verbatim
    //    (qml/PlayerPage.qml:1768) so both players write Continue-Watching rows the same way.
    // ---------------------------------------------------------------------------------------------
    function reportProgress(mediaId, position, duration) {
        if (!mediaId || String(mediaId) === "" || duration <= 0 || position <= 0)
            return
        // Anti-clutter floor: an accidental few-second open never leaves a Continue card behind.
        if (position < 10)
            return

        var frac = Math.max(0, Math.min(1, position / duration))
        var remain = Math.max(0, duration - position)
        var meta = host._episodeMeta()
        var epPrefix = meta.isEpisode ? ("S" + meta.season + " · E" + meta.episode + " · ") : ""

        Progress.record({
            "id": String(mediaId),
            "kind": "video",
            "caption": host.mediaTitle,
            "title": host.mediaTitle,
            "sub": epPrefix + host._fmtTime(remain) + " left",
            "cover": host.mediaArt,
            "c1": "#33445d", "c2": "#0c1118",
            "progress": frac,
            "resume": { "infoHash": host.mediaResumeHash,
                        "fileIdx": host.mediaResumeFileIdx,
                        "localPath": host.mediaLocalPath,
                        "subType": host.subStreamType,
                        "subId": host.subStreamId,
                        "position": position }
        })
    }

    // ---- small shared helpers -------------------------------------------------------------------
    function _progressFor(id) {
        if (!id || !String(id).length)
            return { "frac": 0, "watched": false }
        var rec = Progress.get("video", String(id)) || ({})
        var st = EpisodeBrowser.rowState(rec, String(id), "")
        return { "frac": Number(st.frac || 0), "watched": st.state === "watched" }
    }

    function _resumeSecondsFor(id) {
        var rec = Progress.get("video", String(id)) || ({})
        return (rec.resume && Number(rec.resume.position) > 0) ? Number(rec.resume.position) : 0
    }

    // Season/episode from the addon id, the same way production reads it off the stream id.
    function _episodeMeta() {
        var parts = String(host.subStreamId || "").split(":")
        if (host.subStreamType === "series" && parts.length >= 3) {
            return { "isEpisode": true,
                     "season": Number(parts[parts.length - 2]),
                     "episode": Number(parts[parts.length - 1]) }
        }
        return { "isEpisode": false, "season": 0, "episode": 0 }
    }

    function _fmtTime(seconds) {
        var total = Math.max(0, Math.floor(seconds))
        var h = Math.floor(total / 3600)
        var m = Math.floor((total % 3600) / 60)
        var s = total % 60
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return h > 0 ? (h + ":" + pad(m) + ":" + pad(s)) : (m + ":" + pad(s))
    }
}
