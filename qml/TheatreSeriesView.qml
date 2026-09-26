// TheatreSeriesView — the rebuilt Theatre title page (movies and series), built to replace
// TheatreSeries.qml. World feel design, 2026-09-24 (docs/superpowers/specs/
// 2026-09-24-colosseum-world-feel-design.md): the shared TitlePageBar with the world pills,
// the title art kept, facts that wrap, a synopsis you can expand, and the long-list episode
// ledger — a one-line sticky bar, 96px rows (about eight on screen), a range strip for long
// runs like One Piece, and an inline Go-to field. Every behaviour of the old page is kept:
// Absolute/Seasons order, season picker (dropdown past ten), per-episode progress, Next Up,
// descriptions, per-episode Play/Download, season download, sources sheet, downloaded-first
// play, Library, Ratings & Reviews, Notifications, cast, More Like This.
//
// The data/behaviour half below is carried over from TheatreSeries.qml unchanged except for
// the geometry seams (uniform 96px rows, the new sticky header height) so the two pages can
// never disagree about what an episode, a queue or a download is.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "ratingsreviews"
import "TheatreApi.js" as TheatreApi
import "AnimeEpisodePresentation.js" as AnimeEpisodePresentation
import "TheatreFacts.js" as TheatreFacts

Item {
    id: page
    signal libraryRemovalRequested(var entry)
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
    signal ratingsReviewsRequested(var context, var invokingItem, var fallbackItem)
    signal worldRequested(string world)
    signal searchRequested()
    // Where Back goes, named in the Back pill ("Theatre", "Search", a title). Main sets it.
    property string backLabel: "Theatre"

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
    readonly property int compactEpisodeRowHeight: 96
    readonly property int nextUpEpisodeRowHeight: 96
    readonly property int episodeWindowOverscanRows: 3
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
    property real stickyHeaderHeight: titleBar.height + (ledgerBarFloat.visible ? ledgerBarFloat.height : 0)

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
        var top = ledgerPlaceholder.mapToItem(pageCol, 0, 0).y - titleBar.height
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
    Keys.onReleased: (event) => theatreSeriesScrollKeys.handleRelease(event)

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

    // =====================================================================================
    // Visual half (new). Everything below is the rebuilt page; the logic above is unchanged.
    // =====================================================================================

    readonly property color focusGold: Qt.rgba(0.94, 0.77, 0.29, 1.0)
    readonly property int ledgerBarHeight: 56
    readonly property int episodeRangeSize: 100
    readonly property bool longRun: page.episodes.length > page.episodeRangeSize
    property bool synopsisExpanded: false
    property real _lastPointerX: -1
    property real _lastPointerY: -1
    // The first episode row visible under the pinned bars — drives the range strip's marker.
    readonly property int firstVisibleEpisodeIndex:
        page.episodeIndexAtOffset(Math.max(0, flick.contentY + page.stickyHeaderHeight - page.episodeVirtualContentY))

    onRequestedSourceIdChanged: page.synopsisExpanded = false
    // Land keyboard focus on the episode the page opens at (next-up), not on row 1.
    onNextUpEpisodeIndexChanged: {
        if (page.episodeKeyboardIndex === 0 && page.nextUpEpisodeIndex > 0)
            page.episodeKeyboardIndex = page.nextUpEpisodeIndex
    }

    function focusEpisode(index) {
        if (index < 0 || index >= page.episodes.length) return
        page.episodeKeyboardIndex = index
        page.scrollToEpisodeIndex(index)
        episodeVirtualSpace.forceActiveFocus(Qt.OtherFocusReason)
    }
    function goToEpisodeDraft() {
        var n = parseInt(page.episodeJumpDraft, 10)
        if (isNaN(n)) return
        var idx = page.episodeIndex(n)
        if (idx < 0) return
        page.episodeJumpDraft = ""
        jumpInput.text = ""
        page.episodeJumpOpen = false
        page.focusEpisode(idx)
    }
    function jumpToNextUp() {
        if (page.nextUpEpisodeIndex >= 0) page.focusEpisode(page.nextUpEpisodeIndex)
    }
    // Status names only what differs from the ordinary "available" state; the date always shows.
    function episodeStatusText(v, nextUp, watched, ratio, onDisk, inQueue) {
        var parts = []
        if (nextUp) parts.push("Next up")
        else if (watched) parts.push("Watched")
        else if (ratio > 0.01) parts.push(Math.round(ratio * 100) + "% watched")
        if (onDisk) parts.push("Downloaded")
        else if (inQueue) parts.push("Downloading")
        if (v && v.released) {
            var d = new Date(v.released)
            if (d.getTime() > Date.now()) parts.push("Airs " + d.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
            else parts.push(d.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
        } else if (v) {
            parts.push("Upcoming")
        }
        return parts.join("  ·  ")
    }
    function ledgerHeading() {
        if (page.effectiveEpisodeOrder === "absolute") return "All episodes"
        return page.seasonLabel()
    }
    function ledgerCountText() {
        var watchedCount = 0
        for (var i = 0; i < page.episodes.length; ++i)
            if (page.episodeWatched(page.episodes[i])) watchedCount++
        var n = page.episodes.length
        var s = n.toLocaleString(Qt.locale(), "f", 0) + (n === 1 ? " episode" : " episodes")
        if (watchedCount > 0) s += "  ·  " + watchedCount.toLocaleString(Qt.locale(), "f", 0) + " watched"
        return s
    }

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#06070b" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true
        hideSource: false
        visible: page.backdrop !== null
        opacity: 0.32
    }
    Rectangle { anchors.fill: parent; color: Qt.rgba(0.024, 0.027, 0.043, 0.72) }

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
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            // ---- hero: the title's own art, washed, with the promise and the actions ----
            Item {
                id: hero
                width: parent.width
                height: Math.max(heroCopy.implicitHeight + titleBar.height + 72, Math.min(640, page.height * 0.6))
                Image {
                    id: heroArt
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    sourceSize.width: 1920
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                    cache: true
                    opacity: status === Image.Ready ? 0.9 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                Rectangle {     // left-to-right: keeps the copy legible over any art
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.rgba(0.024, 0.027, 0.043, 0.92) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.024, 0.027, 0.043, 0.45) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.024, 0.027, 0.043, 0.15) }
                    }
                }
                Rectangle {     // bottom fade into the page ground
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.024, 0.027, 0.043, 0.0) }
                        GradientStop { position: 0.62; color: Qt.rgba(0.024, 0.027, 0.043, 0.25) }
                        GradientStop { position: 1.0; color: "#06070b" }
                    }
                }

                Column {
                    id: heroCopy
                    anchors.left: parent.left
                    anchors.leftMargin: theme.margin
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 34
                    width: Math.min(820, parent.width - 2 * theme.margin)
                    spacing: 12
                    // The copy fades as it climbs, so it is gone before it reaches the title bar.
                    opacity: Math.max(0, 1 - flick.contentY / Math.max(1, hero.height - titleBar.height - 40))

                    Text {
                        text: page.mediaType === "series" ? "Series  ·  Theatre" : "Movie  ·  Theatre"
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
                        font.pixelSize: 60
                        font.weight: Font.DemiBold
                        lineHeight: 0.98
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Flow {
                        width: parent.width
                        spacing: 12
                        Text {
                            visible: page.year.length > 0
                            text: page.year
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        Text {
                            visible: page.genresLine.length > 0
                            text: page.genresLine.split(" - ").join("  ·  ")
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 14
                        }
                        Row {
                            visible: page.rating.length > 0
                            spacing: 7
                            height: 20
                            Rectangle {
                                width: 34; height: 18; radius: 3
                                color: "#F5C518"
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: "IMDb"
                                    color: "#111111"
                                    font.family: theme.ui; font.pixelSize: 10; font.weight: Font.Black
                                    font.letterSpacing: -0.45
                                }
                            }
                            Text {
                                text: page.rating
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            visible: page.runtime.length > 0
                            text: page.runtime
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 14
                        }
                    }
                    // Synopsis: three lines at rest, the whole of it on "More".
                    Column {
                        width: parent.width
                        spacing: 4
                        visible: page.synopsis.length > 0
                        Text {
                            id: synopsisText
                            width: Math.min(parent.width, 720)
                            text: page.synopsis
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 15
                            lineHeight: 1.45
                            wrapMode: Text.WordWrap
                            maximumLineCount: page.synopsisExpanded ? 40 : 3
                            elide: Text.ElideRight
                        }
                        Item {
                            visible: synopsisText.truncated || page.synopsisExpanded
                            width: moreText.implicitWidth + 8
                            height: 26
                            Text {
                                id: moreText
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                text: page.synopsisExpanded ? "Less" : "More"
                                color: synopsisMore.interactionActive ? theme.gold : theme.ink
                                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                            }
                            KeyboardAction {
                                id: synopsisMore
                                objectName: "theatreSynopsisMore"
                                anchors.fill: parent
                                accessibleName: page.synopsisExpanded ? "Show less" : "Read the full synopsis"
                                focusRadius: 6
                                focusColor: page.focusGold
                                onTriggered: page.synopsisExpanded = !page.synopsisExpanded
                            }
                        }
                    }
                    Flow {
                        width: parent.width
                        spacing: 12
                        topPadding: 6

                        Rectangle {
                            objectName: "theatreSeriesWatch"
                            visible: page.mediaType !== "series" || page.heroEpisode() !== null
                            width: watchRow.implicitWidth + 40
                            height: 44
                            radius: 12
                            color: theme.gold
                            scale: heroWatchKeyboard.activeFocus ? 1.04 : 1.0
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                            Rectangle {     // the primary action's focus: a dark gap, then the gold ring
                                anchors.fill: parent
                                anchors.margins: -6
                                radius: parent.radius + 6
                                color: "transparent"
                                border.width: 3
                                border.color: theme.gold
                                visible: heroWatchKeyboard.activeFocus
                            }
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
                                    text: {
                                        var ep = page.mediaType === "series" ? page.nextUpEpisode() : null
                                        if (!ep && page.mediaType === "series") ep = page.heroEpisode()
                                        if (!ep) return "Watch"
                                        var verb = page.episodeProgressRatio(ep) > 0.01 ? "Resume" : "Watch"
                                        return verb + "  S" + page.episodeSeason(ep) + " · E" + page.episodeDisplayNumber(ep)
                                    }
                                    color: "#1a1306"
                                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            KeyboardAction {
                                id: heroWatchKeyboard
                                anchors.fill: parent
                                accessibleName: "Watch " + page.title
                                focusRadius: parent.radius
                                showFocusFrame: false
                                onTriggered: {
                                    // Series: the primary plays the next-up episode (the one you are
                                    // actually on), falling back to the first visible episode.
                                    if (page.mediaType === "series" && page.nextUpEpisode())
                                        page.openEpisodeForPlay(page.nextUpEpisode())
                                    else
                                        page.openHeroForPlay()
                                }
                            }
                        }
                        LibraryButton {
                            world: "theatre"
                            entry: page.collectionEntry()
                            onRemoveRequested: (entry) => page.libraryRemovalRequested(entry)
                        }
                        RatingsReviewsAction {
                            objectName: "theatreRatingsReviewsAction"
                            width: 176; height: 42
                            titleRegistry: (typeof RatingsReviewsIdentity !== "undefined")
                                           ? RatingsReviewsIdentity : null
                            world: "theatre"
                            kind: page.mediaType === "series" ? "series" : "movie"
                            directId: (page.itemData && String(page.itemData.id || "").indexOf("ct1:") === 0)
                                      ? String(page.itemData.id) : ""
                            aliases: {
                                // Same identity rules as TheatreSeries.qml: both the door we entered
                                // by and the pivoted Cinemeta id travel as aliases, but only once the
                                // meta load has settled, so a title never splits under two ids.
                                var orig = (page.itemData && page.itemData.id)
                                           ? String(page.itemData.id) : ""
                                if (orig.length && orig.indexOf("tt") !== 0
                                        && (page.loading || page.resolvedId.length === 0))
                                    return []
                                var out = []
                                var rid = page.currentId()
                                if (rid.length)
                                    out.push({ namespace: "theatre-source-id", value: rid })
                                if (orig.length && orig !== rid)
                                    out.push({ namespace: "theatre-source-id", value: orig })
                                return out
                            }
                            titleText: page.title
                            subtitleText: page.genresLine
                            year: Number(page.year || 0)
                            artwork: page.banner.length ? page.banner : page.cover
                            origin: "theatre-detail"
                            readIds: ({ imdb: page.currentId().indexOf("tt") === 0 ? page.currentId() : "",
                                        tmdb: page.tmdbId > 0 ? String(page.tmdbId) : "",
                                        fixtureVariant: (typeof RatingsReviewsProviderFixture !== "undefined")
                                                        ? String(RatingsReviewsProviderFixture || "") : "" })
                            returnTarget: titleBar.backItem
                            onRatingsReviewsRequested: function(context, invokingItem, fallbackItem) {
                                page.ratingsReviewsRequested(context, invokingItem, fallbackItem)
                            }
                        }
                        // Notify-about-new-episodes toggle — only for SAVED series (spec §4.5).
                        Rectangle {
                            id: notifPill
                            visible: page.mediaType === "series" && (typeof Collection !== "undefined")
                                     && (Collection.revision, Collection.has("theatre", String(page.collectionEntry().id)))
                            readonly property bool on: (typeof Collection !== "undefined") ? (Collection.revision, page.libNotifOn()) : false
                            width: notifText.implicitWidth + 32
                            height: 42
                            radius: 11
                            color: notificationKeyboard.interactionActive ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: theme.edge
                            Text {
                                id: notifText
                                anchors.centerIn: parent
                                text: notifPill.on ? "Notifications on" : "Notifications off"
                                color: notifPill.on ? theme.ink : theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            }
                            KeyboardAction {
                                id: notificationKeyboard
                                anchors.fill: parent
                                accessibleName: notifPill.on ? "Turn notifications off" : "Turn notifications on"
                                focusRadius: parent.radius
                                focusColor: page.focusGold
                                onTriggered: page.toggleLibNotif()
                            }
                        }
                    }
                }
            }

            // ---- the facts: two wrapping columns, never clipped ----
            Item {
                width: parent.width
                height: page.factRows.length ? factsGrid.implicitHeight + 36 : 0
                visible: page.factRows.length > 0
                Grid {
                    id: factsGrid
                    x: theme.margin
                    y: 8
                    width: parent.width - 2 * theme.margin
                    columns: width > 1100 ? 2 : 1
                    columnSpacing: 56
                    rowSpacing: 10
                    readonly property real cellWidth: (width - (columns - 1) * columnSpacing) / columns
                    Repeater {
                        model: page.factRows
                        Row {
                            id: factRow
                            required property var modelData
                            width: factsGrid.cellWidth
                            spacing: 18
                            Text {
                                width: 104
                                text: factRow.modelData.k
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 13
                            }
                            Text {
                                width: factRow.width - 122
                                text: factRow.modelData.v
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                lineHeight: 1.3
                            }
                        }
                    }
                }
            }

            // ---- episodes: the long-list ledger ----
            Item {
                id: episodesSection
                width: parent.width
                height: episodesCol.height
                visible: page.mediaType === "series" && page.videos.length > 0

                Column {
                    id: episodesCol
                    width: parent.width
                    spacing: 0

                    // Reserves the sticky bar's slot in the flow; ledgerBarFloat paints it.
                    Item {
                        id: ledgerPlaceholder
                        width: parent.width
                        height: page.ledgerBarHeight + 10
                    }

                    Item {
                        id: episodeVirtualSpace
                        objectName: "theatreEpisodeVirtualSpace"
                        width: parent.width
                        height: page.episodeContentHeight
                        property int currentIndex: page.episodeKeyboardIndex
                        onCurrentIndexChanged: page.episodeKeyboardIndex = currentIndex
                        focusPolicy: page.episodes.length > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: (event) => {
                            // Typing digits on the list starts a Go-to, like a TV remote.
                            if (event.text.length === 1 && event.text >= "0" && event.text <= "9"
                                    && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                                page.episodeJumpOpen = true
                                jumpInput.text = event.text
                                jumpInput.forceActiveFocus(Qt.ShortcutFocusReason)
                                jumpInput.cursorPosition = jumpInput.text.length
                                event.accepted = true
                                return
                            }
                            episodeKeys.handle(event)
                        }
                        KeyboardCollectionController {
                            id: episodeKeys; view: episodeVirtualSpace; orientation: "vertical"
                            count: page.episodes.length; contextEnabled: true
                            pageStep: Math.max(1, Math.floor((flick.height - page.stickyHeaderHeight) / page.compactEpisodeRowHeight))
                            positionIndexFn: function(index) { page.scrollToEpisodeIndex(index) }
                            onActivated: (index) => page.openEpisodeForPlay(page.episodes[index])
                            onContextRequested: (index) => page.openEpisodeContext(index)
                        }

                        Repeater {
                            id: episodeWindowRepeater
                            objectName: "theatreEpisodeWindow"
                            model: page.episodeWindowModel

                            delegate: Item {
                                id: ep
                                required property int index
                                required property var modelData
                                readonly property int absoluteIndex: page.episodeWindowStart + index
                                readonly property real progressRatio: page.episodeProgressRatio(modelData)
                                readonly property bool watched: page.episodeWatched(modelData)
                                readonly property bool nextUp: ep.absoluteIndex === page.nextUpEpisodeIndex
                                readonly property bool focused: episodeVirtualSpace.activeFocus
                                                               && page.episodeKeyboardIndex === ep.absoluteIndex
                                readonly property bool narrow: episodeVirtualSpace.width < 980
                                readonly property string sid: page.episodeStreamId(modelData)
                                readonly property bool onDisk: (typeof Download !== "undefined")
                                    ? (Download.queueRevision, Download.hasVideo(ep.sid)) : false
                                readonly property bool inQueue: page.queuedDownloadIds[ep.sid] === true
                                readonly property string still: ep.modelData.thumbnail
                                    ? TheatreApi.normalizeArtUrl(String(ep.modelData.thumbnail)) : ""
                                // Stills load only for rows that stay on screen: a fling past
                                // hundreds of episodes requests nothing for the rows it skipped.
                                property bool stillWanted: false
                                Timer { interval: 150; running: true; repeat: false; onTriggered: ep.stillWanted = true }

                                y: page.episodeOffsetForIndex(ep.absoluteIndex)
                                width: episodeVirtualSpace.width
                                height: page.compactEpisodeRowHeight

                                Rectangle {
                                    id: rowGround
                                    x: theme.margin
                                    width: parent.width - 2 * theme.margin
                                    height: parent.height - 4
                                    y: 2
                                    radius: 10
                                    color: ep.focused ? Qt.rgba(0.94, 0.77, 0.29, 0.10)
                                         : (ep.nextUp ? Qt.rgba(0.94, 0.77, 0.29, 0.05)
                                         : (rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"))
                                    border.width: ep.focused ? 3 : 0
                                    border.color: theme.gold
                                }
                                Rectangle {
                                    visible: ep.nextUp && !ep.focused
                                    x: theme.margin
                                    y: 14
                                    width: 3
                                    height: parent.height - 28
                                    radius: 2
                                    color: theme.gold
                                }

                                Column {
                                    id: numberCol
                                    x: theme.margin + 12
                                    width: 58
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: page.episodeDisplayNumber(ep.modelData)
                                        color: ep.nextUp ? theme.gold : theme.ink
                                        font.family: theme.display
                                        font.pixelSize: 22
                                        font.weight: Font.DemiBold
                                        font.features: { "tnum": 1 }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: page.episodeIsSpecial(ep.modelData) ? "SPECIAL" : "S" + page.episodeSeason(ep.modelData)
                                        color: theme.inkDimmer
                                        font.family: theme.ui; font.pixelSize: 9
                                        font.letterSpacing: 1.1; font.weight: Font.DemiBold
                                    }
                                }

                                Rectangle {
                                    id: thumb
                                    x: numberCol.x + numberCol.width + 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 128
                                    height: 72
                                    radius: 6
                                    clip: true
                                    color: "#15171f"
                                    Image {
                                        anchors.fill: parent
                                        source: ep.stillWanted ? (ep.still.length ? ep.still : page.sourceBackdrop()) : ""
                                        sourceSize.width: 256
                                        sourceSize.height: 144
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        visible: status === Image.Ready
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: !ep.still.length
                                        text: "E" + page.episodeDisplayNumber(ep.modelData)
                                        color: Qt.rgba(1, 1, 1, 0.5)
                                        font.family: theme.display; font.pixelSize: 20
                                    }
                                    Rectangle {
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                        height: 3
                                        visible: ep.progressRatio > 0.01 && !ep.watched
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        Rectangle { width: parent.width * ep.progressRatio; height: parent.height; color: theme.gold }
                                    }
                                    Rectangle {     // watched: a quiet check over the still
                                        visible: ep.watched
                                        anchors.right: parent.right; anchors.top: parent.top
                                        anchors.margins: 5
                                        width: 20; height: 20; radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.6)
                                        Text { anchors.centerIn: parent; text: "✓"; color: page.watchedInk; font.pixelSize: 12; font.weight: Font.Bold }
                                    }
                                }

                                Column {
                                    anchors.left: thumb.right
                                    anchors.leftMargin: 18
                                    anchors.right: rowActions.left
                                    anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    Row {
                                        width: parent.width
                                        spacing: 10
                                        Text {
                                            width: Math.min(implicitWidth, parent.width - (spBadge.visible ? spBadge.width + 10 : 0))
                                            text: (ep.modelData.name && ep.modelData.name.length) ? ep.modelData.name
                                                  : (ep.modelData.title && ep.modelData.title.length ? ep.modelData.title
                                                     : "Episode " + page.episodeDisplayNumber(ep.modelData))
                                            color: theme.ink
                                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            id: spBadge
                                            visible: page.episodeIsSpecial(ep.modelData)
                                            width: spText.implicitWidth + 12; height: 17; radius: 4
                                            color: Qt.rgba(1, 1, 1, 0.08)
                                            border.width: 1; border.color: theme.edge
                                            Text { id: spText; anchors.centerIn: parent; text: "SPECIAL"; color: theme.inkDim
                                                   font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1; font.weight: Font.DemiBold }
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: page.episodeStatusText(ep.modelData, ep.nextUp, ep.watched, ep.progressRatio, ep.onDisk, ep.inQueue)
                                        visible: text.length > 0
                                        color: ep.nextUp ? theme.gold : (ep.watched ? page.watchedInk : theme.inkDimmer)
                                        font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: !!(ep.modelData.overview || ep.modelData.description)
                                        width: parent.width
                                        text: ep.modelData.overview || ep.modelData.description || ""
                                        color: theme.inkDimmer
                                        font.family: theme.ui; font.pixelSize: 13
                                        lineHeight: 1.3
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: rowMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // Hover is focus: moving the mouse over a row makes it the row
                                    // the keyboard continues from, so there is only ever one highlight.
                                    onPositionChanged: (mouse) => {
                                        // Only real pointer movement moves focus. When the list scrolls under a
                                        // still cursor, Qt reports a move at the same screen point: ignore it,
                                        // or focus and scrolling would chase each other.
                                        var g = rowMa.mapToItem(null, mouse.x, mouse.y)
                                        if (Math.abs(g.x - page._lastPointerX) < 1 && Math.abs(g.y - page._lastPointerY) < 1) return
                                        page._lastPointerX = g.x; page._lastPointerY = g.y
                                        if (page.episodeKeyboardIndex !== ep.absoluteIndex || !episodeVirtualSpace.activeFocus) {
                                            page.episodeKeyboardIndex = ep.absoluteIndex
                                            episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                        }
                                    }
                                    onClicked: {
                                        page.episodeKeyboardIndex = ep.absoluteIndex
                                        episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                        page.openEpisodeForPlay(ep.modelData)
                                    }
                                }

                                Row {
                                    id: rowActions
                                    anchors.right: parent.right
                                    anchors.rightMargin: theme.margin + 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8
                                    Rectangle {
                                        // Automation identity (Lanista): keyed by the episode's stream id,
                                        // stable while the virtual window recreates rows.
                                        objectName: "theatreEpisodePlay_" + String(ep.sid)
                                        width: 40; height: 40; radius: 20
                                        color: playMa.containsMouse ? theme.ink : Qt.rgba(1, 1, 1, 0.07)
                                        border.width: 1
                                        border.color: ep.nextUp ? theme.gold : theme.edge
                                        // Plain glyphs, not PlayerIcon: its per-icon colour effect is too
                                        // costly for rows that are rebuilt on every jump through 1,000+ episodes.
                                        Text {
                                            anchors.centerIn: parent
                                            anchors.horizontalCenterOffset: 2
                                            text: "▶︎"
                                            color: playMa.containsMouse ? "#111111" : (ep.nextUp ? theme.gold : theme.ink)
                                            font.family: "Segoe UI Symbol"
                                            font.pixelSize: 13
                                        }
                                        MouseArea {
                                            id: playMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                page.episodeKeyboardIndex = ep.absoluteIndex
                                                episodeVirtualSpace.forceActiveFocus(Qt.MouseFocusReason)
                                                page.openEpisodeForPlay(ep.modelData)
                                            }
                                        }
                                    }
                                    Rectangle {
                                        visible: typeof Download !== "undefined"
                                        width: 40; height: 40; radius: 20
                                        color: dlMa.containsMouse && !ep.onDisk && !ep.inQueue
                                               ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.05)
                                        border.width: 1
                                        border.color: ep.onDisk ? page.watchedInk : theme.edge
                                        Text {
                                            anchors.centerIn: parent
                                            text: ep.onDisk ? "✓" : "↓"
                                            color: ep.onDisk ? page.watchedInk
                                                 : ep.inQueue ? theme.inkDimmer
                                                 : (dlMa.containsMouse ? theme.ink : theme.inkDim)
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                        }
                                        MouseArea {
                                            id: dlMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: (ep.onDisk || ep.inQueue) ? Qt.ArrowCursor : Qt.PointingHandCursor
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
                    Item { width: 1; height: 44 }
                }
            }

            CastRow {
                x: theme.margin
                width: parent.width - 2 * theme.margin
                people: page.castPeople
            }
            Item {
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
                font.family: theme.ui; font.pixelSize: 13
                topPadding: 18
            }
            Item { width: 1; height: 110 }
        }
    }

    // ---- the ledger's one-line sticky bar: order · season · Next · Download season · Go to ----
    Item {
        id: ledgerBarFloat
        objectName: "theatreLedgerBar"
        z: 30
        width: page.width
        height: page.ledgerBarHeight
        visible: page.mediaType === "series" && page.videos.length > 0 && !page.loading
        readonly property real placeholderContentY: (pageCol.height, ledgerPlaceholder.mapToItem(pageCol, 0, 0).y)
        readonly property bool pinned: flick.contentY > (placeholderContentY - titleBar.height)
        y: Math.max(titleBar.height, placeholderContentY - flick.contentY)

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.024, 0.027, 0.043, 0.97)
            opacity: ledgerBarFloat.pinned ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1; color: theme.edge }
        }

        Row {
            id: ledgerLeft
            x: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: page.ledgerHeading()
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Text {
                    text: page.ledgerCountText()
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.3
                }
            }

            // Absolute / Seasons — only when the native mapping is complete.
            Rectangle {
                visible: page.animeOrder && page.animeOrder.absoluteComplete === true
                anchors.verticalCenter: parent.verticalCenter
                height: 36
                width: orderRow.implicitWidth + 8
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: theme.edge
                Row {
                    id: orderRow
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: ["absolute", "seasons"]
                        delegate: Item {
                            id: orderBtn
                            required property string modelData
                            readonly property bool on: page.effectiveEpisodeOrder === orderBtn.modelData
                            width: orderText.implicitWidth + 28
                            height: 28
                            Rectangle { anchors.fill: parent; radius: 14
                                        color: orderBtn.on ? Qt.rgba(1, 1, 1, 0.14) : (orderAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08) : "transparent") }
                            Text {
                                id: orderText
                                anchors.centerIn: parent
                                text: orderBtn.modelData === "absolute" ? "Absolute" : "Seasons"
                                color: orderBtn.on ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: orderBtn.on ? Font.DemiBold : Font.Normal
                            }
                            KeyboardAction {
                                id: orderAction
                                objectName: orderBtn.modelData === "absolute" ? "theatreOrderAbsolute" : "theatreOrderSeasons"
                                anchors.fill: parent
                                accessibleName: orderText.text + " order"
                                focusRadius: 14
                                focusColor: page.focusGold
                                onTriggered: page.selectEpisodeOrder(orderBtn.modelData)
                            }
                        }
                    }
                }
            }

            // Seasons, ten or fewer: one row of pills.
            Flickable {
                id: seasonStrip
                visible: page.seasons.length > 1 && page.seasons.length <= 10 && page.effectiveEpisodeOrder !== "absolute"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(seasonPills.implicitWidth, Math.max(160, page.width * 0.42))
                height: 36
                contentWidth: seasonPills.implicitWidth
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: seasonPills
                    spacing: 4
                    Repeater {
                        model: page.seasons
                        delegate: Item {
                            id: seasonBtn
                            required property var modelData
                            readonly property bool on: page.activeSeason === seasonBtn.modelData
                            width: seasonText.implicitWidth + 26
                            height: 36
                            Rectangle { anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: 3; radius: 15
                                        color: seasonBtn.on ? Qt.rgba(1, 1, 1, 0.14) : (seasonAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08) : "transparent") }
                            Text {
                                id: seasonText
                                anchors.centerIn: parent
                                text: seasonBtn.modelData === 0 ? "Specials" : ("Season " + seasonBtn.modelData)
                                color: seasonBtn.on ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: seasonBtn.on ? Font.DemiBold : Font.Normal
                            }
                            KeyboardAction {
                                id: seasonAction
                                objectName: "theatreSeason_" + seasonBtn.modelData
                                anchors.fill: parent
                                accessibleName: seasonText.text
                                focusRadius: 15
                                focusColor: page.focusGold
                                onActiveFocusChanged: if (activeFocus) {
                                    var p = seasonBtn.mapToItem(seasonPills, 0, 0)
                                    if (p.x < seasonStrip.contentX) seasonStrip.contentX = p.x
                                    else if (p.x + seasonBtn.width > seasonStrip.contentX + seasonStrip.width)
                                        seasonStrip.contentX = p.x + seasonBtn.width - seasonStrip.width
                                }
                                onTriggered: page.selectSeason(seasonBtn.modelData)
                            }
                        }
                    }
                }
            }

            // Seasons, more than ten: a dropdown.
            Item {
                visible: page.seasons.length > 10 && page.effectiveEpisodeOrder !== "absolute"
                anchors.verticalCenter: parent.verticalCenter
                width: seasonTrigger.width
                height: 36
                z: 40
                Rectangle {
                    id: seasonTrigger
                    width: seasonTrigT.implicitWidth + 50
                    height: 36
                    radius: 18
                    color: (seasonTriggerKeyboard.interactionActive || page.seasonMenuOpen) ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: page.seasonMenuOpen ? theme.gold : theme.edge
                    Text {
                        id: seasonTrigT
                        x: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.seasonLabel()
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"
                        color: page.seasonMenuOpen ? theme.gold : theme.inkDim
                        font.pixelSize: 12
                    }
                    KeyboardAction {
                        id: seasonTriggerKeyboard
                        objectName: "theatreSeasonMenuButton"
                        anchors.fill: parent
                        accessibleName: "Choose season"
                        focusRadius: parent.radius
                        focusColor: page.focusGold
                        onTriggered: page.openSeasonMenu(seasonTriggerKeyboard)
                    }
                }
                Rectangle {
                    anchors.top: seasonTrigger.bottom
                    anchors.topMargin: 8
                    width: 236
                    height: Math.min(320, seasonMenuList.contentHeight + 12)
                    visible: page.seasonMenuOpen
                    radius: 14
                    color: Qt.rgba(0.045, 0.05, 0.075, 0.98)
                    border.width: 1
                    border.color: theme.edge
                    FocusScope {
                        id: seasonMenuFocus; anchors.fill: parent; z: 4
                        Keys.onPressed: (event) => {
                            var n = page.seasons.length
                            if (event.key === Qt.Key_Escape) { page.closeSeasonMenu(true); event.accepted = true; return }
                            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted = true; return }
                            if (!n) return
                            if (event.key === Qt.Key_Up) page.seasonMenuKeyboardIndex = (page.seasonMenuKeyboardIndex + n - 1) % n
                            else if (event.key === Qt.Key_Down) page.seasonMenuKeyboardIndex = (page.seasonMenuKeyboardIndex + 1) % n
                            else if (event.key === Qt.Key_Home) page.seasonMenuKeyboardIndex = 0
                            else if (event.key === Qt.Key_End) page.seasonMenuKeyboardIndex = n - 1
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                page.activateSeasonMenu(page.seasonMenuKeyboardIndex); event.accepted = true; return
                            }
                            else return
                            seasonMenuList.positionViewAtIndex(page.seasonMenuKeyboardIndex, ListView.Contain)
                            event.accepted = true
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
                                ? Qt.rgba(0.94, 0.77, 0.29, 0.12)
                                : (smMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                            border.width: seasonMenuFocus.activeFocus && page.seasonMenuKeyboardIndex === smRow.index ? 2 : 0
                            border.color: theme.gold
                            Text {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: smRow.modelData === 0 ? "Specials" : "Season " + smRow.modelData
                                color: page.activeSeason === smRow.modelData ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: page.activeSeason === smRow.modelData ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                id: smMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPositionChanged: page.seasonMenuKeyboardIndex = smRow.index
                                onClicked: page.activateSeasonMenu(smRow.index)
                            }
                        }
                    }
                }
            }
        }

        Row {
            id: ledgerRight
            anchors.right: parent.right
            anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // Download the active season (hidden in Absolute order: "episodes" is the whole run).
            Rectangle {
                id: seasonDownloadAction
                visible: typeof Download !== "undefined" && page.episodes.length > 0
                         && page.effectiveEpisodeOrder !== "absolute" && ledgerBarFloat.width > 1180
                height: 36
                width: dlSeasonRow.implicitWidth + 28
                radius: 18
                color: seasonDownloadKeyboard.interactionActive && !page.seasonQueued[page.activeSeason]
                       ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: theme.edge
                Row {
                    id: dlSeasonRow
                    anchors.centerIn: parent
                    spacing: 8
                    PlayerIcon {
                        width: 15; height: 15
                        kind: page.seasonQueued[page.activeSeason] ? "check" : "download"
                        ink: page.seasonQueued[page.activeSeason] ? page.watchedInk : theme.inkDim
                    }
                    Text {
                        text: page.seasonQueued[page.activeSeason] ? "Season queued" : "Download season"
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }
                KeyboardAction {
                    id: seasonDownloadKeyboard
                    objectName: "theatreSeasonDownload"
                    anchors.fill: parent
                    enabled: !page.seasonQueued[page.activeSeason]
                    accessibleName: "Download season"
                    focusRadius: parent.radius
                    focusColor: page.focusGold
                    onTriggered: page.openSeasonPicker()
                }
            }

            // Next: jump straight to the episode you are on.
            Rectangle {
                visible: page.nextUpEpisodeIndex >= 0 && page.episodes.length > 1
                height: 36
                width: nextText.implicitWidth + 28
                radius: 18
                color: nextAction.interactionActive ? Qt.rgba(0.94, 0.77, 0.29, 0.16) : Qt.rgba(0.94, 0.77, 0.29, 0.08)
                border.width: 1
                border.color: Qt.rgba(0.94, 0.77, 0.29, 0.45)
                Text {
                    id: nextText
                    anchors.centerIn: parent
                    text: "Next · E" + page.nextUpDisplayNumber()
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
                KeyboardAction {
                    id: nextAction
                    objectName: "theatreLedgerNextUp"
                    anchors.fill: parent
                    accessibleName: "Go to the next episode to watch"
                    focusRadius: parent.radius
                    focusColor: page.focusGold
                    onTriggered: page.jumpToNextUp()
                }
            }

            // Go to: an always-there number field (also reached by typing digits on the list).
            Rectangle {
                visible: page.episodes.length >= 12
                height: 36
                width: 132
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: jumpInput.activeFocus ? 3 : 1
                border.color: jumpInput.activeFocus ? theme.gold : theme.edge
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: jumpInput.text.length === 0
                    text: "Go to #"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                }
                TextInput {
                    id: jumpInput
                    objectName: "theatreLedgerGoTo"
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.ink
                    selectionColor: theme.gold
                    selectedTextColor: "#111111"
                    font.family: theme.ui; font.pixelSize: 13
                    validator: IntValidator { bottom: 0; top: 99999 }
                    activeFocusOnTab: true
                    onTextChanged: page.episodeJumpDraft = text
                    Keys.onReturnPressed: page.goToEpisodeDraft()
                    Keys.onEnterPressed: page.goToEpisodeDraft()
                    Keys.onEscapePressed: {
                        text = ""
                        page.episodeJumpOpen = false
                        episodeVirtualSpace.forceActiveFocus(Qt.OtherFocusReason)
                    }
                    Keys.onDownPressed: episodeVirtualSpace.forceActiveFocus(Qt.TabFocusReason)
                }
            }
        }
    }

    // ---- the range strip: jump through a long run (One Piece) by hundreds ----
    Item {
        id: rangeStrip
        objectName: "theatreLedgerRangeStrip"
        z: 29
        visible: page.longRun && ledgerBarFloat.visible && ledgerBarFloat.pinned
        x: page.width - theme.margin + 8
        y: titleBar.height + page.ledgerBarHeight + 10
        width: Math.max(34, theme.margin - 16)
        height: page.height - y - 110
        readonly property int rangeCount: Math.ceil(page.episodes.length / page.episodeRangeSize)
        readonly property real slot: rangeCount > 0 ? height / rangeCount : height
        readonly property int currentRange: Math.floor(page.firstVisibleEpisodeIndex / page.episodeRangeSize)
        readonly property int labelEvery: Math.max(1, Math.ceil(20 / Math.max(1, rangeStrip.slot)))

        function rangeAt(yy) {
            return Math.max(0, Math.min(rangeCount - 1, Math.floor(yy / slot)))
        }
        function jumpToRange(r) {
            var idx = Math.min(page.episodes.length - 1, r * page.episodeRangeSize)
            page.episodeKeyboardIndex = idx
            page.scrollToEpisodeIndex(idx)
        }

        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 2; height: parent.height; radius: 1; color: Qt.rgba(1, 1, 1, 0.08) }
        Repeater {
            model: rangeStrip.rangeCount
            delegate: Item {
                id: tick
                required property int index
                readonly property bool current: tick.index === rangeStrip.currentRange
                y: tick.index * rangeStrip.slot
                width: rangeStrip.width
                height: rangeStrip.slot
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: tick.current ? 6 : 4
                    height: Math.max(4, Math.min(tick.current ? 18 : 6, parent.height - 2))
                    radius: 3
                    color: tick.current ? theme.gold : Qt.rgba(1, 1, 1, stripMa.containsMouse ? 0.45 : 0.25)
                }
                Text {
                    visible: tick.current || (tick.index % rangeStrip.labelEvery === 0 && stripMa.containsMouse)
                    anchors.right: parent.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: (tick.index * page.episodeRangeSize + 1).toLocaleString(Qt.locale(), "f", 0)
                    color: tick.current ? theme.gold : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                    style: Text.Outline; styleColor: "#06070b"
                }
            }
        }
        MouseArea {
            id: stripMa
            anchors.fill: parent
            anchors.leftMargin: -8
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            onPressed: (mouse) => rangeStrip.jumpToRange(rangeStrip.rangeAt(mouse.y))
            onPositionChanged: (mouse) => { if (pressed) rangeStrip.jumpToRange(rangeStrip.rangeAt(mouse.y)) }
        }
    }

    // ---- the title bar: Back pill, world pills, search, system menu ----
    TitlePageBar {
        id: titleBar
        z: 40
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        world: "Theatre"
        backLabel: page.backLabel
        solid: flick.contentY > 40 || page.loading
        onBackRequested: page.backRequested()
        onWorldRequested: (w) => page.worldRequested(w)
        onSearchRequested: page.searchRequested()
        onMinimizeRequested: page.minimizeRequested()
        onFullscreenRequested: page.fullscreenRequested()
        onQuitRequested: page.closeRequested()
    }

    Rectangle {
        id: episodeContextMenu
        visible: page.episodeContextOpen
        z: 80; anchors.centerIn: parent
        width: 210; height: episodeContextCol.implicitHeight + 12; radius: 12
        color: Qt.rgba(0.045, 0.05, 0.075, 0.98); border.width: 1; border.color: theme.edge
        FocusScope {
            id: episodeContextFocus; anchors.fill: parent
            Keys.onPressed: (event) => {
                var n = page.episodeContextOptions(page.episodeKeyboardIndex).length
                if (event.key === Qt.Key_Escape) { page.closeEpisodeContext(true); event.accepted = true; return }
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted = true; return }
                if (event.key === Qt.Key_Up) page.episodeContextChoice = (page.episodeContextChoice + n - 1) % n
                else if (event.key === Qt.Key_Down) page.episodeContextChoice = (page.episodeContextChoice + 1) % n
                else if (event.key === Qt.Key_Home) page.episodeContextChoice = 0
                else if (event.key === Qt.Key_End) page.episodeContextChoice = n - 1
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    page.activateEpisodeContext(page.episodeContextChoice); event.accepted = true; return
                }
                else return
                event.accepted = true
            }
        }
        Column {
            id: episodeContextCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 6
            Repeater {
                model: page.episodeContextOptions(page.episodeKeyboardIndex)
                delegate: Rectangle {
                    required property string modelData; required property int index
                    width: episodeContextCol.width; height: 36; radius: 8
                    color: episodeContextFocus.activeFocus && page.episodeContextChoice === index ? Qt.rgba(1, 1, 1, 0.11) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.activateEpisodeContext(index) }
                }
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
            font.family: theme.display; font.pixelSize: 34
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading…"
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
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
                page.queueSeasonDownload(row)
                page.pendingSeasonPick = false
            } else if (page.pendingDownloadEpisode) {
                page.queueEpisodeDownload(page.pendingDownloadEpisode, row)
                page.pendingDownloadEpisode = null
            } else if (page.mediaType === "series" && page.sheetEpisode) {
                page.queueEpisodeDownload(page.sheetEpisode, row)
            } else if (page.mediaType === "movie") {
                page.queueMovieDownload(row)
            }
        }
        onSeasonNoPacks: {
            if (page.pendingSeasonPick)
                page.queueSeasonDownload(null)
            page.pendingSeasonPick = false
        }
        onOpenChanged: if (!sources.open) {
            page.pendingSeasonPick = false
            page.pendingDownloadEpisode = null
        }
    }
}
