// TheatreSeries - Theatre detail page for movies and series.
// Mirrors MangaSeries.qml house style: full-bleed banner, inline metadata, reveal gate,
// pitch-black base, and a slide-up SourcesSheet for Torrentio rows.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "TheatreApi.js" as TheatreApi
import "AnimeEpisodePresentation.js" as AnimeEpisodePresentation
import "TheatreFacts.js" as TheatreFacts

Item {
    id: page
    objectName: "theatreSeriesPage"
    property Item backdrop
    property var itemData: ({})
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl, string subType, string subId, var streamCandidates, var playbackContext)
    signal playLocalRequested(var payload)   // downloaded copy on disk → openLocalVideoSession, no sources sheet
    signal playArrivingRequested(var job)    // still-downloading copy → routeArrivingPlay (disk-first .part play)
    signal openItemRequested(var item)

    property string title: ""
    property string mediaType: "movie"
    property string banner: ""
    property string cover: ""
    property string logo: ""      // show logo (transparent art) for the player's startup loader
    property string year: ""
    property string genresLine: ""
    property string rating: ""
    property string runtime: ""
    property string synopsis: ""
    property var factRows: []
    property var castPeople: []
    property var moreLikeCards: []
    property string animeDoor: ""
    // Keyless anime ordering (spec 2026-07-15) sits between the raw provider list
    // and the episode UI. sourceVideos is the untouched provider array; animeOrder
    // is the native resolver's annotation; videos/episodes derive from it and fall
    // back to sourceVideos whenever ordering is unavailable or incomplete.
    property var sourceVideos: []
    property var animeOrder: page.defaultAnimeOrder()
    property string episodeOrderMode: ""
    property string requestedSourceId: ""
    property int animeOrderRevision: (typeof AnimeOrder !== "undefined") ? AnimeOrder.revision : 0
    onAnimeOrderRevisionChanged: page.rebuildAnimeOrder()
    property var videos: (animeOrder && animeOrder.episodes && animeOrder.episodes.length)
                         ? animeOrder.episodes : sourceVideos
    property string effectiveEpisodeOrder: AnimeEpisodePresentation.effectiveMode(animeOrder, episodeOrderMode)
    property var seasons: []
    property int activeSeason: 0
    property var episodes: AnimeEpisodePresentation.visibleEpisodes(animeOrder, effectiveEpisodeOrder, activeSeason)
    readonly property int compactEpisodeRowHeight: 104
    readonly property int nextUpEpisodeRowHeight: 148
    readonly property int episodeWindowOverscanRows: 8
    readonly property int nextUpEpisodeIndex: page.computeNextUpEpisodeIndex()
    readonly property int episodeContentHeight: page.episodes.length * page.compactEpisodeRowHeight
                                                + (page.nextUpEpisodeIndex >= 0
                                                   ? page.nextUpEpisodeRowHeight - page.compactEpisodeRowHeight : 0)
    readonly property real episodeVirtualContentY: episodeVirtualSpace.mapToItem(pageCol, 0, 0).y
    readonly property real episodeWindowLocalTop: flick.contentY - page.episodeVirtualContentY
    readonly property real episodeWindowLocalBottom: page.episodeWindowLocalTop + flick.height
    readonly property int episodeWindowStart: page.computeEpisodeWindowStart()
    readonly property int episodeWindowEnd: page.computeEpisodeWindowEnd()
    readonly property bool episodeWindowNearViewport:
        page.episodeWindowLocalBottom >= -page.compactEpisodeRowHeight * page.episodeWindowOverscanRows
        && page.episodeWindowLocalTop <= page.episodeContentHeight
                                      + page.compactEpisodeRowHeight * page.episodeWindowOverscanRows
    readonly property var episodeWindowModel: page.episodeWindowNearViewport
                                              ? page.episodes.slice(page.episodeWindowStart, page.episodeWindowEnd)
                                              : []
    readonly property int liveEpisodeDelegateCount: episodeWindowRepeater.count
    property bool episodeJumpOpen: false
    property color watchedInk: "#76b8aa"
    property var seasonQueued: ({})   // season -> queued this visit
    property var pendingDownloadEpisode: null   // episode awaiting a source pick in the sheet
    property var sheetEpisode: null   // episode the PLAY-mode sheet is open for (per-row download)
    property bool pendingSeasonPick: false   // season checkout's picker is open in the sheet
    property bool seasonMenuOpen: false
    property int seasonMenuKeyboardIndex: 0
    property Item seasonMenuReturnItem: null
    property int episodeKeyboardIndex: 0
    property bool episodeContextOpen: false
    property int episodeContextChoice: 0
    property Item episodeContextReturnItem: null
    property Item episodeJumpReturnItem: null
    property string episodeJumpDraft: ""
    property bool loading: true
    property string errorMsg: ""
    // When an anime meta pivots to Cinemeta (kitsu -> imdb id), the meta's own id is
    // the identity everything keys off (episode stream ids, progress, last-season).
    property string resolvedId: ""
    // Keyless TMDB id from the RESOLVED Cinemeta record (moviedb_id). 0 when unknown.
    // Generic title identity carried in the sources context (extensions like NoTorrent
    // accept tmdb ids); taken from the final Cinemeta meta, never the original anime
    // provider object, so an anime→Cinemeta pivot keeps the right id. Reset to 0 before
    // every load so a stale id can never leak across titles.
    property int tmdbId: 0

    function currentId() {
        if (resolvedId.length) return resolvedId;
        return (itemData && itemData.id) ? itemData.id : "";
    }
    // The Collection snapshot. itemData.id preserved over resolvedId: anime ids
    // (mal:/kitsu:) pivot to tt… after the kitsu→imdb hop — save the door we entered by.
    function collectionEntry() {
        return { "id": String((itemData && itemData.id) ? itemData.id : resolvedId),
                 "type": mediaType, "title": title, "cover": cover,
                 "payload": { "art": banner } }
    }
    // Notify-about-new-episodes flag (spec §4.5), read off the saved Collection entry. Default
    // ON (undefined libNotif == on); a scan of the small theatre collection is cheap. Naming
    // Collection.revision in the caller keeps it live.
    function libNotifOn() {
        var id = String(page.collectionEntry().id)
        var items = (typeof Collection !== "undefined") ? Collection.items("theatre") : []
        for (var i = 0; i < items.length; i++)
            if (String(items[i].id) === id)
                return !(items[i].payload && items[i].payload.libNotif === false)
        return true
    }
    // Flip libNotif on the STORED entry (preserving its stamps) via the upsert pattern. A
    // silenced series never badges or counts in the ledger (buildRows forces its newCount to 0).
    function toggleLibNotif() {
        if (typeof Collection === "undefined") return
        var id = String(page.collectionEntry().id)
        var items = Collection.items("theatre")
        for (var i = 0; i < items.length; i++) {
            if (String(items[i].id) !== id) continue
            var e = items[i]
            var patched = {}
            for (var k in e) patched[k] = e[k]
            var payload = {}
            var src = e.payload || {}
            for (var pk in src) payload[pk] = src[pk]
            payload.libNotif = (payload.libNotif === false)   // off → on (true) ; on → off (false)
            patched.payload = payload
            Collection.add("theatre", patched)
            return
        }
    }
    // Canonical annotations win when present (sourceSeason/sourceEpisode from the
    // native resolver); raw provider rows fall back to season/episode so non-anime
    // and unmapped titles behave exactly as before.
    function episodeSeason(v) {
        return (v.sourceSeason !== undefined) ? v.sourceSeason
             : ((v.season !== undefined) ? v.season : (v.seasonNumber || 0))
    }
    function episodeNumber(v) {
        return (v.sourceEpisode !== undefined) ? v.sourceEpisode
             : ((v.episode !== undefined) ? v.episode : (v.number || 0))
    }
    // The number shown to the user: the continuous absolute number in Absolute
    // view, otherwise the provider episode number.
    function episodeDisplayNumber(v) {
        if (page.effectiveEpisodeOrder === "absolute"
                && v.absoluteNumber !== undefined && v.absoluteNumber !== null)
            return v.absoluteNumber
        return episodeNumber(v)
    }
    function episodeIsSpecial(v) {
        return v.kind === "special" || episodeSeason(v) === 0
    }
    function defaultAnimeOrder() {
        return { "status": "unavailable", "episodes": [], "seasons": [],
                 "absoluteComplete": false, "defaultOrder": "seasons" }
    }
    // Ask the native AnimeOrder service to annotate this title's provider rows.
    // Runs on first meta load and whenever the service installs a new generation
    // (revision change). It never fetches anything from QML; the C++ service owns
    // all transport, cache, and completeness decisions.
    function rebuildAnimeOrder() {
        if (typeof AnimeOrder === "undefined") {
            page.animeOrder = page.defaultAnimeOrder()
            return
        }
        var ids = {}
        if (page.requestedSourceId && page.requestedSourceId.length)
            ids.sourceId = page.requestedSourceId
        if (page.resolvedId && page.resolvedId.length)
            ids.resolvedId = page.resolvedId
        if (page.resolvedId && page.resolvedId.indexOf("tt") === 0)
            ids.imdbIds = [page.resolvedId]
        page.animeOrder = AnimeOrder.resolve(ids, page.sourceVideos)
        // Adopt the native default view once per title; the selector overrides it.
        if (page.animeOrder.absoluteComplete === true && page.episodeOrderMode === "")
            page.episodeOrderMode = page.animeOrder.defaultOrder
    }

    function filterEpisodes(vids, season) {
        var out = [];
        for (var i = 0; i < vids.length; i++)
            if (episodeSeason(vids[i]) === season) out.push(vids[i]);
        return out;
    }

    function computeSeasons(vids) {
        var seen = {}, out = [];
        for (var i = 0; i < vids.length; i++) {
            var s = episodeSeason(vids[i]);
            if (s >= 0 && !seen[s]) { seen[s] = true; out.push(s); }
        }
        // Numbered seasons ascending; Specials (season 0) pinned to the end of the row.
        out.sort(function(a, b) {
            if (a === 0) return 1;
            if (b === 0) return -1;
            return a - b;
        });
        return out;
    }

    function defaultSeason() {
        if (!seasons.length)
            return 0;
        if (typeof Progress !== "undefined") {
            var saved = Progress.lastSeason(currentId());
            if (seasonExists(saved))
                return saved;
            var resumeSeason = recentProgressSeason();
            if (seasonExists(resumeSeason))
                return resumeSeason;
        }
        // Fresh show: land on the FIRST numbered season, never on Specials (Hemanth
        // 2026-07-20). Seasons are numbered-ascending with Specials (0) pinned last,
        // so the earliest numbered season is the first entry that is > 0.
        for (var i = 0; i < seasons.length; i++)
            if (seasons[i] > 0)
                return seasons[i];
        return seasons[0];
    }

    function seasonExists(season) {
        for (var i = 0; i < seasons.length; i++)
            if (seasons[i] === season)
                return true;
        return false;
    }

    function seasonLabel() {
        return activeSeason === 0 ? "Specials" : "Season " + activeSeason;
    }
    function selectEpisodeOrder(mode) {
        page.episodeOrderMode=mode
        Qt.callLater(page.scrollToEpisodesTop)
    }
    function selectSeason(season) {
        page.activeSeason=season
        Qt.callLater(page.scrollToEpisodesTop)
    }
    function openSeasonMenu(invoker) {
        if (page.seasonMenuOpen) { page.closeSeasonMenu(true); return }
        page.seasonMenuReturnItem=invoker||null
        var idx=Math.max(0,page.seasons.indexOf(page.activeSeason))
        page.seasonMenuKeyboardIndex=idx; page.seasonMenuOpen=true
        Qt.callLater(function(){ seasonMenuFocus.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeSeasonMenu(restore) {
        page.seasonMenuOpen=false
        var target=page.seasonMenuReturnItem; page.seasonMenuReturnItem=null
        if(restore!==false && target) Qt.callLater(function(){ if(target.visible&&target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function activateSeasonMenu(index) {
        if(index<0||index>=page.seasons.length) return
        page.selectSeason(page.seasons[index]); page.closeSeasonMenu(true)
    }
    function toggleEpisodeJump(invoker) {
        if(page.episodeJumpOpen) { page.closeEpisodeJump(true); return }
        page.episodeJumpReturnItem=invoker||null; page.episodeJumpOpen=true
        Qt.callLater(function(){ jumpInput.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeEpisodeJump(restore) {
        page.episodeJumpOpen=false; page.episodeJumpDraft=""
        var target=page.episodeJumpReturnItem; page.episodeJumpReturnItem=null
        if(restore!==false && target) Qt.callLater(function(){ if(target.visible&&target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }

    // Season checkout (torrent-pick rework 2026-07-19): `pick` (optional) is the
    // FULL-SEASON torrent Hemanth chose in the sheet — every episode job pins that
    // infoHash with fileIdx -1 ("find my file inside it at resolve time"); an
    // episode the pack doesn't carry falls back to rank-best on its own. No pick
    // (the no-packs fallback) = the original per-episode auto path. Episodes
    // enqueue in ascending order and the store promotes one at a time, FIFO — the
    // season always downloads sequentially.
    function queueSeasonDownload(pick) {
        if (typeof Download === "undefined")
            return;
        var pinHash = "";
        if (pick) {
            var h = String(pick.infoHash || "");
            if (h.length && h.indexOf("url:") !== 0)
                pinHash = h;
        }
        var reqs = [];
        for (var i = 0; i < episodes.length; i++) {
            var v = episodes[i];
            var sid = episodeStreamId(v);
            if (Download.hasVideo(sid))
                continue;   // already on disk
            var req = {
                "id": sid,
                "kind": "episode",
                "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
                "subtitle": v.title || v.name || "",
                "seriesTitle": page.title,
                "season": episodeSeason(v),
                "episode": episodeNumber(v),
                "art": page.cover
            };
            if (pinHash.length) {
                req["infoHash"] = pinHash;
                req["fileIdx"] = -1;   // hash-only pin: resolver matches the episode's file
            }
            reqs.push(req);
        }
        if (reqs.length) {
            Download.enqueueBatch(reqs);
            Collection.add("theatre", page.collectionEntry())
        }
        var q = seasonQueued;
        q[activeSeason] = true;
        seasonQueued = q;
    }

    // "Download <season>" now fronts the sheet as a FULL-SEASON torrent picker
    // (season mode). The first not-yet-downloaded episode carries the ask — packs
    // ride every episode's stream list. Zero packs → onSeasonNoPacks → auto path.
    function openSeasonPicker() {
        if (typeof Download === "undefined")
            return;
        var target = null;
        for (var i = 0; i < episodes.length; i++) {
            if (!Download.hasVideo(episodeStreamId(episodes[i]))) { target = episodes[i]; break; }
        }
        if (!target) {   // whole season already on disk — just mark it
            var q = seasonQueued;
            q[activeSeason] = true;
            seasonQueued = q;
            return;
        }
        page.pendingSeasonPick = true
        sources.show("series", episodeStreamId(target),
                     page.title + " - " + seasonLabel(),
                     Object.assign({
                         "title": page.title,
                         "metaLine": seasonLabel(),
                         "backdrop": sourceBackdrop(),
                         "season": activeSeason
                     }, adjacentEpisodeContext(target)),
                     "season")
    }

    // Single-episode download (parity spec 2026-07-06 F4; torrent-choice spec
    // 2026-07-11): same request shape as the season checkout, for exactly one
    // episode. `pick` (optional) is the SourcesSheet row Hemanth chose — a torrent
    // row pins infoHash/fileIdx (resolver skips the search), a direct/url row
    // carries its url (DownloadStore downloads it with no resolve at all). The
    // season checkout passes no pick and keeps the rank-best auto path.
    function queueEpisodeDownload(v, pick) {
        if (typeof Download === "undefined")
            return;
        var sid = episodeStreamId(v);
        if (Download.hasVideo(sid))
            return;   // already on disk
        var req = {
            "id": sid,
            "kind": "episode",
            "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
            "subtitle": v.title || v.name || "",
            "seriesTitle": page.title,
            "season": episodeSeason(v),
            "episode": episodeNumber(v),
            "art": page.cover
        };
        applyPick(req, pick);
        Download.enqueueBatch([req]);
        Collection.add("theatre", page.collectionEntry())
    }

    // Pin a hand-picked SourcesSheet row onto a download request: a direct/url row
    // goes straight to startHttp (no resolve), a torrent row pins infoHash/fileIdx
    // so the resolver prefetches exactly that torrent. No pick -> rank-best auto path.
    function applyPick(req, pick) {
        if (!pick)
            return;
        var h = String(pick.infoHash || "");
        var direct = pick.url ? String(pick.url)
                   : (h.indexOf("url:") === 0 ? h.substring(4) : "");
        if (direct.length) {
            req["url"] = direct;
            req["headers"] = (pick.headers && typeof pick.headers === "object" && !Array.isArray(pick.headers))
                             ? pick.headers : ({});
        } else if (h.length) {
            req["infoHash"] = h;
            req["fileIdx"] = Number(pick.fileIdx || 0);
        }
    }

    // Movie flavour of the per-row sheet download (2026-07-19): same pinned request,
    // no season/episode fields — the store's groupKey falls through to the plain id.
    function queueMovieDownload(pick) {
        if (typeof Download === "undefined")
            return;
        var sid = currentId();
        if (Download.hasVideo(sid))
            return;   // already on disk
        var req = {
            "id": sid,
            "kind": "movie",
            "title": page.title,
            "art": page.cover
        };
        applyPick(req, pick);
        Download.enqueueBatch([req]);
        Collection.add("theatre", page.collectionEntry())
    }

    // ids currently sitting in the download queue (any state) — recomputed on every queue
    // change so the per-episode button can show "on its way" instead of re-queueing.
    property var queuedDownloadIds: (typeof Download !== "undefined")
        ? (Download.queueRevision, (function() {
              var m = ({});
              var js = Download.jobs();
              for (var i = 0; i < js.length; i++) m[js[i].id] = true;
              return m;
          })())
        : ({})

    function episodeStreamId(v) {
        if (v.streamId && v.streamId.length) return v.streamId;
        if (v.id && v.id.length) return v.id;
        return currentId() + ":" + episodeSeason(v) + ":" + episodeNumber(v);
    }

    // The hero Watch target for a series: the first visible episode of the default
    // season (the Continue row owns resume; this is the front door). Null-safe.
    function heroEpisode() {
        return (mediaType === "series" && episodes && episodes.length) ? episodes[0] : null
    }
    function openHeroForPlay() {
        if (page.mediaType === "series") {
            var ep=page.heroEpisode(); if(!ep) return
            var label=page.title+" - S"+page.episodeSeason(ep)+"E"+page.episodeNumber(ep)
            if (page.tryPlayLocal(page.episodeStreamId(ep),label,"episode") || page.tryPlayArriving(page.episodeStreamId(ep))) return
            page.sheetEpisode=ep
            sources.show("series",page.episodeStreamId(ep),label,Object.assign({"title":page.title,"metaLine":page.episodeSourceLine(ep),"backdrop":page.sourceBackdrop(),"tmdbId":page.tmdbId,"imdbId":page.currentId(),"season":page.episodeSeason(ep),"episode":page.episodeNumber(ep)},page.adjacentEpisodeContext(ep)))
        } else {
            if (page.tryPlayLocal(page.currentId(),page.title,"movie") || page.tryPlayArriving(page.currentId())) return
            page.sheetEpisode=null
            sources.show("movie",page.currentId(),page.title,{"title":page.title,"year":page.year,"metaLine":page.sourceMetaLine(),"backdrop":page.sourceBackdrop(),"tmdbId":page.tmdbId,"imdbId":page.currentId()})
        }
    }

    function openEpisodeForPlay(v) {
        if (!v) return
        var label=page.title+" - S"+page.episodeSeason(v)+"E"+page.episodeNumber(v)
        if (page.tryPlayLocal(page.episodeStreamId(v),label,"episode") || page.tryPlayArriving(page.episodeStreamId(v))) return
        page.sheetEpisode=v
        sources.show("series",page.episodeStreamId(v),label,Object.assign({"title":page.title,"metaLine":page.episodeSourceLine(v),"backdrop":page.sourceBackdrop(),"tmdbId":page.tmdbId,"imdbId":page.currentId(),"season":page.episodeSeason(v),"episode":page.episodeNumber(v)},page.adjacentEpisodeContext(v)))
    }
    function openEpisodeDownload(v) {
        if (!v || typeof Download === "undefined") return
        var sid=page.episodeStreamId(v)
        if (Download.hasVideo(sid) || page.queuedDownloadIds[sid] === true) return
        page.pendingDownloadEpisode=v
        var label=page.title+" - S"+page.episodeSeason(v)+"E"+page.episodeNumber(v)
        var context=Object.assign({"title":page.title,"metaLine":page.episodeSourceLine(v),"backdrop":page.sourceBackdrop()},page.adjacentEpisodeContext(v))
        sources.show("series",sid,label,context,"download")
    }
    function episodeContextOptions(index) {
        var out=["Play"]
        var v=(index>=0&&index<page.episodes.length)?page.episodes[index]:null
        if (v && typeof Download !== "undefined") {
            var sid=page.episodeStreamId(v)
            if (!Download.hasVideo(sid) && page.queuedDownloadIds[sid] !== true) out.push("Download")
        }
        return out
    }
    function openEpisodeContext(index) {
        page.episodeKeyboardIndex=index; page.episodeContextChoice=0; page.episodeContextReturnItem=episodeVirtualSpace
        page.episodeContextOpen=true
        Qt.callLater(function(){ episodeContextFocus.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeEpisodeContext(restore) {
        page.episodeContextOpen=false
        if (restore!==false && page.episodeContextReturnItem) Qt.callLater(function(){ episodeVirtualSpace.forceActiveFocus(Qt.PopupFocusReason) })
        page.episodeContextReturnItem=null
    }
    function activateEpisodeContext(choice) {
        var v=page.episodes[page.episodeKeyboardIndex]
        if(choice===0) page.openEpisodeForPlay(v); else if(choice===1) page.openEpisodeDownload(v)
        page.closeEpisodeContext(true)
    }

    function sourceBackdrop() {
        return banner.length ? banner : cover;
    }

    function sourceMetaLine() {
        var parts = [];
        if (year.length) parts.push(year);
        if (genresLine.length) parts.push(genresLine);
        return parts.join(" - ");
    }

    function episodeSourceLine(v) {
        var label = "S" + episodeSeason(v) + "E" + episodeNumber(v);
        var epTitle = v.title || v.name || "";
        return epTitle.length ? (label + " - " + epTitle) : label;
    }

    // "S1 · E03 · Name" for the player's startup loader — omits unknown parts, never null/undefined.
    function loadingEpisodeLine(v) {
        if (!v) return "";
        var parts = [];
        var s = episodeSeason(v);
        var e = episodeNumber(v);
        if (s !== undefined && s !== null && String(s).length) parts.push("S" + s);
        if (e !== undefined && e !== null && String(e).length) {
            var en = String(e);
            parts.push("E" + (en.length < 2 ? "0" + en : en));
        }
        var nm = v.title || v.name || "";
        if (nm.length) parts.push(nm);
        return parts.join(" · ");
    }

    function episodePlaybackTarget(v) {
        if (!v)
            return null;
        var target = shallowEpisodeTarget(v);
        target.context = adjacentEpisodeContext(v);
        return target;
    }

    function shallowEpisodeTarget(v) {
        if (!v)
            return null;
        return {
            "type": "series",
            "id": episodeStreamId(v),
            "title": page.title + " - S" + episodeSeason(v) + "E" + episodeNumber(v),
            "backdrop": sourceBackdrop(),
            "season": episodeSeason(v),
            "episode": episodeNumber(v),
            "metaLine": episodeSourceLine(v)
        };
    }

    // The playback queue for the active view, built from the native order:
    // Absolute yields one regular queue across source seasons; Seasons yields the
    // active-season queue exactly as before. The clicked row is located by exact
    // stream id (never by episode number, which repeats across seasons).
    function adjacentEpisodeContext(v) {
        var queue = AnimeEpisodePresentation.playbackTargets(
                        page.animeOrder, page.effectiveEpisodeOrder, page.activeSeason,
                        page.title, page.sourceBackdrop(), page.currentId());
        var targetId = episodeStreamId(v);
        var idx = -1;
        for (var i = 0; i < queue.length; i++)
            if (queue[i].id === targetId) { idx = i; break; }
        return {
            "year": page.year,
            "episodeQueue": queue,
            "episodeIndex": idx,
            // Per-show startup-loader identity (Task 4a) — merged into every episode context.
            "logo": page.logo,
            "episodeStill": TheatreApi.normalizeArtUrl((v && v.thumbnail) || ""),
            "loaderBackdrop": page.banner,
            "episodeLine": page.loadingEpisodeLine(v),
            "adjacentEpisodes": {
                "prev": idx > 0 ? Object.assign({}, queue[idx - 1],
                                                { "context": { "year": page.year, "episodeQueue": queue, "episodeIndex": idx - 1 } }) : null,
                "next": (idx >= 0 && idx + 1 < queue.length)
                        ? Object.assign({}, queue[idx + 1],
                                        { "context": { "year": page.year, "episodeQueue": queue, "episodeIndex": idx + 1 } }) : null
            }
        };
    }

    function episodeIndex(number) {
        for (var i = 0; i < episodes.length; i++)
            if (episodeDisplayNumber(episodes[i]) === number)
                return i;
        return -1;
    }

    // Page-coordinate positioning (scroll-UX rework 2026-08-24): `episodeList` no longer
    // owns its own scrolling — `flick` (the page Flickable) is the single scroll surface, so
    // "jump to episode N" / "open at next-up" have to translate a row index into a
    // flick.contentY, not a ListView-internal position.
    //
    // The pinned chrome's total height (72px Back clearance + the chrome content itself);
    // scrollToEpisodeIndex lands a row just below it rather than under it.
    property real stickyHeaderHeight: seasonChromeFloat.visible ? (72 + chromeCol.height) : 0

    // Episode geometry is arithmetic because only a small moving window of delegates exists.
    // This keeps jump-to-episode and next-up positioning independent of delegate realization.
    function episodeOffsetForIndex(index) {
        if (index <= 0)
            return 0
        var y = index * page.compactEpisodeRowHeight
        if (page.nextUpEpisodeIndex >= 0 && page.nextUpEpisodeIndex < index)
            y += page.nextUpEpisodeRowHeight - page.compactEpisodeRowHeight
        return y
    }

    function episodeIndexAtOffset(offset) {
        if (!page.episodes.length)
            return 0
        var y = Math.max(0, Number(offset || 0))
        var nextIdx = page.nextUpEpisodeIndex
        if (nextIdx >= 0) {
            var nextStart = nextIdx * page.compactEpisodeRowHeight
            var nextEnd = nextStart + page.nextUpEpisodeRowHeight
            if (y >= nextStart && y < nextEnd)
                return nextIdx
            if (y >= nextEnd)
                y -= page.nextUpEpisodeRowHeight - page.compactEpisodeRowHeight
        }
        return Math.max(0, Math.min(page.episodes.length - 1,
                                    Math.floor(y / page.compactEpisodeRowHeight)))
    }

    function computeEpisodeWindowStart() {
        if (!page.episodes.length || page.episodeWindowLocalBottom < 0)
            return 0
        var first = page.episodeIndexAtOffset(Math.max(0, page.episodeWindowLocalTop))
        return Math.max(0, first - page.episodeWindowOverscanRows)
    }

    function computeEpisodeWindowEnd() {
        if (!page.episodes.length)
            return 0
        if (page.episodeWindowLocalTop > page.episodeContentHeight)
            return page.episodes.length
        if (page.episodeWindowLocalBottom < 0)
            return Math.min(page.episodes.length, page.episodeWindowOverscanRows * 2 + 1)
        var bottom = Math.max(0, Math.min(page.episodeContentHeight - 1,
                                         page.episodeWindowLocalBottom))
        var last = page.episodeIndexAtOffset(bottom)
        return Math.min(page.episodes.length, last + 1 + page.episodeWindowOverscanRows)
    }

    function computeNextUpEpisodeIndex() {
        for (var i = 0; i < page.episodes.length; i++)
            if (!page.episodeWatched(page.episodes[i]))
                return i
        return page.episodes.length ? page.episodes.length - 1 : -1
    }

    function scrollToEpisodeIndex(index) {
        if (index < 0)
            return
        var target = page.episodeVirtualContentY + page.episodeOffsetForIndex(index) - page.stickyHeaderHeight - 8
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, target))
    }

    // Season switch / order-mode switch (2026-08-24): land on the top of the episodes
    // section instead of resetting the list's own scroll (there is no list-owned scroll
    // any more) — the sticky chrome anchors itself there once flick.contentY reaches it.
    function scrollToEpisodesTop() {
        // -72: the chrome pins at y 72 (Back clearance), so the placeholder must stop
        // there, not at y 0 — otherwise row 0 tucks under the pinned band.
        var top = seasonChromePlaceholder.mapToItem(pageCol, 0, 0).y - 72
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, top))
    }

    function jumpToEpisodeNumber(number) {
        var index = episodeIndex(number);
        if (index < 0)
            return false;
        Qt.callLater(function() { page.scrollToEpisodeIndex(index) })
        page.closeEpisodeJump(true)
        return true;
    }

    function submitEpisodeJump() {
        var n = parseInt(episodeJumpDraft, 10);
        if (isNaN(n))
            return;
        jumpToEpisodeNumber(n);
    }

    function progressEntry(v) {
        if (typeof Progress === "undefined")
            return ({});
        var rev = Progress.revision;
        var entry = Progress.get("video", episodeStreamId(v));
        return entry || ({});
    }

    // Downloaded-first play (2026-07-31, Hemanth: a downloaded season still routed Play to the
    // sources sheet). If the stream id is in Download's library AND the file still exists, play
    // the local copy through the same openLocalVideoSession path the Continue card uses — never
    // a stream fetch. Returns true when it handled the play; false → caller opens sources as
    // before. `missing` (file deleted outside the app) deliberately falls back to sources.
    function tryPlayLocal(sid, label, kind) {
        if (typeof Download === "undefined" || !Download.hasVideo(sid))
            return false;
        var vids = Download.downloadedVideos() || [];
        for (var i = 0; i < vids.length; i++) {
            if (vids[i].id !== sid) continue;
            if (vids[i].missing) return false;
            var entry = (typeof Progress !== "undefined") ? (Progress.get("video", sid) || {}) : {};
            var resume = entry.resume || {};
            page.playLocalRequested({
                "path": vids[i].path,
                "id": sid,
                "title": vids[i].title || label,
                "art": vids[i].art || page.cover,
                "kind": kind,
                "position": Number(resume.position || 0)
            });
            return true;
        }
        return false;
    }

    // Still downloading? Route through the arriving path (2026-07-31): it plays the
    // job's .part off disk and only goes live if the watcher outruns the download.
    function tryPlayArriving(sid) {
        if (typeof Download === "undefined")
            return false;
        var rows = Download.jobs() || [];
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].id === sid && String(rows[i].url || "").length) {
                page.playArrivingRequested(rows[i]);
                return true;
            }
        }
        return false;
    }

    function episodeProgressRatio(v) {
        var entry = progressEntry(v);
        var p = Number(entry.progress || 0);
        if (!isFinite(p) || p < 0)
            return 0;
        return Math.max(0, Math.min(1, p));
    }

    function episodeWatched(v) {
        var entry = progressEntry(v);
        return entry.watched === true || episodeProgressRatio(v) >= 0.85;
    }

    function nextUpEpisode() {
        return page.nextUpEpisodeIndex >= 0 && page.nextUpEpisodeIndex < page.episodes.length
               ? page.episodes[page.nextUpEpisodeIndex] : null
    }
    function nextUpEpisodeNumber() {
        var e = page.nextUpEpisode()
        return e ? page.episodeNumber(e) : 0
    }
    // Next-up identity rides the stream id: provider episode numbers repeat across
    // seasons, so matching by number lights up two rows at once in Absolute view.
    function nextUpEpisodeId() {
        var e = page.nextUpEpisode()
        return e ? page.episodeStreamId(e) : ""
    }
    function nextUpDisplayNumber() {
        var e = page.nextUpEpisode()
        return e ? page.episodeDisplayNumber(e) : 0
    }

    function recentProgressSeason() {
        if (typeof Progress === "undefined")
            return -1;
        var rev = Progress.revision;
        var rows = Progress.recent("video", 80);
        var prefix = currentId() + ":";
        for (var i = 0; i < rows.length; i++) {
            var id = rows[i].id || "";
            if (id.indexOf(prefix) !== 0)
                continue;
            var parts = id.split(":");
            if (parts.length > 2)
                return Number(parts[1]);
        }
        return -1;
    }

    Theme { id: theme }
    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => { if (!event.accepted) theatreSeriesScrollKeys.handle(event) }

    onItemDataChanged: resolve()
    onEpisodesChanged: {
        page.episodeKeyboardIndex = page.episodes.length ? Math.max(0, Math.min(page.episodes.length - 1, page.episodeKeyboardIndex)) : 0
    }
    onActiveSeasonChanged: {
        // >= 0 so a Specials (season 0) pick is remembered too; resets during
        // resolve() are already guarded by `loading`.
        if (!loading && mediaType === "series" && activeSeason >= 0 && typeof Progress !== "undefined")
            Progress.rememberLastSeason(currentId(), activeSeason)
    }
    Component.onCompleted: if (currentId().length) resolve()

    function resolve() {
        loading = true;
        errorMsg = "";
        title = (itemData && itemData.title) ? itemData.title : "";
        mediaType = (itemData && itemData.type) ? itemData.type : "movie";
        banner = (itemData && itemData.art) ? itemData.art : "";
        cover = (itemData && itemData.cover) ? itemData.cover : "";
        year = "";
        genresLine = "";
        rating = "";
        runtime = "";
        synopsis = "";
        sourceVideos = [];
        animeOrder = page.defaultAnimeOrder();
        episodeOrderMode = "";
        seasons = [];
        activeSeason = 0;
        resolvedId = "";
        tmdbId = 0;
        // Capture the originally requested id before resolvedId pivots to IMDb, so
        // the resolver still gets the provider source id (mal:/kitsu:/anidb:...).
        requestedSourceId = currentId();
        var id = currentId();
        if (!id) { loading = false; errorMsg = "No id for this title."; return; }
        revealGuard.restart();
        page.animeDoor = String((itemData && itemData.id) || "")
        TheatreApi.loadMeta(mediaType, id, function(meta) {
            if (!meta) {
                errorMsg = "Couldn't load details.";
                loading = false;
                revealGuard.stop();
                return;
            }
            if (meta.id) resolvedId = String(meta.id);
            // Take the TMDB id from the FINAL Cinemeta record (after any anime→imdb pivot),
            // so source extensions resolve the right title rather than the anime provider's id.
            page.tmdbId = Math.max(0, Math.floor(Number(meta.moviedb_id || meta.tmdbId || 0)));
            if (meta.name) title = meta.name;
            var bg = TheatreApi.normalizeArtUrl(meta.background || "");
            if (bg) banner = bg;
            var po = TheatreApi.normalizeArtUrl(meta.poster || "");
            if (po) cover = po;
            // per-show loader logo (stylized title art). Cinemeta often omits meta.logo, so fall
            // back to the metahub logo endpoint keyed by the imdb id — the same source Stremio uses.
            logo = TheatreApi.normalizeArtUrl(meta.logo
                || (meta.id ? "https://live.metahub.space/logo/medium/" + meta.id + "/img" : ""));
            year = meta.year ? String(meta.year) : (meta.releaseInfo || "");
            if (meta.genres && meta.genres.length) genresLine = meta.genres.slice(0, 3).join(" - ");
            rating = meta.imdbRating || "";
            runtime = meta.runtime || "";
            synopsis = meta.description || "";
            page.factRows = TheatreFacts.factRows(meta, null)
            var doorForCast = page.animeDoor
            TheatreApi.loadAnimeCast(doorForCast, function(anime) {
                if (page.animeDoor !== doorForCast) return   // stale response, page moved on
                if (anime) {
                    page.castPeople = anime.cast
                    page.factRows = TheatreFacts.factRows(meta, anime)   // Studio + Source rows join
                } else {
                    page.castPeople = (meta.cast || []).map(function(n) {
                        return { "name": n, "role": "", "image": "" }
                    })
                }
            })
            var mltGenre = (meta.genres && meta.genres.length) ? meta.genres[0] : ""
            var doorForMlt = page.animeDoor
            TheatreApi.moreLikeThis(page.mediaType, page.animeDoor, page.resolvedId, mltGenre,
                                    (typeof MalCatalog !== "undefined") ? MalCatalog : null,
                                    function(cards) {
                                        if (page.animeDoor !== doorForMlt) return   // stale: a sibling tap moved the page on
                                        page.moreLikeCards = cards || []
                                    })
            sourceVideos = meta.videos || [];
            page.rebuildAnimeOrder();
            page.onMetaLoaded();
            loading = false;
            revealGuard.stop();
        });
    }

    function onMetaLoaded() {
        if (mediaType === "series") {
            seasons = computeSeasons(videos);
            var requestedSeason = Number((itemData && itemData.requestedSeason) || -1)
            var requestedEpisode = Number((itemData && itemData.requestedEpisode) || 0)
            activeSeason = (requestedSeason >= 0 && seasonExists(requestedSeason))
                           ? requestedSeason : defaultSeason();
            if (requestedEpisode > 0) {
                Qt.callLater(function() {
                    var requestedIdx = page.episodeIndex(requestedEpisode)
                    if (requestedIdx >= 0)
                        page.scrollToEpisodeIndex(requestedIdx)
                })
                return
            }
            // Open on next-up, not episode 1 (Hemanth, scroll-UX rework 2026-08-24). If
            // everything is unwatched, next-up IS episode 1 (index 0) — leave the page at
            // the top so the hero stays visible instead of "scrolling" nowhere.
            var nextIdx = page.nextUpEpisodeIndex
            if (nextIdx > 0)
                Qt.callLater(function() { page.scrollToEpisodeIndex(nextIdx) })
        } else {
            seasons = [];
            activeSeason = 0;
        }
    }

    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true
        hideSource: false
        visible: page.backdrop !== null
        opacity: 0.5
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
        }
    }

    ChromeScrim { z: 16 }

    BackAction {
        id: backBtn
        x: theme.margin
        y: 28
        z: 20
        onTriggered: page.backRequested()
    }

    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: minMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested()
            }
            KeyboardAction {
                anchors.fill: parent; pointerEnabled: false; accessibleName: "Minimize"
                focusRadius: 6; onTriggered: page.minimizeRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.fullscreenRequested()
            }
            KeyboardAction {
                anchors.fill: parent; pointerEnabled: false; accessibleName: "Toggle fullscreen"
                focusRadius: 6; onTriggered: page.fullscreenRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: clMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested()
            }
            KeyboardAction {
                anchors.fill: parent; pointerEnabled: false; accessibleName: "Close"
                focusRadius: 6; onTriggered: page.closeRequested()
            }
        }
    }

    Flickable {
        id: flick
        objectName: "theatreSeriesScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: flick }
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            Item {
                width: parent.width
                height: 360
                Image {
                    id: bannerImg
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.15) }
                        GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.96) }
                    }
                }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin
                    anchors.rightMargin: theme.margin
                    anchors.bottomMargin: 30
                    spacing: 12
                    // Hemanth eyes-on 2026-07-20: scrolling slid this hero text under the
                    // fixed Back/chrome band ("2006Back" mash) — fade it as it climbs; it
                    // is fully gone before it can reach the chrome.
                    opacity: Math.max(0, 1 - flick.contentY / 220)
                    Text {
                        text: page.mediaType === "series" ? "Series - Theatre" : "Movie - Theatre"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.letterSpacing: 3
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.title
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 64
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        style: Text.Raised
                        styleColor: Qt.rgba(0, 0, 0, 0.35)
                    }
                    Row {
                        spacing: 11
                        Text {
                            visible: page.year.length
                            text: page.year
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.year.length && page.genresLine.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.genresLine.length
                            text: page.genresLine
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.rating.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Row {
                            id: imdbRatingBadge
                            visible: page.rating.length
                            height: 20
                            spacing: 7
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                id: imdbPlaque
                                width: 34
                                height: 18
                                radius: 3
                                color: "#F5C518"
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: "IMDb"
                                    color: "#111111"
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    font.weight: Font.Black
                                    font.letterSpacing: -0.45
                                }
                            }
                            Text {
                                id: imdbRatingValue
                                text: page.rating
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            visible: page.runtime.length
                            text: "-"
                            color: theme.inkDimmer
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: page.runtime.length
                            text: page.runtime
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Row {
                        spacing: 12
                        topPadding: 8
                        Rectangle {
                            objectName: "theatreSeriesWatch"
                            visible: page.mediaType !== "series" || page.heroEpisode() !== null
                            width: watchRow.implicitWidth + 40
                            height: 42
                            radius: 11
                            color: theme.gold
                            Row {
                                id: watchRow
                                anchors.centerIn: parent
                                spacing: 9
                                PlayerIcon {
                                    kind: "play"
                                    ink: "#1a1306"
                                    width: 16; height: 16
                                    iconSize: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: page.mediaType === "series" && page.heroEpisode()
                                        ? "Watch  S" + page.episodeSeason(page.heroEpisode()) + " · E" + page.episodeDisplayNumber(page.heroEpisode())
                                        : "Watch"
                                    color: "#1a1306"
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: parent.opacity = 0.92
                                onExited: parent.opacity = 1.0
                                onClicked: page.openHeroForPlay()
                            }
                            KeyboardAction {
                                id: heroWatchKeyboard; anchors.fill: parent; pointerEnabled: false
                                accessibleName: "Watch " + page.title; focusRadius: parent.radius
                                onTriggered: page.openHeroForPlay()
                            }
                        }
                        LibraryButton {
                            world: "theatre"
                            entry: page.collectionEntry()
                        }
                        // Notify-about-new-episodes toggle (spec §4.5) — only for SAVED series.
                        // Flips payload.libNotif; a silenced series stops badging + counting.
                        Rectangle {
                            id: notifPill
                            visible: page.mediaType === "series" && (typeof Collection !== "undefined")
                                     && (Collection.revision, Collection.has("theatre", String(page.collectionEntry().id)))
                            readonly property bool on: (Collection.revision, page.libNotifOn())
                            width: notifText.implicitWidth + 32
                            height: 42
                            radius: 11
                            color: notifMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: theme.edge
                            Text {
                                id: notifText
                                anchors.centerIn: parent
                                text: notifPill.on ? "Notifications on" : "Notifications off"
                                color: notifPill.on ? theme.ink : theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: notifMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.toggleLibNotif()
                            }
                            KeyboardAction {
                                id: notificationKeyboard; anchors.fill: parent; pointerEnabled: false
                                accessibleName: notifPill.on ? "Turn notifications off" : "Turn notifications on"
                                focusRadius: parent.radius; onTriggered: page.toggleLibNotif()
                            }
                        }
                    }
                }
            }

            Row {
                x: theme.margin
                spacing: 56
                Text {
                    visible: page.synopsis.length > 0
                    width: 580
                    text: page.synopsis
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 15
                    lineHeight: 1.5
                    wrapMode: Text.WordWrap
                    topPadding: 22
                    bottomPadding: 6
                }
                Column {
                    spacing: 10
                    visible: page.factRows.length > 0
                    Repeater {
                        model: page.factRows
                        Row {
                            id: factRow
                            required property var modelData
                            spacing: 18
                            Text { text: factRow.modelData.k; color: theme.inkDim; width: 90
                                   font.family: theme.ui; font.pixelSize: 13 }
                            Text { text: factRow.modelData.v; color: theme.ink
                                   font.family: theme.ui; font.pixelSize: 13 }
                        }
                    }
                }
            }

            Item {
                id: episodesSection
                width: parent.width
                height: episodesCol.height
                visible: page.mediaType === "series" && page.videos.length > 0

                Column {
                    id: episodesCol
                    width: parent.width
                    spacing: 0

                    // Season chrome (order selector, season tabs/dropdown, ledger header,
                    // jump popover, column headers) is rendered by the floating
                    // `seasonChromeFloat` item (page-level, pins to the top of the viewport
                    // once scrolled past this point — see below, after `flick`). This
                    // placeholder only reserves its layout space inside episodesCol so the
                    // episode list starts at the correct offset.
                    Item {
                        id: seasonChromePlaceholder
                        width: parent.width
                        height: chromeCol.height
                    }

                    Item {
                        id: episodeVirtualSpace
                        objectName: "theatreEpisodeVirtualSpace"
                        width: parent.width
                        height: page.episodeContentHeight
                        property int currentIndex: page.episodeKeyboardIndex
                        onCurrentIndexChanged: page.episodeKeyboardIndex = currentIndex
                        focusPolicy: page.episodes.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => episodeKeys.handle(event)
                        KeyboardCollectionController {
                            id: episodeKeys; view: episodeVirtualSpace; orientation: "vertical"
                            count: page.episodes.length; contextEnabled: true
                            pageStep: Math.max(1, Math.floor(flick.height / page.compactEpisodeRowHeight))
                            positionIndexFn: function(index) { page.scrollToEpisodeIndex(index) }
                            onActivated: (index) => page.openEpisodeForPlay(page.episodes[index])
                            onContextRequested: (index) => page.openEpisodeContext(index)
                        }

                        // The page remains the only vertical scroll surface. This repeater
                        // receives only the visible episode slice plus a small overscan window.
                        Repeater {
                            id: episodeWindowRepeater
                            objectName: "theatreEpisodeWindow"
                            model: page.episodeWindowModel

                            delegate: Item {
                                id: ep
                                required property int index
                                required property var modelData
                                property int absoluteIndex: page.episodeWindowStart + index
                                property real progressRatio: page.episodeProgressRatio(modelData)
                                property bool watched: page.episodeWatched(modelData)
                                property bool nextUp: ep.absoluteIndex === page.nextUpEpisodeIndex
                                property bool narrow: episodeVirtualSpace.width < 980
                                property bool tiny: episodeVirtualSpace.width < 760
                                y: page.episodeOffsetForIndex(ep.absoluteIndex)
                                width: episodeVirtualSpace.width
                                height: ep.nextUp ? page.nextUpEpisodeRowHeight : page.compactEpisodeRowHeight
                            function openForPlay() { page.openEpisodeForPlay(ep.modelData) }
                            Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: theme.margin
                                anchors.rightMargin: theme.margin
                                color: episodeVirtualSpace.activeFocus && page.episodeKeyboardIndex === ep.absoluteIndex
                                      ? Qt.rgba(0.94, 0.77, 0.29, 0.09)
                                      : (ep.nextUp ? Qt.rgba(0.94, 0.77, 0.29, 0.035)
                                      : (epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent"))
                                radius: 0
                            }
                            Rectangle {
                                id: nextUpRail
                                x: theme.margin
                                width: 2
                                height: parent.height
                                visible: ep.nextUp
                                color: theme.gold
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: theme.margin
                                anchors.right: parent.right
                                anchors.rightMargin: theme.margin
                                anchors.bottom: parent.bottom
                                height: 1
                                color: theme.edge
                            }
                            Item {
                                id: episodeNumberRail
                                x: theme.margin + 2
                                width: ep.tiny ? 52 : 70
                                height: parent.height
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: page.episodeDisplayNumber(ep.modelData)
                                        color: ep.nextUp ? theme.gold : theme.ink
                                        font.family: theme.display
                                        font.pixelSize: ep.nextUp ? 25 : 21
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: page.episodeIsSpecial(ep.modelData) ? "SPECIAL" : "S" + page.episodeSeason(ep.modelData)
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 9
                                        font.letterSpacing: 1.1
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Rectangle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 1; height: parent.height - 28; color: theme.edge }
                            }
                            Rectangle {
                                id: thumb
                                x: episodeNumberRail.x + episodeNumberRail.width + 16
                                y: (parent.height - height) / 2
                                width: ep.tiny ? 122 : (ep.narrow ? 142 : 172)
                                height: ep.tiny ? 69 : (ep.narrow ? 80 : 96)
                                radius: 7
                                clip: true
                                color: "#15171f"
                                Image {
                                    anchors.fill: parent
                                    source: ep.modelData.thumbnail ? ep.modelData.thumbnail : page.sourceBackdrop()
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    visible: status === Image.Ready
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: !ep.modelData.thumbnail
                                    text: "E" + page.episodeDisplayNumber(ep.modelData)
                                    color: Qt.rgba(1, 1, 1, 0.5)
                                    font.family: theme.display
                                    font.pixelSize: 22
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 4
                                    visible: ep.progressRatio > 0.01 && !ep.watched
                                    color: Qt.rgba(0, 0, 0, 0.5)
                                    Rectangle {
                                        width: parent.width * ep.progressRatio
                                        height: parent.height
                                        color: theme.gold
                                    }
                                }
                            }
                            Column {
                                anchors.left: thumb.right
                                anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: rowStatus.visible ? rowStatus.left : rowActions.left
                                anchors.rightMargin: 20
                                spacing: ep.nextUp ? 8 : 5
                                Text {
                                    width: parent.width
                                    text: (ep.modelData.name && ep.modelData.name.length) ? ep.modelData.name
                                              : (ep.modelData.title && ep.modelData.title.length ? ep.modelData.title
                                                 : "Episode " + page.episodeDisplayNumber(ep.modelData))
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: ep.nextUp ? 17 : 15
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    // Honest single badge for a Specials (season 0) row.
                                    visible: page.episodeIsSpecial(ep.modelData)
                                    width: spBadge.implicitWidth + 14
                                    height: 18
                                    radius: 4
                                    color: Qt.rgba(1, 1, 1, 0.08)
                                    border.width: 1
                                    border.color: theme.edge
                                    Text {
                                        id: spBadge
                                        anchors.centerIn: parent
                                        text: "Special"
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        font.letterSpacing: 1
                                        font.capitalization: Font.AllUppercase
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Text {
                                    // one dim status line: state + date joined, gold ONLY for Next up
                                    visible: text.length > 0
                                    text: {
                                        var parts = [];
                                        if (ep.nextUp) parts.push("Next up");
                                        else if (ep.watched) parts.push("Watched");
                                        else if (ep.progressRatio > 0.01) parts.push(Math.round(ep.progressRatio * 100) + "% watched");
                                        if (ep.modelData.released) {
                                            var d = new Date(ep.modelData.released);
                                            parts.push(d.toLocaleDateString(Qt.locale(), Locale.ShortFormat));
                                        }
                                        return parts.join(" \u00b7 ");
                                    }
                                    color: ep.nextUp ? theme.gold : theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    visible: !!(ep.modelData.overview || ep.modelData.description)
                                    width: parent.width
                                    text: ep.modelData.overview || ep.modelData.description || ""
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    lineHeight: 1.35
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: ep.nextUp ? 2 : 1
                                    elide: Text.ElideRight
                                }
                            }
                            Row {
                                id: rowStatus
                                visible: !ep.narrow
                                anchors.right: rowActions.left
                                anchors.rightMargin: 28
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                PlayerIcon {
                                    visible: ep.watched
                                    width: 15
                                    height: 15
                                    kind: "check"
                                    ink: page.watchedInk
                                }
                                Text {
                                    text: ep.nextUp ? "NEXT UP"
                                          : ep.watched ? "WATCHED"
                                          : ep.progressRatio > 0.01 ? Math.round(ep.progressRatio * 100) + "% WATCHED"
                                          : ep.modelData.released ? "AVAILABLE" : "UPCOMING"
                                    color: ep.nextUp ? theme.gold : (ep.watched ? page.watchedInk : theme.inkDimmer)
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    font.letterSpacing: 1.1
                                    font.weight: Font.DemiBold
                                }
                            }
                            Item {
                                id: rowActions
                                anchors.right: parent.right
                                anchors.rightMargin: theme.margin + 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: 84
                                height: 38
                            }
                            MouseArea {
                                id: epMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    page.episodeKeyboardIndex = ep.absoluteIndex
                                    episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                    ep.openForPlay()
                                }
                            }
                            Rectangle {
                                // Automation identity (Lanista): keyed by the episode's own stream id.
                                // The identity stays stable while the bounded virtual window is recreated.
                                objectName: "theatreEpisodePlay_" + String(page.episodeStreamId(ep.modelData))
                                x: rowActions.x
                                y: rowActions.y
                                width: 38
                                height: 38
                                radius: 19
                                color: playMa.containsMouse ? theme.ink : Qt.rgba(1, 1, 1, 0.07)
                                border.width: 1
                                border.color: ep.nextUp ? theme.gold : theme.edge
                                PlayerIcon {
                                    anchors.centerIn: parent
                                    width: 17
                                    height: 17
                                    kind: "play"
                                    ink: playMa.containsMouse ? "#111111" : (ep.nextUp ? theme.gold : theme.ink)
                                }
                                MouseArea {
                                    id: playMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        page.episodeKeyboardIndex = ep.absoluteIndex
                                        episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                        ep.openForPlay()
                                    }
                                }
                            }
                            // Per-episode download (parity spec F4). Declared AFTER epMa so it
                            // stacks above the row's open-sources click. Three states: on disk
                            // (inert check), queued (inert download dim), ready to queue.
                            Rectangle {
                                id: epDl
                                property string sid: page.episodeStreamId(ep.modelData)
                                property bool onDisk: (typeof Download !== "undefined")
                                    ? (Download.queueRevision, Download.hasVideo(sid)) : false
                                property bool inQueue: page.queuedDownloadIds[sid] === true
                                visible: typeof Download !== "undefined"
                                x: rowActions.x + 46
                                y: rowActions.y
                                width: 38
                                height: 38
                                radius: 19
                                color: epDlMa.containsMouse && !epDl.onDisk && !epDl.inQueue
                                       ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.05)
                                border.width: 1
                                border.color: epDl.onDisk ? page.watchedInk : theme.edge
                                PlayerIcon {
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    kind: epDl.onDisk ? "check" : "download"
                                    ink: epDl.onDisk ? page.watchedInk
                                         : epDl.inQueue ? theme.inkDimmer
                                         : (epDlMa.containsMouse ? theme.ink : theme.inkDim)
                                }
                                MouseArea {
                                    id: epDlMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: (epDl.onDisk || epDl.inQueue) ? Qt.ArrowCursor : Qt.PointingHandCursor
                                    // torrent-choice spec 2026-07-11: the download action opens the source
                                    // picker in download mode; the chosen row lands back in
                                    // onDownloadRequested below and pins the request.
                                    onClicked: {
                                        page.episodeKeyboardIndex = ep.absoluteIndex
                                        episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                        page.openEpisodeDownload(ep.modelData)
                                    }
                                }
                            }
                            }
                        }
                    }
                }
            }

            CastRow {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                people: page.castPeople
            }

            Item {
                // Hemanth eyes-on 2026-07-20: breathing room between CAST and
                // MORE LIKE THIS (pageCol runs spacing: 0 — every gap is explicit).
                width: parent.width
                height: 44
                visible: (page.castPeople || []).length > 0 && (page.moreLikeCards || []).length > 0
            }

            MoreLikeThisRow {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                cards: page.moreLikeCards
                onOpenRequested: function(item) { page.openItemRequested(item) }
            }

            Text {
                visible: !page.loading && page.errorMsg.length > 0
                x: theme.margin
                text: page.errorMsg
                color: "#e6a3a3"
                font.family: theme.ui
                font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }
        }
    }
    Rectangle {
        id: episodeContextMenu
        visible: page.episodeContextOpen
        z: 80; anchors.centerIn: parent
        width: 210; height: episodeContextCol.implicitHeight + 12; radius: 12
        color: Qt.rgba(0.045,0.05,0.075,0.98); border.width: 1; border.color: theme.edge
        FocusScope {
            id: episodeContextFocus; anchors.fill: parent
            Keys.onPressed: (event) => {
                var n=page.episodeContextOptions(page.episodeKeyboardIndex).length
                if(event.key===Qt.Key_Escape){page.closeEpisodeContext(true);event.accepted=true;return}
                if(event.key===Qt.Key_Tab||event.key===Qt.Key_Backtab){event.accepted=true;return}
                if(event.key===Qt.Key_Up) page.episodeContextChoice=(page.episodeContextChoice+n-1)%n
                else if(event.key===Qt.Key_Down) page.episodeContextChoice=(page.episodeContextChoice+1)%n
                else if(event.key===Qt.Key_Home) page.episodeContextChoice=0
                else if(event.key===Qt.Key_End) page.episodeContextChoice=n-1
                else if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter||event.key===Qt.Key_Space){page.activateEpisodeContext(page.episodeContextChoice);event.accepted=true;return}
                else return
                event.accepted=true
            }
        }
        Column {
            id: episodeContextCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 6
            Repeater {
                model: page.episodeContextOptions(page.episodeKeyboardIndex)
                delegate: Rectangle {
                    required property string modelData; required property int index
                    width: episodeContextCol.width; height: 36; radius: 8
                    color: episodeContextFocus.activeFocus && page.episodeContextChoice===index ? Qt.rgba(1,1,1,0.11) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.activateEpisodeContext(index) }
                }
            }
        }
    }

    // Sticky season chrome (scroll-UX rework 2026-08-24). Lives at page level (a sibling
    // of `flick`, not inside pageCol) so it can pin above the scrolled content instead of
    // scrolling away with it. `seasonChromePlaceholder` (inside episodesCol) reserves the
    // matching space in the normal flow; this item paints the real content, tracking the
    // placeholder's content-space Y against flick.contentY.
    Item {
        id: seasonChromeFloat
        z: 18
        width: page.width
        visible: page.mediaType === "series" && page.videos.length > 0
        // The placeholder's Y in flick CONTENT coordinates. mapToItem reads the live x/y
        // of every item between here and pageCol, so this recomputes as layout settles;
        // pageCol.height is read too (a plain dependency nudge) so a height change anywhere
        // above the placeholder also re-triggers this binding.
        readonly property real placeholderContentY: (pageCol.height, seasonChromePlaceholder.mapToItem(pageCol, 0, 0).y)
        // 72px clears the fixed Back/window chrome band (Back sits at y 28, height ~34).
        readonly property bool pinned: flick.contentY > (placeholderContentY - 72)
        y: Math.max(72, placeholderContentY - flick.contentY)

        Rectangle {
            id: chromeScrim
            // Extends UPWARD to cover behind the Back/window chrome band (full band from
            // page y 0) so hero text scrolling under it never mashes into Back (Hemanth
            // 2026-07-20 eyes-on note). Transparent and in-flow-looking when not pinned.
            anchors.left: parent.left
            anchors.right: parent.right
            y: seasonChromeFloat.pinned ? -seasonChromeFloat.y : 0
            height: seasonChromeFloat.pinned ? (seasonChromeFloat.y + chromeCol.height) : 0
            color: Qt.rgba(0.016, 0.02, 0.035, 0.93)
            opacity: seasonChromeFloat.pinned ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.edge
                visible: seasonChromeFloat.pinned
            }
        }

        Column {
            id: chromeCol
            width: parent.width
            spacing: 0

                // Absolute / Seasons selector — shown only when the native mapping is
                // complete. Absolute plays one continuous run across provider seasons;
                // Seasons keeps the provider grouping and stream ids untouched.
                Row {
                    id: orderRow
                    visible: page.animeOrder && page.animeOrder.absoluteComplete === true
                    property int currentIndex: page.effectiveEpisodeOrder === "absolute" ? 0 : 1
                    onCurrentIndexChanged: if (activeFocus) page.selectEpisodeOrder(currentIndex === 0 ? "absolute" : "seasons")
                    focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => orderKeys.handle(event)
                    KeyboardCollectionController { id: orderKeys; view: orderRow; orientation: "horizontal"; count: 2 }
                    x: theme.margin
                    topPadding: 18
                    spacing: 22
                    Repeater {
                        model: ["absolute", "seasons"]
                        delegate: Item {
                            id: orderBtn
                            required property string modelData
                            required property int index
                            width: orderCol.width
                            height: orderCol.height
                            property bool on: page.effectiveEpisodeOrder === orderBtn.modelData
                            Column {
                                id: orderCol
                                spacing: 5
                                Text {
                                    text: orderBtn.modelData === "absolute" ? "Absolute" : "Seasons"
                                    color: orderBtn.on ? theme.gold : (orderMa.containsMouse ? theme.ink : theme.inkDim)
                                    font.family: theme.ui
                                    font.pixelSize: 15
                                    font.weight: orderBtn.on ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    visible: orderBtn.on
                                    width: 26
                                    height: 2
                                    radius: 2
                                    color: theme.gold
                                }
                            }
                            MouseArea {
                                id: orderMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    orderRow.currentIndex = orderBtn.index
                                    orderRow.forceActiveFocus(Qt.MouseFocusReason)
                                    page.selectEpisodeOrder(orderBtn.modelData)
                                }
                            }
                        }
                    }
                }

                Item {
                    // 11+ seasons: a Netflix-style dropdown — the sideways strip has no
                    // sideways browsing affordance in this app, so long shows use this.
                    width: parent.width
                    height: 56
                    z: 40
                    visible: page.seasons.length > 10 && page.effectiveEpisodeOrder !== "absolute"
                    Rectangle {
                        id: seasonTrigger
                        x: theme.margin
                        anchors.verticalCenter: parent.verticalCenter
                        width: seasonTrigT.implicitWidth + 52
                        height: 38
                        radius: 19
                        color: seasonTrigMa.containsMouse || page.seasonMenuOpen
                               ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        border.color: page.seasonMenuOpen ? theme.gold : theme.edge
                        Text {
                            id: seasonTrigT
                            x: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.seasonLabel()
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u25be"
                            color: page.seasonMenuOpen ? theme.gold : theme.inkDim
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: seasonTrigMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.openSeasonMenu(seasonTriggerKeyboard)
                        }
                        KeyboardAction {
                            id: seasonTriggerKeyboard; anchors.fill: parent; pointerEnabled: false
                            accessibleName: "Choose season"; focusRadius: parent.radius
                            onTriggered: page.openSeasonMenu(seasonTriggerKeyboard)
                        }
                    }
                    Item {
                        // zero-height overlay host: the menu floats, never reflows the page
                        x: theme.margin
                        anchors.top: seasonTrigger.bottom
                        anchors.topMargin: 8
                        width: 236
                        height: 0
                        Rectangle {
                            width: parent.width
                            height: Math.min(304, seasonMenuList.contentHeight + 12)
                            visible: page.seasonMenuOpen
                            radius: 14
                            color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
                            border.width: 1
                            border.color: theme.edge
                            FocusScope {
                                id: seasonMenuFocus; anchors.fill: parent; z: 4
                                Keys.onPressed: (event) => {
                                    var n=page.seasons.length
                                    if(event.key===Qt.Key_Escape){page.closeSeasonMenu(true);event.accepted=true;return}
                                    if(event.key===Qt.Key_Tab||event.key===Qt.Key_Backtab){event.accepted=true;return}
                                    if(!n)return
                                    if(event.key===Qt.Key_Up)page.seasonMenuKeyboardIndex=(page.seasonMenuKeyboardIndex+n-1)%n
                                    else if(event.key===Qt.Key_Down)page.seasonMenuKeyboardIndex=(page.seasonMenuKeyboardIndex+1)%n
                                    else if(event.key===Qt.Key_Home)page.seasonMenuKeyboardIndex=0
                                    else if(event.key===Qt.Key_End)page.seasonMenuKeyboardIndex=n-1
                                    else if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter||event.key===Qt.Key_Space){page.activateSeasonMenu(page.seasonMenuKeyboardIndex);event.accepted=true;return}
                                    else return
                                    seasonMenuList.positionViewAtIndex(page.seasonMenuKeyboardIndex,ListView.Contain);event.accepted=true
                                }
                            }
                            ListView {
                                id: seasonMenuList
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                model: page.seasons
                                boundsBehavior: Flickable.StopAtBounds
                                delegate: Rectangle {
                                    id: smRow
                                    required property var modelData
                                    required property int index
                                    width: seasonMenuList.width
                                    height: 36
                                    radius: 9
                                    color: seasonMenuFocus.activeFocus && page.seasonMenuKeyboardIndex === smRow.index
                                        ? Qt.rgba(0.94,0.77,0.29,0.10)
                                        : (smMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                                    Text {
                                        x: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: smRow.modelData === 0 ? "Specials" : "Season " + smRow.modelData
                                        color: page.activeSeason === smRow.modelData ? theme.gold : theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 13
                                        font.weight: page.activeSeason === smRow.modelData ? Font.DemiBold : Font.Normal
                                    }
                                    MouseArea {
                                        id: smMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            page.seasonMenuKeyboardIndex = smRow.index
                                            page.activateSeasonMenu(smRow.index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Flickable {
                    id: seasonStrip
                    visible: page.seasons.length <= 10 && page.effectiveEpisodeOrder !== "absolute"
                    property int currentIndex: Math.max(0, page.seasons.indexOf(page.activeSeason))
                    onCurrentIndexChanged: if (activeFocus && currentIndex < page.seasons.length) page.selectSeason(page.seasons[currentIndex])
                    focusPolicy: visible && page.seasons.length > 0 ? Qt.TabFocus : Qt.NoFocus
                    Keys.onPressed: (event) => seasonKeys.handle(event)
                    KeyboardCollectionController {
                        id: seasonKeys; view: seasonStrip; orientation: "horizontal"; count: page.seasons.length
                        positionIndexFn: function(index) {
                            var it=seasonRepeater.itemAt(index); if(!it)return
                            if(it.x < seasonStrip.contentX) seasonStrip.contentX=it.x
                            else if(it.x+it.width > seasonStrip.contentX+seasonStrip.width) seasonStrip.contentX=it.x+it.width-seasonStrip.width
                        }
                    }
                    width: parent.width
                    height: (page.seasons.length <= 10 && page.effectiveEpisodeOrder !== "absolute") ? 44 : 0
                    contentWidth: seasonRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: seasonRow
                        x: theme.margin
                        spacing: 22
                        topPadding: 18
                        Repeater {
                            id: seasonRepeater
                            model: page.seasons
                            // Delegate root is an Item, NOT the Column itself: a MouseArea
                            // with anchors.fill inside a positioner is ignored (0x0, dead
                            // clicks) and breaks the Column's layout entirely.
                            delegate: Item {
                                id: seasonBtn
                                required property var modelData
                                required property int index
                                width: seasonCol.width
                                height: seasonCol.height
                                property bool on: page.activeSeason === seasonBtn.modelData
                                Column {
                                    id: seasonCol
                                    spacing: 5
                                    Text {
                                        text: seasonBtn.modelData === 0 ? "Specials" : ("Season " + seasonBtn.modelData)
                                        color: seasonBtn.on ? theme.gold : (seasonMa.containsMouse ? theme.ink : theme.inkDim)
                                        font.family: theme.ui
                                        font.pixelSize: 15
                                        font.weight: seasonBtn.on ? Font.DemiBold : Font.Normal
                                    }
                                    Rectangle {
                                        visible: seasonBtn.on
                                        width: 26
                                        height: 2
                                        radius: 2
                                        color: theme.gold
                                    }
                                }
                                MouseArea {
                                    id: seasonMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        seasonStrip.currentIndex = seasonBtn.index
                                        seasonStrip.forceActiveFocus(Qt.MouseFocusReason)
                                        page.selectSeason(seasonBtn.modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: episodeLedgerHeader
                    x: theme.margin
                    width: parent.width - 2 * theme.margin
                    height: 86

                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7
                        Text {
                            text: page.seasonLabel()
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 25
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: {
                                var watchedCount = 0
                                var progressCount = 0
                                for (var i = 0; i < page.episodes.length; ++i) {
                                    if (page.episodeWatched(page.episodes[i]))
                                        watchedCount++
                                    else if (page.episodeProgressRatio(page.episodes[i]) > 0.01)
                                        progressCount++
                                }
                                var parts = [page.episodes.length + " episodes"]
                                if (watchedCount > 0) parts.push(watchedCount + " watched")
                                if (progressCount > 0) parts.push(progressCount + " in progress")
                                if (page.nextUpDisplayNumber() > 0) parts.push("next E" + page.nextUpDisplayNumber())
                                return parts.join("  /  ")
                            }
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.letterSpacing: 0.3
                        }
                    }

                    // Season checkout: queue every not-yet-downloaded episode of the
                    // ACTIVE season. Each job resolves its stream lazily when promoted.
                    // Hidden in Absolute view — there "episodes" is the whole run, so a
                    // "Download <season>" button would flood the queue under a wrong label.
                    Rectangle {
                        id: seasonDownloadAction
                        border.color: theme.edge
                        // the ACTION wears the glass tablet...
                        visible: typeof Download !== "undefined" && page.episodes.length > 0
                                 && page.effectiveEpisodeOrder !== "absolute"
                        anchors.right: jumpHost.visible ? jumpHost.left : parent.right
                        anchors.rightMargin: jumpHost.visible ? 10 : 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 176
                        height: 40
                        radius: 9
                        color: dlSeasonMa.containsMouse && !page.seasonQueued[page.activeSeason]
                               ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        Row {
                            anchors.centerIn: parent
                            spacing: 9
                            PlayerIcon {
                                width: 16
                                height: 16
                                kind: page.seasonQueued[page.activeSeason] ? "check" : "download"
                                ink: page.seasonQueued[page.activeSeason] ? page.watchedInk : theme.inkDim
                            }
                            Text {
                                id: dlSeasonT
                                text: page.seasonQueued[page.activeSeason]
                                      ? "Season queued"
                                      : "Download season"
                                color: page.seasonQueued[page.activeSeason] ? theme.inkDim
                                     : (dlSeasonMa.containsMouse ? theme.ink : theme.inkDim)
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: dlSeasonMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: !page.seasonQueued[page.activeSeason]
                            onClicked: page.openSeasonPicker()
                        }
                        KeyboardAction {
                            id: seasonDownloadKeyboard; anchors.fill: parent; pointerEnabled: false
                            focusEnabled: seasonDownloadAction.visible && !page.seasonQueued[page.activeSeason]
                            accessibleName: "Download season"; focusRadius: parent.radius
                            onTriggered: page.openSeasonPicker()
                        }
                    }

                    Item {
                        id: jumpHost
                        width: 102
                        height: 40
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.episodes.length >= 12
                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: page.episodeJumpOpen ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                            border.width: 1
                            border.color: page.episodeJumpOpen ? theme.gold : theme.edge
                            Text {
                                anchors.centerIn: parent
                                text: "Go to episode"
                                color: page.episodeJumpOpen ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.toggleEpisodeJump(jumpToggleKeyboard)
                            }
                            KeyboardAction {
                                id: jumpToggleKeyboard; anchors.fill: parent; pointerEnabled: false
                                accessibleName: "Go to episode"; focusRadius: parent.radius
                                onTriggered: page.toggleEpisodeJump(jumpToggleKeyboard)
                            }
                        }
                    }
                }

                Item { width: parent.width; height: 0; z: 30   // overlay host: the panel floats
                Rectangle {
                    id: episodeJumpPanel
                    x: Math.max(theme.margin, parent.width - theme.margin - 292)
                    y: 6
                    width: 292
                    height: jumpRanges.height + 58
                    radius: 12
                    visible: page.episodeJumpOpen
                    color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
                    border.width: 1
                    border.color: theme.edge
                    z: 3

                    Row {
                        id: jumpInputRow
                        x: 12
                        y: 12
                        spacing: 8
                        Rectangle {
                            width: 184
                            height: 34
                            radius: 8
                            color: Qt.rgba(1, 1, 1, 0.06)
                            border.width: 1
                            border.color: jumpInput.activeFocus ? theme.gold : theme.edge
                            TextInput {
                                id: jumpInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                text: page.episodeJumpDraft
                                color: theme.ink
                                selectionColor: theme.gold
                                selectedTextColor: "#111111"
                                font.family: theme.ui
                                font.pixelSize: 13
                                validator: IntValidator { bottom: 1; top: Math.max(1, page.episodes.length) }
                                onTextChanged: page.episodeJumpDraft = text
                                Keys.onReturnPressed: page.submitEpisodeJump()
                                Keys.onEnterPressed: page.submitEpisodeJump()
                                Keys.onEscapePressed: page.closeEpisodeJump(true)
                                Keys.onTabPressed: { jumpSubmitKeyboard.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
                                Keys.onBacktabPressed: { jumpRanges.forceActiveFocus(Qt.BacktabFocusReason); event.accepted = true }
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                visible: jumpInput.text.length === 0 && !jumpInput.activeFocus
                                text: "1 - " + page.episodes.length
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 13
                            }
                        }
                        Rectangle {
                            width: 72
                            height: 34
                            radius: 8
                            color: theme.ink
                            opacity: page.episodeJumpDraft.length ? 1.0 : 0.45
                            Text {
                                anchors.centerIn: parent
                                text: "Jump"
                                color: "#111111"
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.submitEpisodeJump()
                            }
                            KeyboardAction {
                                id: jumpSubmitKeyboard; anchors.fill: parent; pointerEnabled: false
                                focusEnabled: page.episodeJumpOpen && page.episodeJumpDraft.length > 0
                                accessibleName: "Jump to episode"; focusRadius: parent.radius
                                onTriggered: page.submitEpisodeJump()
                            }
                        }
                    }

                    Flow {
                        id: jumpRanges
                        x: 12
                        y: 56
                        width: parent.width - 24
                        spacing: 6
                        property int currentIndex: 0
                        focusPolicy: page.episodeJumpOpen && rangeRepeater.count > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Tab) { jumpInput.forceActiveFocus(Qt.TabFocusReason); event.accepted=true; return }
                            if (event.key === Qt.Key_Backtab) { jumpSubmitKeyboard.forceActiveFocus(Qt.BacktabFocusReason); event.accepted=true; return }
                            jumpRangeKeyboard.handle(event)
                        }
                        KeyboardCollectionController {
                            id: jumpRangeKeyboard; view: jumpRanges; orientation: "grid"; columns: 3
                            count: rangeRepeater.count
                            onActivated: (index) => page.jumpToEpisodeNumber(index * 50 + 1)
                        }
                        Repeater {
                            id: rangeRepeater
                            model: Math.ceil(page.episodes.length / 50)
                            delegate: Rectangle {
                                id: rangeBtn
                                required property int index
                                property int start: index * 50 + 1
                                property int end: Math.min(start + 49, page.episodes.length)
                                width: label.implicitWidth + 18
                                height: 25
                                radius: 6
                                color: jumpRanges.activeFocus && jumpRanges.currentIndex === rangeBtn.index
                                    ? Qt.rgba(0.94,0.77,0.29,0.10) : Qt.rgba(1, 1, 1, 0.06)
                                border.width: jumpRanges.activeFocus && jumpRanges.currentIndex === rangeBtn.index ? 2 : 1
                                border.color: jumpRanges.activeFocus && jumpRanges.currentIndex === rangeBtn.index ? theme.gold : theme.edge
                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: rangeBtn.start + "-" + rangeBtn.end
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        jumpRanges.currentIndex = rangeBtn.index
                                        jumpRanges.forceActiveFocus(Qt.MouseFocusReason)
                                        page.jumpToEpisodeNumber(rangeBtn.start)
                                    }
                                }
                            }
                        }
                    }
                }

                }

                Item {
                    id: episodeLedgerColumns
                    x: theme.margin
                    width: parent.width - 2 * theme.margin
                    height: 30
                    Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: "EPISODE"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.3; font.weight: Font.DemiBold }
                    Text { x: 276; anchors.verticalCenter: parent.verticalCenter; text: "STORY"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.3; font.weight: Font.DemiBold }
                    Text { anchors.right: parent.right; anchors.rightMargin: 118; anchors.verticalCenter: parent.verticalCenter; text: "STATUS"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.3; font.weight: Font.DemiBold }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.edge }
                }
        }
    }


    Column {
        id: loadingState
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.title
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 34
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading..."
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
    }

    ScrollGlide { id: theatreSeriesGlide; flick: flick }
    KeyboardScrollController { id: theatreSeriesScrollKeys; flick: flick; glide: theatreSeriesGlide }

    SourcesSheet {
        id: sources
        z: 60
        backdrop: page.backdrop
        onPlayRequested: (infoHash, fileIdx, title, backdropUrl, subType, subId, streamCandidates, playbackContext) => page.playRequested(infoHash, fileIdx, title, backdropUrl, subType, subId, streamCandidates, playbackContext)
        onDownloadRequested: (row) => {
            if (page.pendingSeasonPick) {
                // season-mode pick: the chosen FULL-SEASON torrent pins the checkout
                page.queueSeasonDownload(row)
                page.pendingSeasonPick = false
            } else if (page.pendingDownloadEpisode) {
                // download-mode pick (episode action opened the sheet as a picker)
                page.queueEpisodeDownload(page.pendingDownloadEpisode, row)
                page.pendingDownloadEpisode = null
            } else if (page.mediaType === "series" && page.sheetEpisode) {
                // play-mode per-row download: this episode, pinned to the clicked torrent
                page.queueEpisodeDownload(page.sheetEpisode, row)
            } else if (page.mediaType === "movie") {
                page.queueMovieDownload(row)
            }
        }
        // no full-season torrent in the answer → the original auto path (rank-best
        // per episode), and the button flips to "queued" exactly as before
        onSeasonNoPacks: {
            if (page.pendingSeasonPick)
                page.queueSeasonDownload(null)
            page.pendingSeasonPick = false
        }
        // Backing out of a picker un-arms it — a stale pick marker must never
        // route a later play-mode sheet's row into the wrong queue action.
        onOpenChanged: if (!sources.open) {
            page.pendingSeasonPick = false
            page.pendingDownloadEpisode = null
        }
    }
}
