// SourcesSheet - source picker in the Colosseum house language (approved mock, 2026-06-27).
// The full source-picker content is the JOB, not clutter: provider, the quality TOP BAR + quick-filter
// pills, release, seeders/size/source, audio + language, format chips, stream type, and an always-on
// action button. House craft on top: a GLASS table floating over the wallpaper, warm Colosseum ink
// (never cold blue), real glyphs, and gold kept to the active pill / quality line / play button.
// Public API (show/hide/properties) is unchanged.
//
// Un-soldered 2026-07-05 (extensions spec Phase 2): the sheet asks EVERY enabled
// stream extension in installed order via AddonClient — rows land as each one
// answers, tagged by extension; dedup keeps the higher-priority answer. With only
// the seeded four installed this behaves exactly as the old Torrentio-only sheet.
import QtQuick
import QtQuick.Controls
import QtCore
import "AddonClient.js" as AddonClient
import "Magnet.js" as Magnet
import "TrackLanguage.js" as TrackLanguage
import "SmartPlayPreferences.js" as SmartPlayPreferences
import "." as App

Item {
    id: sheet
    anchors.fill: parent
    // Slice-2 automation surface (no visual effect): names the sheet root so the Lanista bridge can
    // read its state (`qml-get sourcesSheet.loading/rowCount/httpRowCount`) and prove the source-sheet
    // slices (5–7) in the running app instead of by eye. dump-ui sees named items only.
    objectName: "sourcesSheet"
    // The host owns the Android-width decision. Standalone/desktop sheets stay unchanged.
    readonly property bool phoneLayout: sheet.Window.window
                                     && sheet.Window.window.androidHost && sheet.Window.window.desktopPresentation !== true
                                     && sheet.Window.window.width < 840
    readonly property int dockBottomInset: sheet.Window.window
                                        ? (sheet.Window.window.theatreSourcesDockInset || 0) : 0

    readonly property real outerPageMargin: theme.margin

    readonly property real headerOuterMargin: theme.margin

    // Height budget (PF-02). The desktop composition pins a 300 banner, 64 filter bar,
    // quality pills and a 52 table head, leaving H-544 for results. Android desktop
    // presentation at launch zoom (logical height ~640/zoom, e.g. 556 at 1.15 or 533 at 1.20)
    // would leave little or no result viewport, so there the banner and fixed chrome yield
    // to keep at least one complete 150-unit source row plus a peek of the next. Windows and
    // desktop geometry are unchanged; the phone branch keeps its own layout.
    readonly property bool androidDesktopPresentation: !!(sheet.Window.window
        && sheet.Window.window.androidHost && sheet.Window.window.desktopPresentation === true)
    readonly property real sourceRowHeight: 150
    readonly property real minResultBudget: sheet.sourceRowHeight + 40
    readonly property real desktopChromeReserve: (topBar.visible ? 20 + 64 : 0)
        + (pills.visible ? 14 + pills.height : 0) + 16 + Math.max(26, sheet.dockClearance) + (tableHead.visible ? 52 : 0) + 12
    readonly property bool compactHeight: !sheet.phoneLayout && sheet.androidDesktopPresentation
        && sheet.height - 300 - sheet.desktopChromeReserve < sheet.minResultBudget
    readonly property real compactChromeReserve: (topBar.visible ? 10 + 52 : 0)
        + (pills.visible ? 8 + pills.height : 0) + 10 + Math.max(12, sheet.dockClearance) + (tableHead.visible ? 44 : 0) + 12
    readonly property real compactBannerMin: 76
    readonly property real dockClearance: sheet.dockBottomInset > 0 ? sheet.dockBottomInset + 8 : 0
    // Keep space for two direct rows on short desktop windows while Nuvio is in use.
    readonly property real bannerHeight: sheet.phoneLayout ? 200
        : !sheet.compactHeight ? (sheet.nuvioAsked ? Math.min(300, Math.max(164, sheet.height - 556)) : 300)
        : Math.round(Math.max(sheet.compactBannerMin,
              Math.min(300, sheet.height - sheet.compactChromeReserve - sheet.minResultBudget)))
    readonly property bool compactControls: sheet.compactHeight
        || (!sheet.phoneLayout && sheet.nuvioAsked
            && sheet.height - sheet.bannerHeight - sheet.desktopChromeReserve < 2 * sheet.sourceRowHeight + 2)
    readonly property real tableBottomMargin: Math.max(
        sheet.phoneLayout ? 16 : (sheet.compactControls ? 12 : 26), sheet.dockClearance)

    property Item backdrop
    property string label: ""
    property string title: ""
    property string metaLine: ""
    property string backdropUrl: ""
    property string subType: ""   // "movie" | "series" — carried to the player for online subtitle fetch
    property string subId: ""     // "tt..." (movie) or "tt...:s:e" (episode)
    property var playbackContext: ({})
    property var mediaMetadata: ({})
    property bool open: false
    property bool loading: false
    property bool timedOut: false
    property var rows: []
    Settings {
        id: smartPlaySettings
        objectName: "smartPlaySettings"
        location: Qt.resolvedUrl("../player.ini")
        category: "transport"
    }
    property string smartPlayQuality: "1080p"
    function refreshSmartPlayQuality() {
        sheet.smartPlayQuality = SmartPlayPreferences.read(smartPlaySettings)
    }
    function setSmartPickEnabled(enabled) {
        sheet.smartPlayQuality = SmartPlayPreferences.setEnabled(smartPlaySettings, enabled)
    }
    Component.onCompleted: sheet.refreshSmartPlayQuality()
    Connections {
        id: smartPlayWindow
        target: sheet.Window.window
        ignoreUnknownSignals: true
        function onPlayerOpenChanged() { if (!sheet.open) sheet.refreshSmartPlayQuality() }
        function onActiveChanged() {
            if (sheet.Window.window && sheet.Window.window.active && !sheet.open)
                sheet.refreshSmartPlayQuality()
        }
    }
    readonly property bool smartPlayEnabled: sheet.mode === "play" && sheet.smartPlayQuality !== "off"
    property bool smartPlayCancelled: false
    property bool smartPlaySettled: false
    property var smartPlayRow: null
    readonly property bool smartPicking: sheet.open && sheet.smartPlayEnabled
        && ((!sheet.smartPlaySettled && !sheet.smartPlayCancelled) || sheet.smartPlayRow !== null)
    function chooseSourceManually() {
        if (!sheet.smartPicking || sheet.smartPlaySettled) return
        sheet.smartPlayCancelled = true
        sheet.smartPlayRow = null
        sheet.closeSourceContext(false)
        sheet.closeExtMenu(false)
        Qt.callLater(function() {
            if (!sheet.open || sheet.smartPicking) return
            if (list.visible) list.forceActiveFocus(Qt.OtherFocusReason)
            else sourcesBack.focusControl(Qt.OtherFocusReason)
        })
    }
    readonly property int smartPlayVisibleIndex: sheet.visibleRows.indexOf(sheet.smartPlayRow)
    readonly property string smartPlayStatus: !sheet.smartPlayEnabled || sheet.smartPlayCancelled ? ""
        : !sheet.smartPlaySettled ? qsTr("Finding the best source…")
        : !sheet.smartPlayRow ? qsTr("No source passed the quality check. Choose one below.") : ""
    function finishSmartPlay(atTimeout) {
        if (!sheet.open || !sheet.smartPlayEnabled || sheet.smartPlayCancelled || sheet.smartPlaySettled
                || (!atTimeout && (sheet.loading || sheet.nuvioPending))) return
        timeout.stop()
        var allRows = sheet.baseRows()
        var pick = SmartPlay.pick(allRows, sheet.smartPlayQuality,
            Object.assign({}, sheet.playbackContext, { type: sheet.subType, id: sheet.subId,
                preferredLanguages: TrackLanguage.parseLanguageList(
                    smartPlaySettings.value("preferredAudioLanguages", "eng,jpn"), ["eng", "jpn"]) }))
        sheet.smartPlayRow = pick.index >= 0 ? allRows[pick.index] : null
        sheet.smartPlaySettled = true
        sheet.warmTopRow()
        if (pick.index >= 0) sheet.activateSource(pick.index, allRows)
    }
    onLoadingChanged: if (!sheet.loading) sheet.finishSmartPlay(false)
    onNuvioPendingChanged: if (!sheet.nuvioPending) sheet.finishSmartPlay(false)
    property var extensionsService: typeof Extensions !== "undefined" ? Extensions : null
    property var loadStremioStreams: AddonClient.loadStreams
    property var nuvioSourceService: typeof NuvioSources !== "undefined" ? NuvioSources : null
    property var homeServerSourceService: typeof HomeServers !== "undefined" ? HomeServers : null
    property var homeServerRows: []
    property var homeServerProfileGeneration: -1
    property int nuvioRequestId: 0
    readonly property bool nuvioPending: nuvioRequestId !== 0
    property bool nuvioAsked: false
    property var nuvioRows: []
    property var askedNames: []     // the extensions this ask went out to, ask-order
    // Automation surface (additive, no behavior change): the plain per-pipe
    // Lanista CLI cannot express a JSON array through qml-get, so a length
    // check on askedNames needs a scalar mirror to be equality-probeable.
    // askedCount==0 after loading settles means the ask never actually went
    // out (e.g. an enablement step upstream silently failed) - a drive can
    // assert this BEFORE waiting for rows, instead of only ever seeing an
    // indistinguishable rows-timeout.
    readonly property int askedCount: askedNames.length
    property int gen: 0
    property string qualityFilter: "all"
    // Provider filtering keeps the order of matching rows from the combined list.
    property string extFilter: "all"
    property bool extMenuOpen: false
    property Item focusReturnItem: null
    property Item extMenuReturnItem: null
    property string extMenuKey: "all"
    readonly property var providerOptions: extOptions()
    readonly property int extMenuIndex: Math.max(0, providerOptions.findIndex(option => option.key === extMenuKey))
    property bool sourceContextOpen: false
    property var sourceContextRow: null
    property int sourceContextChoice: 0
    readonly property var qualityOptions: ["all", "4K", "1080p", "720p", "SD"].filter(function(q) { return q === "all" || sheet.countFor(q) > 0 })
    property var visibleRows: filteredRows()
    // Slice-2 automation surface (no visual effect): row counts read by the bridge via
    // `qml-get sourcesSheet.*`. `loading` (below) is the sheet's own state; these count what shows.
    property int rowCount: sheet.visibleRows.length
    property int httpRowCount: {
        var n = 0;
        for (var i = 0; i < sheet.visibleRows.length; ++i)
            if (sheet.visibleRows[i] && sheet.visibleRows[i].streamKind === "Direct") ++n;
        return n;
    }
    // "play" (default — every pre-existing caller), "download" (choosing a row
    // queues that exact torrent instead of playing, spec 2026-07-11), or "season"
    // (2026-07-19): the season checkout's picker — only FULL-SEASON torrents are
    // listed, the pick pins the whole season to that one torrent. When the ask
    // finishes with zero packs the sheet bows out via seasonNoPacks and the page
    // falls back to the per-episode auto path.
    property string mode: "play"
    property int pickSeason: 0   // the season the "season" ask is for (context.season)
    // Play-mode per-row download (2026-07-19): the ↓ beside the copy queues THIS
    // title pinned to THAT torrent. One download per title: the store is idempotent
    // by id, so after one pick (this visit or an earlier one) a second row's pick
    // would be silently ignored — every row's button shows the tick instead of
    // offering a choice that can't be honored.
    property bool titleQueued: false

    function activateSource(index, sourceRows) {
        var available = sourceRows || sheet.visibleRows
        if (index < 0 || index >= available.length) return
        sheet.smartPlayCancelled = true
        var row=available[index]
        if (sheet.mode !== "play") { if (row.homeServer) return; sheet.downloadRequested(row); sheet.hide() }
        else {
            var candidates = sheet.baseRows()
            if (row.nuvio) {
                // Players look up a direct stream by its URL. Keep the chosen provider's headers.
                var rowIndex = candidates.indexOf(row)
                var firstMatch = candidates.findIndex(function(candidate) {
                    return candidate.infoHash.toLowerCase() === row.infoHash.toLowerCase()
                })
                if (firstMatch !== rowIndex) {
                    candidates.splice(rowIndex, 1)
                    candidates.splice(firstMatch, 0, row)
                }
            }
            sheet.playRequested(row.infoHash, row.fileIdx, sheet.title, sheet.backdropUrl, sheet.subType, sheet.subId, candidates, sheet.playbackContext)
        }
    }
    function copySource(index) {
        if (index < 0 || index >= sheet.visibleRows.length) return
        if (sheet.visibleRows[index].homeServer || String(sheet.visibleRows[index].infoHash || "").indexOf("home-server:") === 0) return
        var link=Magnet.linkFor(sheet.visibleRows[index]); if(!link.length) return
        Clipboard.copy(link)
        if (list.itemAtIndex) { var item=list.itemAtIndex(index); if(item && item.markCopied) item.markCopied() }
    }
    function downloadSource(index) {
        if (sheet.mode !== "play" || sheet.titleQueued || index < 0 || index >= sheet.visibleRows.length) return
        if (sheet.visibleRows[index].homeServer || String(sheet.visibleRows[index].infoHash || "").indexOf("home-server:") === 0) return
        sheet.downloadRequested(sheet.visibleRows[index]); sheet.titleQueued=true
    }
    function activateSourceContext(choice) {
        var index=sheet.visibleRows.indexOf(sheet.sourceContextRow)
        if (choice===0) sheet.activateSource(index)
        else if (choice===1) sheet.copySource(index)
        else if (choice===2) sheet.downloadSource(index)
        sheet.closeSourceContext(true)
    }

    function refreshTitleQueued() {
        if (typeof Download === "undefined" || !sheet.subId.length) { sheet.titleQueued = false; return }
        if (Download.hasVideo(sheet.subId)) { sheet.titleQueued = true; return }
        var js = Download.jobs()
        for (var i = 0; i < js.length; i++)
            if (js[i].id === sheet.subId) { sheet.titleQueued = true; return }
        sheet.titleQueued = false
    }

    // a source row was chosen → play it (handled up at Main, which opens the player)
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl, string subType, string subId, var streamCandidates, var playbackContext)
    // download/season mode's row action: the full chosen row (infoHash/fileIdx/url/…)
    signal downloadRequested(var row)
    // season mode only: the ask came back with no full-season torrent
    signal seasonNoPacks()
    readonly property bool androidHost: !!(sheet.Window.window && sheet.Window.window.androidHost)
    signal openExtensionsRequested(bool browseMoreSources)

    visible: sheet.open || sheet.opacity > 0.01
    opacity: sheet.open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Theme { id: theme }

    function openExtMenu(invoker) {
        if (sheet.extMenuOpen) { sheet.extMenuOpen=false; return }
        sheet.extMenuReturnItem=invoker || null
        sheet.extMenuKey = sheet.extFilter
        sheet.extMenuOpen = true
        Qt.callLater(function(){ extMenuFocus.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeExtMenu(restore) {
        sheet.extMenuOpen=false
        var target=sheet.extMenuReturnItem; sheet.extMenuReturnItem=null
        if (restore!==false && target) Qt.callLater(function(){ if(target.visible&&target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function activateExtOption(key) {
        sheet.extFilter = key
        if (sheet.qualityFilter !== "all" && !sheet.countFor(sheet.qualityFilter)) {
            sheet.qualityFilter = "all"
            pills.currentIndex = 0
        }
        sheet.closeExtMenu(true)
    }
    function sourceContextOptions() {
        if (sheet.sourceContextRow && sheet.sourceContextRow.homeServer) return [qsTr("Play")]
        var out=[sheet.mode === "play" ? "Play" : "Choose source", "Copy link"]
        if (sheet.mode === "play" && typeof Download !== "undefined") out.push("Download")
        return out
    }
    function openSourceContext(index) {
        sheet.sourceContextRow=sheet.visibleRows[index]; sheet.sourceContextChoice=0; sheet.sourceContextOpen=true
        Qt.callLater(function(){ sourceContextFocus.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeSourceContext(restore) {
        sheet.sourceContextOpen=false
        sheet.sourceContextRow=null
        if (restore!==false) Qt.callLater(function(){ if(list.visible) list.forceActiveFocus(Qt.PopupFocusReason) })
    }

    function show(type, id, lbl, context, pickMode) {
        sheet.homeServerProfileGeneration = -1;
        sheet.homeServerRows = [];
        sheet.smartPlayCancelled = true;
        sheet.smartPlayRow = null;
        sheet.smartPlaySettled = false;
        sheet.refreshSmartPlayQuality();
        sheet.cancelNuvio();
        sheet.closeSourceContext(false);
        var w=sheet.Window.window
        sheet.focusReturnItem=w ? w.activeFocusItem : null
        sheet.mode = (pickMode === "download" || pickMode === "season") ? pickMode : "play";
        sheet.pickSeason = (context && context.season !== undefined) ? Number(context.season) : 0;
        sheet.subType = type ? type : "";
        sheet.subId = id ? id : "";
        sheet.label = lbl ? lbl : "";
        sheet.title = (context && context.title) ? context.title : sheet.label;
        sheet.metaLine = (context && context.metaLine) ? context.metaLine : "";
        sheet.backdropUrl = (context && context.backdrop) ? context.backdrop : "";
        sheet.playbackContext = Object.assign({}, context || ({}), {
            metadata: Object.assign({}, sheet.mediaMetadata, (context || ({})).metadata || ({}))
        });
        sheet.rows = [];
        sheet.nuvioRows = [];
        sheet.nuvioAsked = false;
        sheet.qualityFilter = "all";
        sheet.extFilter = "all";
        sheet.extMenuOpen = false;
        sheet.timedOut = false;
        sheet.refreshTitleQueued();
        sheet.loading = true;
        sheet.smartPlayCancelled = false;
        sheet.open = true;
        sheet.gen += 1;
        sheet.homeServerProfileGeneration = sheet.homeServerSourceService ? sheet.homeServerSourceService.profileGeneration : -1;
        sheet.refreshHomeServerRows();
        Qt.callLater(function(){ sourcesBack.forceActiveFocus(Qt.PopupFocusReason) });
        var myGen = sheet.gen;
        timeout.restart();

        var installedList = sheet.extensionsService ? sheet.extensionsService.installed() : [];

        // every enabled stream extension that accepts this title, installed order
        var exts = AddonClient.streamExtensions(installedList, type, id);
        sheet.askedNames = exts.map(function(e) {
            return (e.manifest && e.manifest.name) || e.id;
        });
        sheet.startNuvio(type, id, sheet.playbackContext);
        if (!exts.length) { sheet.loading = false; seasonSettle(); return; }

        sheet.loadStremioStreams(exts, type, id,
            function(partialRows) {                 // one extension answered, more out
                if (myGen !== sheet.gen) return;
                sheet.acceptStremioRows(partialRows);
            },
            function(allRows) {                     // every extension answered or timed out
                if (myGen !== sheet.gen) return;
                sheet.acceptStremioRows(allRows);
                sheet.loading = false;
                if (!sheet.smartPlayEnabled || !sheet.nuvioPending) timeout.stop();
                sheet.warmTopRow();
                seasonSettle();
            },
            // Advisory decoder evidence (WT-03); a library cannot read context properties.
            typeof PlatformRuntime !== "undefined" ? PlatformRuntime : null);
    }

    function acceptStremioRows(rows) {
        var selected = sheet.nuvioRows.length ? sheet.visibleRows[list.currentIndex] : null;
        sheet.rows = rows;
        if (selected && selected.nuvio) list.currentIndex = sheet.visibleRows.indexOf(selected);
    }

    function startNuvio(type, id, context) {
        if (sheet.mode !== "play" || !sheet.nuvioSourceService) return;
        var season = Number(context.season) || 0;
        var episode = Number(context.episode) || 0;
        var imdbEpisode = /^(tt\d+):(\d+):(\d+)$/.exec(id || "");
        if (imdbEpisode) {
            if (context.season === undefined) season = Number(imdbEpisode[2]);
            if (context.episode === undefined) episode = Number(imdbEpisode[3]);
        }
        if (type !== "movie" && ((type !== "series" && type !== "tv") || season < 0 || episode < 1)) return;
        var tmdbId = Number(context.tmdbId);
        var mediaId = tmdbId > 0 ? String(tmdbId) : (context.imdbId || id || "");
        sheet.nuvioRequestId = sheet.nuvioSourceService.resolve(mediaId, type, season, episode);
        sheet.nuvioAsked = sheet.nuvioPending;
    }

    function cancelNuvio() {
        var requestId = sheet.nuvioRequestId;
        sheet.nuvioRequestId = 0;
        if (requestId && sheet.nuvioSourceService) sheet.nuvioSourceService.cancel(requestId);
    }

    Connections {
        target: sheet.nuvioSourceService
        function onStreamsReady(requestId, streams) {
            if (!sheet.open || !sheet.nuvioPending || requestId !== sheet.nuvioRequestId) return;
            var batch = [];
            for (var i = 0; i < streams.length; ++i) {
                var stream = streams[i];
                var row = AddonClient.parseStream({
                    url: stream.url, name: stream.name, title: stream.title,
                    behaviorHints: { proxyHeaders: { request: stream.headers || ({}) } }
                }, stream.pluginName, 0, "nuvio:" + stream.repoId + ":" + stream.pluginId);
                if (!row) continue;
                row.nuvio = true;
                batch.push(row);
            }
            sheet.nuvioRows = sheet.nuvioRows.concat(batch);
        }
        function onFinished(requestId) {
            if (requestId === sheet.nuvioRequestId) sheet.nuvioRequestId = 0;
        }
    }

    function refreshHomeServerRows() {
        if (!sheet.open || sheet.mode !== "play" || !sheet.homeServerSourceService
                || sheet.homeServerProfileGeneration !== sheet.homeServerSourceService.profileGeneration) {
            sheet.homeServerRows = [];
            return;
        }
        var identity = Object.assign({}, sheet.playbackContext || ({}));
        identity.type = sheet.subType;
        identity.id = sheet.subId;
        sheet.homeServerRows = sheet.homeServerSourceService.sources(identity) || [];
        if (sheet.extFilter.indexOf("home-server:") === 0
                && !sheet.homeServerRows.some(row => sheet.providerKey(row) === sheet.extFilter))
            sheet.extFilter = "all";
    }
    onHomeServerSourceServiceChanged: { sheet.homeServerRows = []; sheet.homeServerProfileGeneration = -1 }
    onSubTypeChanged: sheet.refreshHomeServerRows()
    onSubIdChanged: sheet.refreshHomeServerRows()
    onPlaybackContextChanged: sheet.refreshHomeServerRows()
    onModeChanged: sheet.refreshHomeServerRows()
    Connections {
        target: sheet.homeServerSourceService
        function onChanged() { sheet.refreshHomeServerRows() }
        function onProfileChanged() {
            sheet.homeServerProfileGeneration = -1;
            sheet.homeServerRows = [];
            if (sheet.extFilter.indexOf("home-server:") === 0) sheet.extFilter = "all";
            sheet.closeSourceContext(false);
        }
    }
    onOpenChanged: if (!sheet.open) {
        sheet.cancelNuvio();
        sheet.homeServerRows = [];
        sheet.homeServerProfileGeneration = -1;
    }
    Component.onDestruction: sheet.cancelNuvio()

    // Torrent warm-up (2026-07-31). The 2026-07-30 server warm-up starts the ENGINE
    // early but not the TORRENT: /create — join the DHT for THIS infohash, announce to
    // trackers, handshake, get unchoked, pull the head pieces — still ran only when Play
    // was pressed, and that is the part measured in minutes. So warm the top-ranked row
    // the moment the list lands: he spends seconds reading it, and that is exactly the
    // window the connect work needs. Same adopt-first path as play; a fetchReady with no
    // pending download job is a no-op in Main.qml, so there is no side effect.
    // Bounded to ONE row on purpose — warming all of them would open a swarm per row.
    property string warmedHash: ""
    function warmTopRow() {
        if (typeof Stream === "undefined" || sheet.mode === "download") return;
        var rows = sheet.smartPlayRow ? [sheet.smartPlayRow] : sheet.filteredRows();
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i];
            // Direct rows keep a synthetic url:<url> in infoHash only to ride the existing
            // play signal chain. streamKind — not hash emptiness — decides whether warming is legal.
            if (!row || row.streamKind !== "Torrent") continue;
            var h = String(row.infoHash || "");
            if (!h.length) continue;
            if (h === sheet.warmedHash) return;         // already warmed this one
            sheet.warmedHash = h;
            // Android's best-effort warm-up yields to an explicit download owner.
            if (Qt.platform.os === "android") Stream.warmSource(h, row.fileIdx || 0);
            else Stream.prefetch(h, row.fileIdx || 0);
            return;
        }
    }
    onQualityFilterChanged: sheet.warmTopRow()          // he narrowed the list → warm the new top
    onExtFilterChanged: sheet.warmTopRow()              // same on an extension pick (url rows no-op)

    // season mode, ask over, zero full-season torrents → hand the page the
    // auto-pick fallback and get out of the way
    function seasonSettle() {
        if (sheet.mode !== "season" || sheet.loading || !sheet.open) return;
        if (baseRows().length === 0) {
            sheet.seasonNoPacks();
            sheet.hide();
        }
    }

    // Shell Back delegation (PF-03): innermost transient first, then the sheet itself.
    function requestEscape() {
        if (!sheet.open) return false
        if (sheet.sourceContextOpen) { sheet.closeSourceContext(true); return true }
        if (sheet.extMenuOpen) { sheet.closeExtMenu(true); return true }
        sheet.hide()
        return true
    }

    function hide() {
        sheet.gen += 1;
        sheet.closeSourceContext(false);
        sheet.extMenuOpen = false;
        sheet.open = false;
        sheet.smartPlayCancelled = true;
        sheet.smartPlayRow = null;
        timeout.stop();
        var target=sheet.focusReturnItem; sheet.focusReturnItem=null
        if(target) Qt.callLater(function(){ if(target.visible&&target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }

    // the mode's row universe: season mode sees only full-season torrents,
    // everything else sees the raw answer. Quality pills filter on top of this.
    function baseRows() {
        if (sheet.mode !== "season") {
            var providers = sheet.nuvioRows.length ? sheet.rows.concat(sheet.nuvioRows) : sheet.rows;
            return sheet.mode === "play" && sheet.homeServerRows.length ? sheet.homeServerRows.concat(providers) : providers;
        }
        var out = [];
        for (var i = 0; i < sheet.rows.length; ++i)
            if (AddonClient.isSeasonPack(sheet.rows[i], sheet.pickSeason)) out.push(sheet.rows[i]);
        return out;
    }

    // Quality pills and counts use the rows belonging to the selected provider.
    function extRows() {
        var base = baseRows();
        if (sheet.extFilter === "all") return base;
        var out = [];
        for (var i = 0; i < base.length; ++i)
            if (sheet.providerKey(base[i]) === sheet.extFilter) out.push(base[i]);
        return out;
    }

    function providerKey(row) {
        if (row.homeServer) return "home-server:" + row.connectionId;
        return row.nuvio ? "nuvio" : "stremio:" + (row.addonName || "?");
    }

    // Native plugins share a menu entry. Stremio add-ons follow the existing row order.
    function extOptions() {
        var base = baseRows();
        var all = { key: "all", label: qsTr("All"), count: base.length };
        var nuvio = { key: "nuvio", label: qsTr("Nuvio"), count: 0 };
        var byKey = ({ nuvio: nuvio });
        var stremio = [];
        var homeServers = [];
        for (var i = 0; i < base.length; ++i) {
            var row = base[i];
            var key = providerKey(row);
            if (!byKey[key]) {
                var option = { key: key, label: row.addonName || "?", count: 0 };
                byKey[key] = option;
                if (row.homeServer) homeServers.push(option);
                else stremio.push(option);
            }
            ++byKey[key].count;
        }
        if (nuvio.count && byKey["stremio:Nuvio"])
            byKey["stremio:Nuvio"].label = qsTr("Nuvio (Stremio)");
        return [all].concat(homeServers, nuvio.count ? [nuvio] : [], stremio);
    }

    function filteredRows() {
        var base = extRows();
        if (sheet.qualityFilter === "all") return base;
        var out = [];
        for (var i = 0; i < base.length; ++i)
            if (base[i].quality === sheet.qualityFilter) out.push(base[i]);
        return out;
    }

    function countFor(q) {
        var base = extRows();
        if (q === "all") return base.length;
        var n = 0;
        for (var i = 0; i < base.length; ++i)
            if (base[i].quality === q) ++n;
        return n;
    }

    function chipText(q) { return q === "all" ? "All" : q; }

    function streamTypeText(row) {
        return (row.streamKind || "Torrent") + " · " + (row.streamLabel || "P2P stream");
    }

    component NuvioHeading: Flow {
        id: heading
        required property string providerName
        required property string quality
        required property Theme palette
        property bool allowWrap: false
        spacing: 8

        Text {
            width: Math.min(implicitWidth, heading.allowWrap ? heading.width
                            : Math.max(0, heading.width - brand.width - direct.width
                                       - qualityLabel.width - 3 * heading.spacing))
            height: Math.max(implicitHeight, brand.implicitHeight)
            text: heading.providerName
            textFormat: Text.PlainText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: heading.palette.ink
            font.family: heading.palette.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        NuvioSourceChip { id: brand; objectName: "nuvioSourceBrandChip"; branded: true }
        NuvioSourceChip { id: direct; objectName: "nuvioSourceDirectChip" }
        Text {
            id: qualityLabel
            objectName: "nuvioSourceQuality"
            width: Math.min(implicitWidth, heading.allowWrap ? heading.width : heading.width * 0.4)
            height: Math.max(implicitHeight, brand.implicitHeight)
            text: heading.quality
            textFormat: Text.PlainText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: heading.palette.gold
            font.family: heading.palette.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
        }
    }

    // the one inline meta line: seeders, size, source group — PLAIN metadata, no emoji-as-icon
    // (semantic audit 2026-07-19). Words label the values instead of pictographs.
    function metaText(m) {
        var p = [];
        if (m.seeders >= 0) p.push(m.seeders + " seeders");
        if (m.size) p.push(m.size);
        if (m.sourceName && m.sourceName !== "P2P") p.push(m.sourceName);
        return p.join("   ·   ");
    }

    Timer {
        id: timeout
        objectName: "sourcesTimeout"
        interval: 22000
        repeat: false
        // partial answers in hand at the bell = a result, not an error
        onTriggered: {
            if (sheet.loading) { sheet.loading = false; sheet.timedOut = sheet.rows.length === 0; sheet.seasonSettle() }
            sheet.finishSmartPlay(true)
        }
    }

    // ===================== base: float over the wallpaper, not a flat void =====================
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: sheet.backdrop
        live: true; hideSource: false
        visible: sheet.backdrop !== null
        opacity: 0.5
    }
    MouseArea { anchors.fill: parent }                                    // absorb clicks from below

    // ---- banner hero: the title's key-art across the top, washing down ----
    Item {
        id: bannerStrip
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        objectName: "sourcesSheetBanner"
        height: sheet.bannerHeight
        readonly property bool compact: sheet.phoneLayout || height < 200
        Image {
            anchors.fill: parent
            source: sheet.backdropUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            visible: sheet.backdropUrl.length > 0
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.25) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.5) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.92) }
            }
        }
    }
    Rectangle {                                                          // scrim over the rest
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: bannerStrip.bottom; anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.9)
    }

    // ---- back ----
    BackAction {
        id: sourcesBack
        objectName: "sourcesSheetBack"
        x: sheet.phoneLayout ? 16 : sheet.headerOuterMargin
        y: sheet.compactHeight ? 12 : (bannerStrip.compact ? 20 : 30); z: 20
        onTriggered: sheet.hide()
    }

    SmartPickingScreen {
        objectName: "smartPickingScreen"
        anchors.fill: parent
        z: 10
        visible: sheet.smartPicking
        enabled: visible
        allowManual: !sheet.smartPlaySettled
        onManualRequested: sheet.chooseSourceManually()
    }

    // ---- title block, pinned to the bottom of the banner ----
    Column {
        id: titleBlock
        visible: !sheet.smartPicking
        objectName: "sourcesSheetTitleBlock"
        anchors.left: parent.left; anchors.right: parent.right
        // Short Android banners share the first line with Back instead of overlapping it.
        anchors.leftMargin: sheet.phoneLayout ? 16
            : sheet.compactHeight ? sourcesBack.x + sourcesBack.width + 16 : sheet.headerOuterMargin
        anchors.rightMargin: sheet.phoneLayout ? 16 : sheet.headerOuterMargin
        anchors.top: parent.top
        anchors.topMargin: sheet.compactHeight ? Math.max(sourcesBack.y, bannerStrip.height - height - 12)
            : bannerStrip.height - height - (bannerStrip.compact ? 14 : 26)
        spacing: sheet.compactHeight ? 4 : (bannerStrip.compact ? 8 : 12)
        Text {
            visible: sheet.mode !== "play" || sheet.metaLine.length > 0
            width: parent.width
            text: (sheet.mode === "season" ? "DOWNLOAD SEASON — FULL-SEASON TORRENTS" +
                       (sheet.metaLine.length ? "  ·  " + sheet.metaLine : "")
                     : sheet.mode === "download" ? "DOWNLOAD — PICK A SOURCE" +
                       (sheet.metaLine.length ? "  ·  " + sheet.metaLine : "")
                     : sheet.metaLine).toUpperCase()
            color: theme.gold; font.family: theme.ui
            font.pixelSize: sheet.phoneLayout ? 11 : 12
            font.letterSpacing: sheet.phoneLayout ? 1.5 : 4
            wrapMode: sheet.phoneLayout ? Text.WordWrap : Text.NoWrap
            elide: sheet.phoneLayout ? Text.ElideNone : Text.ElideRight
        }
        Text {
            width: parent.width
            text: sheet.title.length ? sheet.title : "Sources"
            color: theme.ink; font.family: theme.display
            font.pixelSize: sheet.phoneLayout ? 30 : (sheet.compactHeight ? 34 : 56); font.weight: Font.DemiBold
            wrapMode: sheet.phoneLayout ? Text.WordWrap : Text.NoWrap
            maximumLineCount: sheet.phoneLayout ? 3 : 1; elide: Text.ElideRight
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    // ===================== filters: the quality TOP BAR + quick-filter pills =====================
    // the full-width selector bar (the "top bar that separates the quality")
    Rectangle {
        id: topBar
        objectName: "extPickerTrigger"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: sheet.phoneLayout ? 16 : sheet.outerPageMargin
        anchors.rightMargin: sheet.phoneLayout ? 16 : sheet.outerPageMargin
        anchors.top: bannerStrip.bottom; anchors.topMargin: sheet.phoneLayout || sheet.compactControls ? 10 : 20
        height: sheet.phoneLayout || sheet.compactControls ? 52 : 64; radius: 16
        color: tbMa.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: tbMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
        Behavior on border.color { ColorAnimation { duration: 140 } }
        visible: !sheet.smartPicking && (sheet.rows.length > 0 || sheet.nuvioRows.length > 0)
        enabled: !sheet.smartPicking

        Rectangle {                                                      // grid glyph
            id: gridBadge
            anchors.left: parent.left; anchors.leftMargin: sheet.phoneLayout ? 12 : 20
            anchors.verticalCenter: parent.verticalCenter
            width: sheet.phoneLayout ? 36 : 44
            height: sheet.phoneLayout ? 36 : 44; radius: 12
            color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
            Grid {
                anchors.centerIn: parent; rows: 2; columns: 2; rowSpacing: 5; columnSpacing: 5
                Repeater { model: 4
                    delegate: Rectangle { width: 7; height: 7; radius: 2; color: theme.inkDim } }
            }
        }
        Text {
            objectName: "extPickerLabel"
            anchors.left: gridBadge.right; anchors.leftMargin: sheet.phoneLayout ? 12 : 20
            anchors.verticalCenter: parent.verticalCenter
            width: sheet.phoneLayout ? Math.max(0, topBar.width - x - 48) : implicitWidth
            elide: sheet.phoneLayout ? Text.ElideRight : Text.ElideNone
            text: sheet.providerOptions.find(option => option.key === sheet.extFilter)?.label ?? qsTr("All")
            textFormat: sheet.extFilter.indexOf("home-server:") === 0 ? Text.PlainText : Text.AutoText
            color: sheet.extFilter === "all" ? theme.ink : theme.gold
            font.family: theme.ui; font.pixelSize: sheet.phoneLayout ? 18 : 22
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: sheet.phoneLayout ? 14 : 24
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"; color: theme.inkDim; font.family: theme.display; font.pixelSize: 18
            rotation: sheet.extMenuOpen ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 140 } }
        }
        MouseArea { id: tbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: sheet.openExtMenu(extTriggerKeyboard) }
        KeyboardAction {
            id: extTriggerKeyboard; anchors.fill: parent; pointerEnabled: false
            accessibleName: qsTr("Choose source provider"); focusRadius: topBar.radius
            onTriggered: sheet.openExtMenu(extTriggerKeyboard)
        }
    }

    // quality quick-filter pills (active = gold fill)
    Flow {
        id: pills
        property int currentIndex: 0
        anchors.left: parent.left
        anchors.leftMargin: sheet.phoneLayout ? 16 : sheet.outerPageMargin
        anchors.top: topBar.bottom; anchors.topMargin: sheet.phoneLayout ? 10 : (sheet.compactControls ? 8 : 14)
        width: Math.max(0, sheet.width - (sheet.phoneLayout ? 32 : 2 * sheet.outerPageMargin))
        spacing: sheet.phoneLayout ? 8 : 12
        visible: topBar.visible
        focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => qualityKeys.handle(event)
        KeyboardCollectionController {
            id: qualityKeys; view: pills; orientation: "horizontal"; count: sheet.qualityOptions.length
            onActivated: (index) => sheet.qualityFilter = sheet.qualityOptions[index]
        }
        Repeater {
            model: sheet.qualityOptions
            delegate: Rectangle {
                id: pill
                required property string modelData
                required property int index
                objectName: "sourceQuality_" + pill.modelData
                property bool on: sheet.qualityFilter === pill.modelData
                property int n: sheet.countFor(pill.modelData)
                visible: n > 0 || pill.modelData === "all"
                width: pillRow.implicitWidth + (sheet.phoneLayout ? 24 : 36)
                height: 40; radius: 20
                color: pill.on ? theme.gold : (pMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
                border.width: pills.activeFocus && pills.currentIndex === index ? 2 : (pill.on ? 0 : 1)
                border.color: pills.activeFocus && pills.currentIndex === index ? theme.gold : theme.edge
                Row {
                    id: pillRow; anchors.centerIn: parent; spacing: 8
                    Text {
                        text: sheet.chipText(pill.modelData)
                        color: pill.on ? "#1a1306" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 15; font.weight: pill.on ? Font.Bold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: pill.n
                        color: pill.on ? Qt.rgba(0.10, 0.075, 0.02, 0.7) : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: {
                        pills.currentIndex = pill.index
                        pills.forceActiveFocus(Qt.MouseFocusReason)
                        sheet.qualityFilter = pill.modelData
                    } }
            }
        }
    }

    // ===================== the glass source table =====================
    Glass {
        id: table
        visible: !sheet.smartPicking
        enabled: !sheet.smartPicking
        backdrop: sheet.backdrop
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: sheet.phoneLayout ? 16 : sheet.outerPageMargin
        anchors.rightMargin: sheet.phoneLayout ? 16 : sheet.outerPageMargin
        objectName: "sourcesSheetTable"
        anchors.top: pills.visible ? pills.bottom : bannerStrip.bottom
        anchors.topMargin: sheet.phoneLayout ? 12 : (sheet.compactControls ? 10 : 16)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sheet.tableBottomMargin
        radius: 18
        track: 0

        Item {
            id: tableHead
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: sheet.phoneLayout ? 70 : (sheet.compactControls ? 44 : 52)
            visible: sheet.visibleRows.length > 0
            Text {
                anchors.left: parent.left; anchors.leftMargin: sheet.phoneLayout ? 14 : 26
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: sheet.phoneLayout && !sheet.smartPlayStatus.length ? -13 : 0
                width: parent.width - (sheet.phoneLayout ? 28 : 52)
                text: sheet.smartPlayStatus || sheet.visibleRows.length + (sheet.visibleRows.length === 1 ? " source" : " sources")
                      + (sheet.loading ? "  ·  still asking…" : "")
                      + (sheet.nuvioPending ? qsTr("  ·  asking Nuvio…") : "")
                color: theme.ink; font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                wrapMode: sheet.smartPlayStatus.length ? Text.WordWrap : Text.NoWrap
                elide: sheet.phoneLayout && !sheet.smartPlayStatus.length ? Text.ElideRight : Text.ElideNone
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: sheet.phoneLayout ? 14 : 26
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: sheet.phoneLayout ? 15 : 0
                width: sheet.phoneLayout ? parent.width - 28 : implicitWidth
                horizontalAlignment: sheet.phoneLayout ? Text.AlignLeft : Text.AlignRight
                elide: sheet.phoneLayout ? Text.ElideRight : Text.ElideNone
                visible: sheet.askedNames.length > 0 && !sheet.smartPlayStatus.length
                text: sheet.askedNames.length > 2
                      ? "via " + sheet.askedNames.length + " extensions"
                      : "via " + sheet.askedNames.join(" · ")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                font.letterSpacing: sheet.phoneLayout ? 0 : 1
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
        }

        Text {
            id: emptyText
            objectName: "sourcesSheetEmptyText"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: extensionsAction.visible ? -((extensionsAction.height + 12) / 2) : 0
            visible: sheet.visibleRows.length === 0
            width: Math.max(0, table.width - 36)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: sheet.smartPlayStatus || (sheet.loading ? "Finding sources…"
                  : sheet.nuvioPending ? qsTr("Finding Nuvio sources…")
                  : (sheet.timedOut ? "Sources timed out. Try again."
                     : (sheet.askedCount === 0 && !sheet.nuvioAsked
                        ? (sheet.androidHost
                            ? "No stream extension carries this title — add one on the Extensions page."
                            : "No stream source is enabled for this title. Enable one in Extensions.")
                        : "No sources found.")))
            color: sheet.timedOut ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: sheet.phoneLayout ? 15 : 16
        }

        Rectangle {
            id: extensionsAction
            objectName: "sourcesSheetExtensionsRoute"
            visible: sheet.open && sheet.mode === "play" && !sheet.loading && !sheet.nuvioPending && !sheet.timedOut
                     && sheet.rowCount === 0 && (!sheet.androidHost || sheet.askedCount === 0)
            anchors.top: emptyText.bottom
            anchors.topMargin: 12
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(112, extensionsActionLabel.implicitWidth + 32)
            height: sheet.phoneLayout ? 48 : 44
            radius: height / 2
            color: extensionsActionKeyboard.interactionActive ? theme.glassHi : "transparent"
            border.width: 1
            border.color: theme.gold
            Text {
                id: extensionsActionLabel
                objectName: "sourcesSheetExtensionsLabel"
                anchors.centerIn: parent
                text: sheet.androidHost ? "Extensions" : "Find sources by adding extensions"
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            KeyboardAction {
                id: extensionsActionKeyboard
                objectName: "sourcesSheetExtensionsAction"
                anchors.fill: parent
                visible: extensionsAction.visible
                accessibleName: extensionsActionLabel.text
                focusRadius: parent.radius
                onTriggered: {
                    var browseMoreSources = sheet.askedCount > 0 || sheet.nuvioAsked
                    sheet.hide()
                    sheet.openExtensionsRequested(browseMoreSources)
                }
            }
        }

        ListView {
            id: list
            objectName: "sourcesSheetList"
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: tableHead.bottom; anchors.bottom: parent.bottom
            anchors.topMargin: 4; anchors.bottomMargin: 8
            clip: true
            visible: !sheet.smartPicking && sheet.visibleRows.length > 0
            enabled: !sheet.smartPicking
            model: sheet.visibleRows
            boundsBehavior: Flickable.StopAtBounds
            focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: (event) => sourceKeys.handle(event)
            KeyboardCollectionController {
                id: sourceKeys; view: list; orientation: "vertical"; count: sheet.visibleRows.length
                contextEnabled: true
                onActivated: (index) => sheet.activateSource(index)
                onContextRequested: (index) => sheet.openSourceContext(index)
            }
            ScrollBar.vertical: HouseScrollBar { flick: list }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                property bool copiedTick: false
                function markCopied() { row.copiedTick = true; copyTickTimer.restart() }
                // Slice-2 automation surface (no visual effect): names each source row and exposes
                // read-only state so the Lanista bridge can address it and prove slices 5–7 in the
                // running app. dump-ui sees only named items; this is that name.
                objectName: "sourceRow_" + row.index
                // Slice 8a: read-only automation surface (additive, no behavior change) — the row's
                // own infoHash, cheapest identity a watch-party acceptance can read via qml-get
                // without depending on UI text formatting. Empty for non-torrent (Direct/HTTP) rows.
                readonly property string automationInfoHash: row.modelData && row.modelData.infoHash ? row.modelData.infoHash : ""
                // Automation surface (additive, no behavior change): the row's
                // own seeder count, so a drive can pick a well-seeded row
                // without depending on parsing the rendered meta-line text.
                // -1 (not >= 0) when the field is absent (e.g. non-torrent rows).
                readonly property int automationSeeders: row.modelData && row.modelData.seeders !== undefined ? row.modelData.seeders : -1
                // Automation surface (additive, no behavior change): the row's
                // quality tag (e.g. "1080p"/"2160p"), a same-pattern size proxy
                // when no raw byte count is available on the model.
                readonly property string automationQuality: row.modelData && row.modelData.quality ? row.modelData.quality : ""
                property string streamKind: row.modelData.streamKind || "Torrent"
                property string providerName: row.modelData.addonName || row.modelData.sourceName || ""
                // WT-03 advisory decoder evidence: only an explicit "unsupported" is shown;
                // unknown stays silent and the row remains selectable.
                readonly property bool codecUnsupported: !!(row.modelData.videoCompatibility
                    && row.modelData.videoCompatibility.state === "unsupported")
                readonly property string codecCompatibilityLabel: row.codecUnsupported
                    ? "Unsupported on this device — " + (row.modelData.videoCompatibility.reason || "Video codec profile is unsupported")
                    : ""
                // Today every shown row is ready; slice 5 drives HTTP rows "checking" → "confirmed".
                // Plain string on purpose — ui-wait-for polls strict-equality only.
                property string checkState: "confirmed"
                // Slice 7 flips this when a series pre-picks the source that worked last episode.
                property bool preselected: false
                Text {
                    objectName: "sourceBestMatch_" + row.index
                    visible: row.index === sheet.smartPlayVisibleIndex
                    x: row.width - width - (sheet.phoneLayout ? 14 : 30); y: 4
                    text: qsTr("Best match")
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                }
                width: ListView.view.width
                height: sheet.phoneLayout ? Math.max(44, mobileContent.implicitHeight + 28 + (row.index === sheet.smartPlayVisibleIndex ? 20 : 0))
                        : row.modelData.nuvio ? Math.max(96, desktopContent.implicitHeight + 20)
                        : 150

                Rectangle { anchors.fill: parent; color: list.activeFocus && list.currentIndex === row.index
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.08)
                    : (rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent") }

                // provider logo
                Rectangle {
                    id: logo
                    visible: !sheet.phoneLayout
                    anchors.left: parent.left; anchors.leftMargin: 26
                    anchors.verticalCenter: parent.verticalCenter
                    width: 54; height: 54; radius: 12
                    color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                    Text { anchors.centerIn: parent
                        text: (row.modelData.addonName || "?").charAt(0)
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 24; font.weight: Font.DemiBold }
                }

                // copy column — every element, clean hierarchy
                Column {
                    id: desktopContent
                    visible: !sheet.phoneLayout
                    anchors.left: logo.right; anchors.leftMargin: 24
                    anchors.right: dlBtn.visible ? dlBtn.left : copyBtn.visible ? copyBtn.left : play.left
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: row.modelData.nuvio ? 5 : 7

                    Row {
                        visible: !row.modelData.nuvio
                        spacing: 12
                        Text { text: row.modelData.addonName; color: theme.ink
                            objectName: "sourceProviderWide_" + row.index
                            textFormat: row.modelData.homeServer ? Text.PlainText : Text.AutoText
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { text: row.modelData.qualityLine || row.modelData.quality
                            color: theme.gold
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.5
                            anchors.verticalCenter: parent.verticalCenter }
                    }
                    NuvioHeading {
                        objectName: "nuvioSourceHeading"
                        visible: !!row.modelData.nuvio
                        width: parent.width
                        providerName: row.providerName
                        quality: row.modelData.qualityLine || row.modelData.quality || ""
                        palette: theme
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.release
                        objectName: "sourceReleaseWide_" + row.index
                        textFormat: row.modelData.homeServer ? Text.PlainText : Text.AutoText
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: sheet.metaText(row.modelData)
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight
                    }
                    Text {
                        objectName: "sourceCompatibilityWide_" + row.index
                        visible: row.codecUnsupported
                        width: parent.width
                        text: row.codecCompatibilityLabel
                        color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    Row {
                        spacing: 8
                        visible: row.modelData.audio !== undefined
                        Text { text: row.modelData.audio || ""; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Repeater {
                            model: row.modelData.languages || []
                            delegate: Rectangle {
                                required property string modelData
                                width: lg.implicitWidth + 12; height: 18; radius: 4
                                color: "transparent"; border.width: 1; border.color: theme.edge
                                anchors.verticalCenter: parent.verticalCenter
                                Text { id: lg; anchors.centerIn: parent; text: parent.modelData
                                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1 }
                            }
                        }
                    }
                    Row {
                        spacing: 7
                        Repeater {
                            model: row.modelData.nuvio
                                   ? (row.modelData.tags || []).filter(tag => tag !== row.modelData.quality)
                                   : row.modelData.tags || []
                            delegate: Rectangle {
                                required property string modelData
                                width: tg.implicitWidth + 16; height: 20; radius: 6
                                color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                                Text { id: tg; anchors.centerIn: parent; text: parent.modelData; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            }
                        }
                    }
                    Text {
                        objectName: "sourceStreamType"
                        visible: !row.modelData.nuvio
                        text: sheet.streamTypeText(row.modelData)
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }

                // always-visible action button — solid gold, real triangle glyph
                Rectangle {
                    id: play
                    visible: !sheet.phoneLayout
                    anchors.right: parent.right; anchors.rightMargin: 30
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56; height: 56; radius: 28; color: theme.gold
                    scale: rowMa.containsMouse ? 1.05 : 1.0
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    Text { anchors.centerIn: parent
                        anchors.horizontalCenterOffset: sheet.mode === "play" ? 2 : 0
                        text: sheet.mode === "play" ? "▶" : "↓"
                        color: "#1a1306"; font.pixelSize: 18
                        font.weight: sheet.mode === "play" ? Font.Normal : Font.DemiBold }
                }

                MouseArea {
                    id: rowMa; anchors.fill: parent; hoverEnabled: true
                    ToolTip.text: sheet.mode === "play" ? qsTr("Play source") : qsTr("Download source")
                    ToolTip.visible: ((!touchTooltipRowMa.longPressTooltipEnabled && containsMouse) || touchTooltipRowMa.active) && mouseX >= play.x && mouseX < play.x + play.width && mouseY >= play.y && mouseY < play.y + play.height
                    ToolTip.delay: touchTooltipRowMa.longPressTooltipEnabled ? 0 : 350
                    visible: !sheet.phoneLayout
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        sheet.activateSource(row.index)
                    }
                    App.TouchTooltip { id: touchTooltipRowMa; target: rowMa }
                }

                // copy link — the row's second verb (spec 2026-07-08). Torrents copy a
                // magnet, direct-HTTP rows copy their url; a brief tick confirms, playback
                // stays on the row click / gold button, untouched. Declared AFTER rowMa so
                // it stacks ABOVE it — copy clicks never fall through into playback.
                Rectangle {
                    id: copyBtn
                    objectName: "sourceCopyWide_" + row.index
                    visible: !sheet.phoneLayout && !row.modelData.homeServer
                    anchors.right: play.left; anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40; radius: 20
                    color: copyMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                    border.width: 1; border.color: theme.edge
                    Text {
                        anchors.centerIn: parent
                        text: row.copiedTick ? "✓" : "⧉"   // ✓ tick / ⧉ two-squares copy glyph
                        color: row.copiedTick ? theme.gold : (copyMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.ui; font.pixelSize: 15
                    }
                    MouseArea {
                        id: copyMa; anchors.fill: parent; hoverEnabled: true
                        ToolTip.text: row.copiedTick ? qsTr("Source link copied") : qsTr("Copy source link")
                        ToolTip.visible: ((!touchTooltipCopyMa.longPressTooltipEnabled && containsMouse) || touchTooltipCopyMa.active)
                        ToolTip.delay: touchTooltipCopyMa.longPressTooltipEnabled ? 0 : 350
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            list.currentIndex = row.index
                            list.forceActiveFocus(Qt.MouseFocusReason)
                            sheet.copySource(row.index)
                        }
                        App.TouchTooltip { id: touchTooltipCopyMa; target: copyMa }
                    }
                }
                Timer { id: copyTickTimer; interval: 1200; onTriggered: row.copiedTick = false }

                // download — the row's third verb (2026-07-19), beside the copy: queue
                // THIS title for download pinned to THIS torrent. Play-mode only (in
                // download mode the whole row already IS the download pick). Stays on
                // the sheet; the tick is per-TITLE, not per-row — see titleQueued above.
                Rectangle {
                    id: dlBtn
                    objectName: "sourceDownloadWide_" + row.index
                    visible: !sheet.phoneLayout && !row.modelData.homeServer && sheet.mode === "play" && typeof Download !== "undefined"
                    anchors.right: copyBtn.left; anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40; radius: 20
                    color: dlMa.containsMouse && !sheet.titleQueued ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                    border.width: 1; border.color: theme.edge
                    Text {
                        anchors.centerIn: parent
                        text: sheet.titleQueued ? "✓" : "↓"   // ✓ on its way / ↓ download this torrent
                        color: sheet.titleQueued ? theme.gold : (dlMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: dlMa; anchors.fill: parent; hoverEnabled: true
                        ToolTip.text: sheet.titleQueued ? qsTr("Download queued") : qsTr("Download source")
                        ToolTip.visible: ((!touchTooltipDlMa.longPressTooltipEnabled && containsMouse) || touchTooltipDlMa.active)
                        ToolTip.delay: touchTooltipDlMa.longPressTooltipEnabled ? 0 : 350
                        cursorShape: sheet.titleQueued ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: {
                            list.currentIndex = row.index
                            list.forceActiveFocus(Qt.MouseFocusReason)
                            sheet.downloadSource(row.index)
                        }
                        App.TouchTooltip { id: touchTooltipDlMa; target: dlMa }
                    }
                }

                // Phone-only row: one readable vertical stack, with the same data and actions.
                MouseArea {
                    id: mobileRowMa
                    anchors.fill: parent
                    visible: sheet.phoneLayout
                    onClicked: {
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        sheet.activateSource(row.index)
                    }
                }
                Column {
                    id: mobileContent
                    visible: sheet.phoneLayout
                    x: 14; y: row.index === sheet.smartPlayVisibleIndex ? 34 : 14
                    width: Math.max(0, row.width - 28)
                    spacing: 8

                    Row {
                        width: parent.width
                        spacing: 12
                        Rectangle {
                            width: 44; height: 44; radius: 10
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1; border.color: theme.edge
                            Text {
                                anchors.centerIn: parent
                                text: (row.modelData.addonName || "?").charAt(0)
                                color: theme.ink; font.family: theme.display
                                font.pixelSize: 22; font.weight: Font.DemiBold
                            }
                        }
                        Column {
                            visible: !row.modelData.nuvio
                            width: Math.max(0, mobileContent.width - 56)
                            spacing: 4
                            Text {
                                width: parent.width
                                text: row.modelData.addonName || ""
                                objectName: "sourceProviderPhone_" + row.index
                                textFormat: row.modelData.homeServer ? Text.PlainText : Text.AutoText
                                color: theme.ink; font.family: theme.ui
                                font.pixelSize: 15; font.weight: Font.DemiBold
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 2; elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: row.modelData.qualityLine || row.modelData.quality || ""
                                color: theme.gold; font.family: theme.ui
                                font.pixelSize: 13; font.weight: Font.DemiBold
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                        NuvioHeading {
                            objectName: "nuvioSourceHeadingPhone"
                            visible: !!row.modelData.nuvio
                            width: Math.max(0, mobileContent.width - 56)
                            allowWrap: true
                            providerName: row.providerName
                            quality: row.modelData.qualityLine || row.modelData.quality || ""
                            palette: theme
                        }
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.release || ""
                        objectName: "sourceReleasePhone_" + row.index
                        textFormat: row.modelData.homeServer ? Text.PlainText : Text.AutoText
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 2; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: sheet.metaText(row.modelData)
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere
                    }
                    Flow {
                        width: parent.width; spacing: 8
                        visible: row.modelData.audio !== undefined
                        Text {
                            text: row.modelData.audio || ""
                            color: theme.ink; font.family: theme.ui
                            font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: row.modelData.languages || []
                            delegate: Rectangle {
                                required property string modelData
                                width: Math.min(languageText.implicitWidth + 12, mobileContent.width)
                                height: 24; radius: 5
                                color: "transparent"; border.width: 1; border.color: theme.edge
                                Text {
                                    id: languageText; anchors.centerIn: parent
                                    width: parent.width - 12
                                    text: modelData; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    Flow {
                        width: parent.width; spacing: 7
                        Repeater {
                            model: row.modelData.tags || []
                            delegate: Rectangle {
                                required property string modelData
                                width: Math.min(tagText.implicitWidth + 16, mobileContent.width)
                                height: 24; radius: 6
                                color: Qt.rgba(1, 1, 1, 0.05)
                                border.width: 1; border.color: theme.edge
                                Text {
                                    id: tagText; anchors.centerIn: parent
                                    width: parent.width - 16
                                    text: modelData; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "sourceStreamTypePhone"
                        visible: !row.modelData.nuvio
                        width: parent.width
                        text: sheet.streamTypeText(row.modelData)
                        color: theme.inkDimmer; font.family: theme.ui
                        font.pixelSize: 12; font.weight: Font.DemiBold
                        wrapMode: Text.WrapAnywhere
                    }
                    Row {
                        width: parent.width; height: 48; spacing: 12
                        layoutDirection: Qt.RightToLeft
                        Rectangle {
                            width: 56; height: 48; radius: 24; color: theme.gold
                            Text {
                                anchors.centerIn: parent
                                text: sheet.mode === "play" ? "▶" : "↓"
                                color: "#1a1306"; font.pixelSize: 18
                            }
                            MouseArea {
                                objectName: "sourcePlay_" + row.index
                                anchors.fill: parent
                                ToolTip.text: sheet.mode === "play" ? qsTr("Play source") : qsTr("Download source")
                                ToolTip.visible: ((!touchTooltipMobilePlay.longPressTooltipEnabled && mobilePlayHover.hovered) || touchTooltipMobilePlay.active)
                                ToolTip.delay: touchTooltipMobilePlay.longPressTooltipEnabled ? 0 : 350
                                HoverHandler { id: mobilePlayHover }
                                onClicked: {
                                    list.currentIndex = row.index
                                    list.forceActiveFocus(Qt.MouseFocusReason)
                                    sheet.activateSource(row.index)
                                }
                                App.TouchTooltip { id: touchTooltipMobilePlay; target: parent }
                            }
                        }
                        Rectangle {
                            visible: !row.modelData.homeServer
                            width: 48; height: 48; radius: 24
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1; border.color: theme.edge
                            Text {
                                anchors.centerIn: parent
                                text: row.copiedTick ? "✓" : "⧉"
                                color: row.copiedTick ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 16
                            }
                            MouseArea {
                                objectName: "sourceCopy_" + row.index
                                anchors.fill: parent
                                ToolTip.text: row.copiedTick ? qsTr("Source link copied") : qsTr("Copy source link")
                                ToolTip.visible: ((!touchTooltipMobileCopy.longPressTooltipEnabled && mobileCopyHover.hovered) || touchTooltipMobileCopy.active)
                                ToolTip.delay: touchTooltipMobileCopy.longPressTooltipEnabled ? 0 : 350
                                HoverHandler { id: mobileCopyHover }
                                onClicked: {
                                    list.currentIndex = row.index
                                    list.forceActiveFocus(Qt.MouseFocusReason)
                                    sheet.copySource(row.index)
                                }
                                App.TouchTooltip { id: touchTooltipMobileCopy; target: parent }
                            }
                        }
                        Rectangle {
                            visible: !row.modelData.homeServer && sheet.mode === "play" && typeof Download !== "undefined"
                            width: 48; height: 48; radius: 24
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1; border.color: theme.edge
                            Text {
                                anchors.centerIn: parent
                                text: sheet.titleQueued ? "✓" : "↓"
                                color: sheet.titleQueued ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 16
                            }
                            MouseArea {
                                objectName: "sourceDownload_" + row.index
                                anchors.fill: parent
                                ToolTip.text: sheet.titleQueued ? qsTr("Download queued") : qsTr("Download source")
                                ToolTip.visible: ((!touchTooltipMobileDownload.longPressTooltipEnabled && mobileDownloadHover.hovered) || touchTooltipMobileDownload.active)
                                ToolTip.delay: touchTooltipMobileDownload.longPressTooltipEnabled ? 0 : 350
                                HoverHandler { id: mobileDownloadHover }
                                onClicked: {
                                    list.currentIndex = row.index
                                    list.forceActiveFocus(Qt.MouseFocusReason)
                                    sheet.downloadSource(row.index)
                                }
                                App.TouchTooltip { id: touchTooltipMobileDownload; target: parent }
                            }
                        }
                    }
                }
            }
        }

        ScrollGlide { flick: list }
    }

    Rectangle {
        id: sourceContextMenu
        visible: sheet.sourceContextOpen && !sheet.smartPicking
        enabled: !sheet.smartPicking
        z: 120
        anchors.centerIn: parent
        width: 210; height: sourceContextCol.implicitHeight + 12; radius: 12
        color: Qt.rgba(0.045, 0.05, 0.075, 0.98); border.width: 1; border.color: theme.edge
        FocusScope {
            id: sourceContextFocus; anchors.fill: parent
            Keys.onPressed: (event) => {
                var n=sheet.sourceContextOptions().length
                if (event.key === Qt.Key_Escape) { sheet.closeSourceContext(true); event.accepted=true; return }
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted=true; return }
                if (event.key === Qt.Key_Up) sheet.sourceContextChoice=(sheet.sourceContextChoice+n-1)%n
                else if (event.key === Qt.Key_Down) sheet.sourceContextChoice=(sheet.sourceContextChoice+1)%n
                else if (event.key === Qt.Key_Home) sheet.sourceContextChoice=0
                else if (event.key === Qt.Key_End) sheet.sourceContextChoice=n-1
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    sheet.activateSourceContext(sheet.sourceContextChoice); event.accepted=true; return
                } else return
                event.accepted=true
            }
        }
        Column {
            id: sourceContextCol; anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.topMargin: 6
            Repeater {
                model: sheet.sourceContextOptions()
                delegate: Rectangle {
                    required property string modelData; required property int index
                    width: sourceContextCol.width; height: 36; radius: 8
                    color: sourceContextFocus.activeFocus && sheet.sourceContextChoice === index ? Qt.rgba(1,1,1,0.11) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sheet.activateSourceContext(index) }
                }
            }
        }
    }

    // ===================== extension picker menu (declared after the table so it stacks above) =====
    // Click-away closer: any click outside the menu closes it without falling through.
    MouseArea {
        anchors.fill: parent
        visible: sheet.extMenuOpen && !sheet.smartPicking
        enabled: !sheet.smartPicking
        onClicked: sheet.closeExtMenu(true)
    }
    Rectangle {
        id: extMenu
        objectName: "extPickerMenu"
        visible: sheet.extMenuOpen && !sheet.smartPicking
        enabled: !sheet.smartPicking
        anchors.left: topBar.left; anchors.right: topBar.right
        anchors.top: topBar.bottom; anchors.topMargin: 6
        // Bounded by the sheet so every extension stays reachable on short viewports.
        height: Math.max(0, Math.min(extMenuCol.implicitHeight + 16,
                                     sheet.height - topBar.y - topBar.height - 6 - 12))
        clip: true
        radius: 16
        color: "#141414"
        border.width: 1; border.color: theme.edge
        FocusScope {
            id: extMenuFocus; anchors.fill: parent; z: 5
            objectName: "extPickerMenuFocus"
            Keys.onPressed: (event) => {
                var n=sheet.providerOptions.length
                if (event.key === Qt.Key_Escape) { sheet.closeExtMenu(true); event.accepted=true; return }
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted=true; return }
                if (!n) return
                var index = sheet.extMenuIndex
                if (event.key === Qt.Key_Up) index = (index + n - 1) % n
                else if (event.key === Qt.Key_Down) index = (index + 1) % n
                else if (event.key === Qt.Key_Home) index = 0
                else if (event.key === Qt.Key_End) index = n - 1
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    sheet.activateExtOption(sheet.providerOptions[index].key); event.accepted=true; return
                } else return
                sheet.extMenuKey = sheet.providerOptions[index].key
                event.accepted=true
            }
        }
        Flickable {
            id: extMenuFlick
            objectName: "extPickerScroll"
            anchors.fill: parent
            contentWidth: width
            contentHeight: extMenuCol.implicitHeight + 16
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            Connections {
                target: sheet
                function onExtMenuIndexChanged() {
                    var top = 8 + sheet.extMenuIndex * 52
                    if (top < extMenuFlick.contentY) extMenuFlick.contentY = top - 8
                    else if (top + 52 > extMenuFlick.contentY + extMenuFlick.height)
                        extMenuFlick.contentY = Math.min(extMenuFlick.contentHeight - extMenuFlick.height,
                                                         top + 52 + 8 - extMenuFlick.height)
                }
            }
            Column {
                id: extMenuCol
                width: extMenuFlick.width
                y: 8
                Repeater {
                    model: sheet.extMenuOpen ? sheet.providerOptions : []
                    delegate: Item {
                        id: extOpt
                        required property var modelData
                        required property int index
                        objectName: "extPick_" + extOpt.index
                        property bool on: sheet.extFilter === extOpt.modelData.key
                        width: parent.width; height: 52
                        Rectangle {
                            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                            radius: 10
                            color: extMenuFocus.activeFocus && sheet.extMenuIndex === extOpt.index
                                ? Qt.rgba(0.94, 0.77, 0.29, 0.10)
                                : (extOptMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 28
                            anchors.verticalCenter: parent.verticalCenter
                            text: extOpt.modelData.label
                            objectName: "extPickLabel_" + extOpt.index
                            textFormat: extOpt.modelData.key.indexOf("home-server:") === 0 ? Text.PlainText : Text.AutoText
                            color: extOpt.on ? theme.gold : theme.ink
                            font.family: theme.ui; font.pixelSize: 16
                            font.weight: extOpt.on ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 28
                            anchors.verticalCenter: parent.verticalCenter
                            text: extOpt.modelData.count + (extOpt.modelData.count === 1 ? " source" : " sources")
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
                        }
                        MouseArea {
                            id: extOptMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.activateExtOption(extOpt.modelData.key)
                        }
                    }
                }
            }
        }
    }
}
