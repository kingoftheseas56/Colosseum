// DownloadsPage — everything the house holds locally, in one full page.
// Ratified design: agents/colosseum-downloads-mock.html (2026-07-04, "go with it").
// Structure IS the information: "Now arriving" (live jobs, cross-world) answers a
// different question than the vault shelves (settled files, world → series → item),
// so they are separate surfaces in that order. Data = LocalDownloads (read-model);
// every action routes back to the owning backend. No sample data — empty lanes
// say so honestly and route to their world.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal openRequested(var item)           // completed row → host routes by world/kind
    signal openWorldRequested(string world)  // empty-lane CTA → host opens that world
    signal playArrivingRequested(var job)    // live theatre job → host streams the same url
    signal openAudiobookRequested(var item)

    Theme { id: theme }

    // ---- read-model bindings (revision-driven refresh) ----
    property var downloadsApi: (typeof LocalDownloads !== "undefined") ? LocalDownloads : null
    property var audiobooksApi: (typeof Audiobooks !== "undefined") ? Audiobooks : null
    property var jobs: []
    property var jobGroups: []
    property int liveJobCount: 0
    property int attentionCount: 0
    property var openGroups: ({})
    property var laneSeries: ({})      // world -> series list
    property var totalsMap: ({})
    property string openLedgerWorld: ""
    property string openLedgerKey: ""
    property var ledgerItems: []
    property string mutationMessage: ""

    property bool confirmationOpen: false
    property string confirmationTitle: ""
    property string confirmationBody: ""
    property string confirmationLabel: ""
    property var confirmationCallback: null

    function confirmAction(title, body, label, callback) {
        confirmationTitle = title;
        confirmationBody = body;
        confirmationLabel = label;
        confirmationCallback = callback;
        confirmationOpen = true;
    }
    function closeConfirmation() {
        confirmationOpen = false;
        confirmationCallback = null;
    }
    function runConfirmedAction() {
        var callback = confirmationCallback;
        closeConfirmation();
        if (callback) callback();
    }
    function finishMutation(result, fallbackMessage) {
        if (result && result.success === false) {
            mutationMessage = result.message || fallbackMessage;
            return false;
        }
        mutationMessage = "";
        refresh();
        return true;
    }
    function isLiveState(state) {
        return state === "queued" || state === "resolving"
                || state === "downloading" || state === "paused"
                || state === "extracting"
                // manga-volume acquisition states (MangaTankobanService) — a group entirely
                // mid-pack or mid-ingest must still count as live, not "0 of N landed".
                || state === "packing" || state === "ingesting";
    }

    // ---- source-cooldown visibility (Task 11) ----
    // MangaDownloader (exposed to QML as `Downloads`) emits paused(chapterId,
    // resumeInMs) each time a soft-blocked source parks a page-image download for
    // 120s. Nothing consumed it, so a cooling job read as stuck ("0 of 58 pages").
    // We hold resume-at wall-clock per chapterId and surface an honest live
    // countdown on its "Now arriving" row, cleared on the next progress/finish/fail.
    property var coolMap: ({})              // chapterId -> resume-at epoch ms (absent = not cooling)

    // ---- audiobooks (A2 lane) ----
    // The audiobook engine (`Audiobooks`) keys by pairKey ("title|author") and enumerates only
    // COMPLETED sets (downloadedAudiobooks). Active jobs are mirrored HERE from its progress
    // signals — a download streams in chunks, so an already-running job repaints within a beat
    // of this page opening. (This closes the 2026-07-18 gap: audiobook downloads ran invisible.)
    property var abActive: ({})        // pairKey -> { state: "resolving"|"downloading"|"failed", pct }
    property var abDone: []            // downloadedAudiobooks() snapshot: {id,title,author,bytes,addedAt,missing,fileCount}
    function abRefresh() {
        abDone = audiobooksApi ? audiobooksApi.downloadedAudiobooks() : [];
        var m = {};
        if (audiobooksApi && audiobooksApi.activeDownloads) {
            var act = audiobooksApi.activeDownloads() || [];
            for (var i = 0; i < act.length; i++) {
                var item = act[i];
                m[item.id] = {
                    state: item.state,
                    title: item.title || "",
                    author: item.author || "",
                    error: item.error || "",
                    received: item.received || 0,
                    total: item.total || 0,
                    pct: item.total > 0 ? item.received / item.total : 0
                };
            }
        }
        abActive = m;
    }
    function abTitleOf(key) {          // pairKey → display title (its lowercased "title|author" front half)
        var p = String(key || "").split("|");
        return p[0] ? p[0] : String(key || "");
    }
    Connections {
        target: root.audiobooksApi
        ignoreUnknownSignals: true
        function onResolving(key)          { var m = Object.assign({}, root.abActive); var old = m[key] || ({}); m[key] = { state: "resolving", title: old.title || "", author: old.author || "", error: "", received: 0, total: 0, pct: 0 }; root.abActive = m }
        function onProgress(key, rcv, tot) { var m = Object.assign({}, root.abActive); var old = m[key] || ({}); m[key] = { state: "downloading", title: old.title || "", author: old.author || "", error: "", received: rcv, total: tot, pct: tot > 0 ? rcv / tot : 0 }; root.abActive = m }
        function onFinished(key, path)     { var m = root.abActive; delete m[key]; root.abActive = m; root.abRefresh() }
        function onFailed(key, why)        { var m = Object.assign({}, root.abActive); var old = m[key] || ({}); m[key] = { state: "failed", title: old.title || "", author: old.author || "", error: why || "download failed", received: old.received || 0, total: old.total || 0, pct: old.pct || 0 }; root.abActive = m }
        function onFailuresChanged()       { root.abRefresh() }
    }
    property double nowTick: Date.now()     // bumped 1/s while anything cools, drives the countdown
    readonly property bool anyCooling: {
        var m = root.coolMap;
        for (var k in m) if (m[k] - root.nowTick > 0) return true;
        return false;
    }

    readonly property var worlds: [
        { key: "tankoban", title: "Tankoban", unit: "chapters & issues" },
        { key: "biblio",   title: "Biblio",   unit: "books" },
        { key: "theatre",  title: "Theatre",  unit: "files" }
    ]

    function refresh() {
        abRefresh();                       // audiobooks ride their own engine, not LocalDownloads
        if (!downloadsApi) return;
        jobs = downloadsApi.activeJobs();
        jobGroups = groupJobs(jobs);
        var live = 0;
        var attention = 0;
        for (var k = 0; k < jobs.length; k++)
            if (isLiveState(jobs[k].state)) live++;
            else if (jobs[k].state === "failed") attention++;
        for (var abKey in abActive) {
            if (isLiveState(abActive[abKey].state)) live++;
            else if (abActive[abKey].state === "failed") attention++;
        }
        liveJobCount = live;
        attentionCount = attention;
        // drop cooldowns whose job left the queue without a terminal signal
        // (e.g. cancelled) so a stale countdown never lingers.
        var present = {};
        for (var c = 0; c < jobs.length; c++) present[jobs[c].id] = true;
        var pruned = {}, dropped = false;
        for (var ck in coolMap) { if (present[ck]) pruned[ck] = coolMap[ck]; else dropped = true; }
        if (dropped) coolMap = pruned;
        var baseTotals = downloadsApi.totals || ({});
        var audioBytes = 0;
        for (var a = 0; a < abDone.length; a++) audioBytes += (abDone[a].bytes || 0);
        totalsMap = {
            items: (baseTotals.items || 0) + abDone.length,
            bytes: (baseTotals.bytes || 0) + audioBytes,
            tankoban: baseTotals.tankoban || 0,
            biblio: baseTotals.biblio || 0,
            theatre: baseTotals.theatre || 0,
            audiobook: abDone.length,
            active: live,
            attention: attention
        };
        var lanes = {};
        for (var i = 0; i < worlds.length; i++)
            lanes[worlds[i].key] = downloadsApi.series(worlds[i].key);
        laneSeries = lanes;
        if (openLedgerKey.length) {
            ledgerItems = downloadsApi.items(openLedgerWorld, openLedgerKey);
            computeLedgerSeasons();
        }
    }

    function toggleLedger(world, key) {
        if (openLedgerWorld === world && openLedgerKey === key) {
            openLedgerWorld = ""; openLedgerKey = ""; ledgerItems = [];
            ledgerSeasonList = []; openSeasons = ({});
            return;
        }
        openLedgerWorld = world;
        openLedgerKey = key;
        ledgerItems = downloadsApi ? downloadsApi.items(world, key) : [];
        openSeasons = ({});
        computeLedgerSeasons();
    }

    function toggleGroup(key) {
        var next = {};
        for (var k in openGroups) next[k] = openGroups[k];
        next[key] = !next[key];
        openGroups = next;   // reassign so bindings wake
    }

    property var ledgerSeasonList: []   // [{season, items, bytes, newest, arriving}] or [] = flat
    property var openSeasons: ({})

    function toggleSeason(s) {
        var next = {};
        for (var k in openSeasons) next[k] = openSeasons[k];
        next[s] = !next[s];
        openSeasons = next;
    }
    // Season groupKey for a ledger item — same derivation as the engine's
    // groupKeyFor: series base = stream id minus its trailing :season:episode.
    function seasonKeyFor(item, season) {
        var parts = (item.id || "").split(":");
        if (parts.length >= 3)
            return parts.slice(0, parts.length - 2).join(":") + ":s" + season;
        return "";
    }
    function computeLedgerSeasons() {
        var out = [], byS = {}, any = false;
        if (openLedgerWorld === "theatre") {
            for (var i = 0; i < ledgerItems.length; i++) {
                var e = ledgerItems[i];
                if (e.kind === "episode" && (e.season || 0) > 0) any = true;
            }
        }
        if (!any) { ledgerSeasonList = []; return; }
        for (var j = 0; j < ledgerItems.length; j++) {
            var it = ledgerItems[j];
            var s = it.season || 0;
            if (!byS[s]) { byS[s] = { season: s, items: [], bytes: 0, newest: 0, arriving: 0 }; out.push(byS[s]); }
            byS[s].items.push(it);
            byS[s].bytes += (it.bytes || 0);
            byS[s].newest = Math.max(byS[s].newest, it.addedAt || 0);
        }
        // live cross-reference: prefer the groupKey join (same derivation as the
        // engine: series base = stream id minus trailing :season:episode); title
        // match is only the fallback for rows whose id carries no series identity.
        for (var k = 0; k < jobs.length; k++) {
            var jb = jobs[k];
            if (jb.world !== "theatre" || jb.state === "done") continue;
            for (var g = 0; g < out.length; g++) {
                var first = out[g].items[0];
                var wantKey = seasonKeyFor(first, out[g].season);
                if (wantKey.length > 0
                        ? (jb.groupKey === wantKey)
                        : ((first.seriesTitle || "").toLowerCase() === (jb.seriesTitle || "").toLowerCase()
                           && (jb.season || 0) === out[g].season))
                    out[g].arriving++;
            }
        }
        out.sort(function(a, b) { return a.season - b.season; });
        // Default fold (newest season open) ONLY when the ledger was just opened —
        // refresh() re-runs every progress tick and must never stomp the user's folds.
        if (Object.keys(openSeasons).length === 0) {
            var open = {};
            var newestSeason = out[0] ? out[0].season : 0, newestAt = -1;
            for (var m = 0; m < out.length; m++)
                if (out[m].newest > newestAt) { newestAt = out[m].newest; newestSeason = out[m].season; }
            open[newestSeason] = true;
            openSeasons = open;
        }
        ledgerSeasonList = out;
    }

    function fmtBytes(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GB";
        if (b >= 1048576) return Math.round(b / 1048576) + " MB";
        if (b > 0) return Math.max(1, Math.round(b / 1024)) + " KB";
        return "";
    }
    function fmtWhen(secs) {
        if (!secs) return "";
        var d = new Date(secs * 1000), now = new Date();
        var days = Math.floor((now - d) / 86400000);
        if (days <= 0) return "added today";
        if (days === 1) return "added yesterday";
        return "added " + Qt.formatDate(d, "MMMM d");
    }
    function fmtSpeed(bps) {
        if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s";
        if (bps >= 1024) return Math.round(bps / 1024) + " KB/s";
        return "";
    }
    function fmtEta(secs) {
        if (secs === undefined || secs === null || secs < 0) return "";
        if (secs >= 5400) return "~" + (secs / 3600).toFixed(1) + " h left";
        if (secs >= 60) return "~" + Math.round(secs / 60) + " min left";
        return "~" + Math.round(secs) + " s left";
    }
    // ms until a cooling job resumes (0 = not cooling). Reads nowTick + coolMap so
    // any binding calling it re-evaluates on every countdown tick.
    function coolMsFor(id) {
        var u = root.coolMap[id];
        if (!u) return 0;
        var rem = u - root.nowTick;
        return rem > 0 ? rem : 0;
    }
    function fmtCooldown(ms) {
        var s = Math.ceil(ms / 1000);
        var mm = Math.floor(s / 60), ss = s % 60;
        return mm + ":" + (ss < 10 ? "0" : "") + ss;
    }
    function clearCool(id) {
        if (root.coolMap[id] === undefined) return;
        var next = {};
        for (var k in root.coolMap) if (k !== id) next[k] = root.coolMap[k];
        root.coolMap = next;   // reassign so bindings wake
    }
    // Fold flat jobs into checkout groups (a season = a view of the queue).
    function groupJobs(list) {
        var groups = [], byKey = {};
        for (var i = 0; i < list.length; i++) {
            var j = list[i];
            var key = (j.world || "") + "|" + (j.groupKey || j.id);
            var g = byKey[key];
            if (!g) {
                g = { key: key, world: j.world || "theatre", rows: [],
                      doneCount: 0, liveCount: 0, received: 0, total: 0,
                      speed: 0, eta: -1 };
                byKey[key] = g;
                groups.push(g);
            }
            g.rows.push(j);
            if (j.state === "done") g.doneCount++;
            else if (isLiveState(j.state)) g.liveCount++;
            if ((j.total || 0) > 0) {
                g.received += (j.received || 0);
                g.total += j.total;
            }
            if (j.state === "downloading") g.speed += (j.speed || 0);
            var eta = (j.etaSec === undefined || j.etaSec === null) ? -1 : j.etaSec;
            if (eta >= 0) g.eta = Math.max(g.eta, eta);
        }
        for (var k = 0; k < groups.length; k++) {
            var g2 = groups[k];
            // m_acq (the manga-volume acquisition map) is a QHash — iteration order is
            // unspecified and can change on rehash. Without this, a batch's expanded fold
            // would visibly reshuffle its members on every refresh tick.
            g2.rows.sort(function(a, b) { return (a.id < b.id) ? -1 : (a.id > b.id ? 1 : 0); });
            g2.count = g2.rows.length;
            g2.single = g2.count === 1;
            g2.hasKnownTotal = g2.total > 0;
            g2.ratio = g2.hasKnownTotal
                    ? Math.max(0, Math.min(1, g2.received / g2.total))
                    : 0;
            var first = g2.rows[0];
            g2.season = first.season || 0;
            g2.seriesTitle = first.seriesTitle || "";
            // groupUnit is a POSITIVE, opt-in gate (not "world === theatre"): only a producer
            // that actually emits it (manga volumes, future multi-part comics) takes this
            // branch, so Theatre's season titling — and any future world that emits neither —
            // falls through unchanged. Hoisted onto the group, not read as first.groupUnit at
            // the use site, so it survives the row sort above.
            g2.groupUnit = first.groupUnit || "";
            var base = g2.seriesTitle || first.title || "Download";
            g2.title = g2.single ? (first.title || "Download")
                     : g2.groupUnit
                         ? base + " — " + g2.count + " " + g2.groupUnit
                         : base + " — Season " + g2.season;
        }
        return groups;
    }
    // deterministic quiet cover tones per title (styling, not data)
    function coverTone(title, dark) {
        var h = 0;
        for (var i = 0; i < title.length; i++) h = ((h << 5) - h + title.charCodeAt(i)) | 0;
        var hue = ((h % 360) + 360) % 360;
        return Qt.hsla(hue / 360, 0.22, dark ? 0.10 : 0.22, 1);
    }

    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: root.downloadsApi
        ignoreUnknownSignals: true
        function onChanged() { root.refresh() }
    }
    // Cooldown feed — `Downloads` (MangaDownloader) is the only backbone that
    // rate-limits with a pause+resume, so its paused() is where the truth lives.
    // Any progress/finish/fail for that chapter means the wall lifted → clear.
    Connections {
        target: typeof Downloads !== "undefined" ? Downloads : null
        function onPaused(chapterId, resumeInMs) {
            var next = {};
            for (var k in root.coolMap) next[k] = root.coolMap[k];
            next[chapterId] = Date.now() + resumeInMs;
            root.coolMap = next;
            root.nowTick = Date.now();
        }
        function onProgress(chapterId, done, total) { root.clearCool(chapterId) }
        function onFinished(chapterId) { root.clearCool(chapterId) }
        function onFailed(chapterId, reason) { root.clearCool(chapterId) }
        function onRemoved(chapterId) { root.clearCool(chapterId) }
    }
    Timer {   // live countdown — only ticks while something is actually cooling
        interval: 1000; repeat: true; running: root.anyCooling
        onTriggered: root.nowTick = Date.now()
    }

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the 899a648 pattern) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 140
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "COLOSSEUM · LOCAL"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Downloads"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: "Everything the house holds — kept locally, ready offline."
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            // one quiet inline metrics line — no stat cards, no pills
            Text {
                topPadding: 16
                textFormat: Text.StyledText
                font.family: theme.ui; font.pixelSize: 13
                color: theme.inkDimmer
                text: {
                    var t = root.totalsMap || {};
                    var parts = [
                        "<b><font color='#f7f7f5'>" + (t.items || 0) + "</font></b> items"
                    ];
                    if (t.bytes) parts.push("<b><font color='#f7f7f5'>" + root.fmtBytes(t.bytes) + "</font></b> on disk");
                    parts.push("Tankoban <font color='#c9c8d0'>" + (t.tankoban || 0) + "</font>");
                    parts.push("Biblio <font color='#c9c8d0'>" + (t.biblio || 0) + "</font>");
                    parts.push("Theatre <font color='#c9c8d0'>" + (t.theatre || 0) + "</font>");
                    parts.push("Audiobooks <font color='#c9c8d0'>" + (t.audiobook || 0) + "</font>");
                    if (t.active) parts.push("<b><font color='#f0c44a'>" + t.active + " arriving</font></b>");
                    if (t.attention) parts.push("<b><font color='#c98b8b'>" + t.attention + " need attention</font></b>");
                    return parts.join("  ·  ");
                }
            }
            Text {
                visible: root.mutationMessage.length > 0
                topPadding: 10
                width: parent.width
                text: root.mutationMessage
                color: "#c98b8b"
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            // ============ NOW ARRIVING — the manager zone ============
            // Ratified: agents/colosseum-downloads-manager-mock.html (2026-07-05).
            // A checkout folds to ONE collapsible group; rows carry their own
            // numbers and exact-row controls. Detail is honest: progress, speed,
            // ETA, size — our files arrive over plain HTTP from our own engine.
            Column {
                width: col.width
                visible: root.jobGroups.length > 0 || Object.keys(root.abActive).length > 0
                topPadding: 40
                spacing: 16

                Row {
                    spacing: 14
                    Text { text: "Now arriving"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                    Text { anchors.baseline: parent.children[0].baseline
                           text: root.liveJobCount + (root.liveJobCount === 1 ? " live job" : " live jobs")
                                 + (root.attentionCount > 0 ? " · " + root.attentionCount + " need attention" : "")
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                }

                Rectangle {
                    width: col.width
                    implicitHeight: groupsCol.implicitHeight
                    radius: 18
                    color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                    border.width: 1; border.color: theme.edge
                    clip: true

                    Column {
                        id: groupsCol
                        width: parent.width

                        Repeater {
                            model: root.jobGroups
                            delegate: Column {
                                id: grp
                                required property var modelData
                                required property int index
                                readonly property bool open: root.openGroups[grp.modelData.key] === true
                                width: groupsCol.width

                                // ---- group header ----
                                Item {
                                    width: parent.width
                                    height: 76

                                    Rectangle { // hairline between groups
                                        visible: grp.index > 0
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.top: parent.top
                                        height: 1; color: Qt.rgba(1, 1, 1, 0.06)
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: !grp.modelData.single
                                        cursorShape: grp.modelData.single ? Qt.ArrowCursor : Qt.PointingHandCursor
                                        onClicked: root.toggleGroup(grp.modelData.key)
                                    }
                                    Text { // chevron (fold handle) — multi-row groups only
                                        visible: !grp.modelData.single
                                        x: 26; anchors.verticalCenter: parent.verticalCenter
                                        text: "›"; color: theme.inkDimmer; font.pixelSize: 18
                                        rotation: grp.open ? 90 : 0
                                        Behavior on rotation { NumberAnimation { duration: 120 } }
                                    }
                                    Column {
                                        anchors.left: parent.left; anchors.leftMargin: 52
                                        anchors.right: numsT.left; anchors.rightMargin: 16
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 4
                                        Text {
                                            width: parent.width
                                            textFormat: Text.PlainText
                                            text: grp.modelData.title
                                            color: theme.ink; font.family: theme.ui
                                            font.pixelSize: 15; font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            textFormat: Text.PlainText
                                            text: {
                                                var w = grp.modelData.world;
                                                var wn = w === "tankoban" ? "Tankoban" : w === "biblio" ? "Biblio" : "Theatre";
                                                if (grp.modelData.single) {
                                                    var r0 = grp.modelData.rows[0];
                                                    if (r0.state === "failed")
                                                        return wn + " · " + (r0.error || "download failed");
                                                    if (r0.state === "queued")
                                                        return wn + " · queued — waits its turn";
                                                    if (r0.state === "paused")
                                                        return wn + " · paused — holds its place";
                                                    if (r0.state === "resolving")
                                                        return wn + " · resolving — finding the best stream";
                                                    if (r0.state === "extracting")
                                                        return wn + " · unpacking";
                                                    if (r0.state === "done")
                                                        return wn + " · landed";
                                                    var d = r0.detail || "";
                                                    var base = d.length ? (wn + " · " + d) : wn;
                                                    // honest cooldown: a soft-blocked source parked
                                                    // this job's page download; show the live resume
                                                    // countdown so it never reads as stuck at "0 of 58 pages".
                                                    var cd = root.coolMsFor(r0.id);
                                                    if (cd > 0)
                                                        base += " · source cooling down — resumes in "
                                                                + root.fmtCooldown(cd);
                                                    return base;
                                                }
                                                // Same positive opt-in gate as the group title: only a
                                                // producer that emits groupUnit (manga volumes, future
                                                // multi-part comics) takes this line; Theatre falls through
                                                // to its unchanged "season checkout" subtitle.
                                                return grp.modelData.groupUnit
                                                    ? wn + " · " + grp.modelData.count + " " + grp.modelData.groupUnit
                                                    : wn + " · season checkout · " + grp.modelData.count + " episodes";
                                            }
                                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Text {
                                        id: numsT
                                        anchors.right: actsRow.left; anchors.rightMargin: 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: Text.AlignRight
                                        textFormat: Text.StyledText
                                        text: {
                                            var parts = [];
                                            if (!grp.modelData.single)
                                                parts.push("<b><font color='#f7f7f5'>" + grp.modelData.doneCount
                                                           + " of " + grp.modelData.count + "</font></b> landed");
                                            if (grp.modelData.total > 0)
                                                parts.push(root.fmtBytes(grp.modelData.received)
                                                           + " of " + root.fmtBytes(grp.modelData.total));
                                            if (grp.modelData.speed > 0) {
                                                var hsp = root.fmtSpeed(grp.modelData.speed);
                                                if (hsp.length) parts.push("<font color='#f0c44a'><b>" + hsp + "</b></font>");
                                            }
                                            var eta = root.fmtEta(grp.modelData.eta);
                                            if (eta.length) parts.push(eta);
                                            return parts.join(" · ");
                                        }
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                    }
                                    Row {
                                        id: actsRow
                                        anchors.right: parent.right; anchors.rightMargin: 26
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 20
                                        Text {
                                            // play-while-arriving (2026-07-20): a LIVE job can be
                                            // watched now — only once it's actually downloading
                                            // with a resolved url (never queued/resolving).
                                            visible: grp.modelData.single
                                                     && grp.modelData.rows[0].canPlay === true
                                            text: "Play"
                                            color: hPlayMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                            MouseArea { id: hPlayMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.playArrivingRequested(grp.modelData.rows[0]) }
                                        }
                                        Text {
                                            visible: grp.modelData.single
                                                     && grp.modelData.rows[0].canRetry === true
                                            text: "Retry"
                                            color: hRetryMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                            MouseArea { id: hRetryMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.downloadsApi.retry(grp.modelData.world, grp.modelData.rows[0].id) }
                                        }
                                        Text {
                                            id: hPauseT
                                            readonly property bool anyRunning: {
                                                var rows = grp.modelData.rows;
                                                for (var i = 0; i < rows.length; i++)
                                                    if (rows[i].canPause === true) return true;
                                                return false;
                                            }
                                            readonly property bool anyPaused: {
                                                var rows = grp.modelData.rows;
                                                for (var i = 0; i < rows.length; i++)
                                                    if (rows[i].canResume === true) return true;
                                                return false;
                                            }
                                            visible: anyRunning || anyPaused
                                            text: anyRunning
                                                  ? (grp.modelData.single ? "Pause" : "Pause season")
                                                  : (grp.modelData.single ? "Resume" : "Resume season")
                                            color: hPauseMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                            MouseArea { id: hPauseMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            var rows = grp.modelData.rows;
                                                            var pausing = hPauseT.anyRunning;
                                                            // pause walks BACKWARD (pump can't promote a row the
                                                            // loop is about to pause); resume walks FORWARD (the
                                                            // earliest episode — holding the half-downloaded .part
                                                            // and the live session url — takes the slot first).
                                                            for (var k = 0; k < rows.length; k++) {
                                                                var i = pausing ? rows.length - 1 - k : k;
                                                                if (pausing && rows[i].canPause === true)
                                                                    root.downloadsApi.pause(grp.modelData.world, rows[i].id);
                                                                else if (!pausing && rows[i].canResume === true)
                                                                    root.downloadsApi.resume(grp.modelData.world, rows[i].id);
                                                            }
                                                        } }
                                        }
                                        Text {
                                            visible: grp.modelData.single
                                                     && grp.modelData.rows[0].state === "failed"
                                                     && grp.modelData.rows[0].canRetry !== true
                                            text: grp.modelData.world === "biblio" ? "Open Biblio" : "Open Tankoban"
                                            color: hSourceMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                            MouseArea { id: hSourceMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.openWorldRequested(grp.modelData.world) }
                                        }
                                        Text {
                                            readonly property bool anyCancelable: {
                                                var rows = grp.modelData.rows;
                                                for (var i = 0; i < rows.length; i++)
                                                    if (rows[i].canCancel === true) return true;
                                                return false;
                                            }
                                            visible: anyCancelable
                                                     || (grp.modelData.single
                                                         && grp.modelData.rows[0].canDismiss === true)
                                            text: grp.modelData.single && grp.modelData.rows[0].canDismiss === true
                                                  ? "Dismiss"
                                                  : (grp.modelData.single ? "Cancel" : "Cancel season")
                                            color: hCancelMa.containsMouse ? theme.ink : theme.inkDimmer
                                            font.family: theme.ui; font.pixelSize: 12
                                            MouseArea { id: hCancelMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            var rows = grp.modelData.rows;
                                                            if (grp.modelData.single && rows[0].canDismiss === true) {
                                                                root.downloadsApi.dismissFailure(grp.modelData.world, rows[0].id);
                                                                return;
                                                            }
                                                            // grp is a Repeater delegate; jobGroups is reassigned wholesale on
                                                            // every refresh() (a plain-array Repeater diffs nothing), so grp
                                                            // can be torn down before this dialog's callback fires (2026-08-05,
                                                            // the Chew silent-cancel bug — a resolve mid-dialog killed `grp` and
                                                            // threw, aborting the cancel with no error and, then, no log line).
                                                            // Capture only the group's identity now; re-resolve its CURRENT
                                                            // rows at commit time from root.jobGroups (a page-level property
                                                            // that outlives any delegate) so a group that grew between click
                                                            // and confirm — exactly what "resolving" -> 53 links does — still
                                                            // cancels everything it now contains, not a stale snapshot.
                                                            var world = grp.modelData.world;
                                                            var key = grp.modelData.key;
                                                            root.confirmAction(
                                                                grp.modelData.single ? "Cancel download?" : "Cancel season?",
                                                                "Partial files will be deleted.",
                                                                "Cancel download",
                                                                function() {
                                                                    for (var g = 0; g < root.jobGroups.length; g++) {
                                                                        if (root.jobGroups[g].key !== key) continue;
                                                                        var curRows = root.jobGroups[g].rows;
                                                                        for (var i = curRows.length - 1; i >= 0; i--)
                                                                            if (curRows[i].canCancel === true)
                                                                                root.downloadsApi.cancel(world, curRows[i].id);
                                                                        return;
                                                                    }
                                                                });
                                                        } }
                                        }
                                    }
                                    Rectangle { // aggregate: gold lives on the bottom edge
                                        anchors.left: parent.left; anchors.bottom: parent.bottom
                                        width: parent.width * grp.modelData.ratio
                                        height: 3; color: theme.gold
                                        visible: grp.modelData.liveCount > 0 && grp.modelData.hasKnownTotal
                                    }
                                }

                                // ---- episode rows (fold) ----
                                Repeater {
                                    model: (grp.open && !grp.modelData.single) ? grp.modelData.rows : []
                                    delegate: Item {
                                        id: epRow
                                        required property var modelData
                                        width: grp.width
                                        height: 54

                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.top: parent.top
                                            height: 1; color: Qt.rgba(1, 1, 1, 0.06)
                                        }
                                        Text {
                                            id: epNoT
                                            x: 52; anchors.verticalCenter: parent.verticalCenter
                                            width: 60
                                            // badge is a positive opt-in gate: manga volumes emit
                                            // "Vol. N" (MangaTankobanService's own label, passed through
                                            // unchanged); Theatre never emits it and keeps its "E01" text;
                                            // a future producer with neither renders an honest blank gutter
                                            // instead of a lie.
                                            text: epRow.modelData.badge
                                                  ? epRow.modelData.badge
                                                  : (epRow.modelData.episode > 0
                                                     ? "E" + (epRow.modelData.episode < 10 ? "0" : "") + epRow.modelData.episode
                                                     : "")
                                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                        Column {
                                            anchors.left: epNoT.right; anchors.leftMargin: 8
                                            anchors.right: epNums.left; anchors.rightMargin: 16
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            Text {
                                                width: parent.width
                                                textFormat: Text.PlainText
                                                text: epRow.modelData.title || "Episode"
                                                color: epRow.modelData.state === "done" || epRow.modelData.state === "queued"
                                                       || epRow.modelData.state === "paused"
                                                       ? theme.inkDim : theme.ink
                                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.Medium
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                width: parent.width
                                                textFormat: Text.PlainText
                                                text: epRow.modelData.state === "downloading" ? "downloading"
                                                    : epRow.modelData.state === "resolving" ? "resolving — finding the best stream"
                                                    : epRow.modelData.state === "queued" ? "queued — waits its turn"
                                                    : epRow.modelData.state === "paused" ? "paused — holds its place"
                                                    : epRow.modelData.state === "failed"
                                                          ? "failed — " + (epRow.modelData.error || "download failed")
                                                    : "landed — on the Theatre shelf"
                                                color: epRow.modelData.state === "failed"
                                                       ? "#c98b8b" : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 11
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Text {
                                            id: epNums
                                            anchors.right: epActs.left; anchors.rightMargin: 20
                                            anchors.verticalCenter: parent.verticalCenter
                                            textFormat: Text.StyledText
                                            text: {
                                                var m = epRow.modelData;
                                                if (m.state === "downloading") {
                                                    var parts = [];
                                                    var sp = root.fmtSpeed(m.speed || 0);
                                                    if (sp.length) parts.push("<font color='#f0c44a'><b>" + sp + "</b></font>");
                                                    if (m.total > 0) {
                                                        parts.push(Math.round((m.ratio || 0) * 100) + "%");
                                                        parts.push(root.fmtBytes(m.received || 0) + " of " + root.fmtBytes(m.total));
                                                    }
                                                    return parts.join(" · ");
                                                }
                                                if (m.state === "done" && m.total > 0) return root.fmtBytes(m.total);
                                                if (m.state === "paused" && m.total > 0)
                                                    return root.fmtBytes(m.received || 0) + " of " + root.fmtBytes(m.total);
                                                return "—";
                                            }
                                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                        }
                                        Row {
                                            id: epActs
                                            anchors.right: parent.right; anchors.rightMargin: 26
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 16
                                            Text {
                                                visible: epRow.modelData.state === "done"
                                                text: "✓"; color: theme.inkDim
                                                font.family: theme.ui; font.pixelSize: 13
                                            }
                                            Text {
                                                // play-while-arriving (2026-07-20): same gate as the
                                                // single card — downloading + resolved url only.
                                                visible: epRow.modelData.canPlay === true
                                                text: "Play"
                                                color: rPlayMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                                MouseArea { id: rPlayMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.playArrivingRequested(epRow.modelData) }
                                            }
                                            Text {
                                                visible: epRow.modelData.canPause === true
                                                         || epRow.modelData.canResume === true
                                                text: epRow.modelData.canResume === true ? "Resume" : "Pause"
                                                color: rPauseMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                                MouseArea { id: rPauseMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: epRow.modelData.canResume === true
                                                                       ? root.downloadsApi.resume(epRow.modelData.world, epRow.modelData.id)
                                                                       : root.downloadsApi.pause(epRow.modelData.world, epRow.modelData.id) }
                                            }
                                            Text {
                                                visible: epRow.modelData.canRetry === true
                                                text: "Retry"
                                                color: rRetryMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                                MouseArea { id: rRetryMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.downloadsApi.retry(epRow.modelData.world, epRow.modelData.id) }
                                            }
                                            Text {
                                                visible: epRow.modelData.canCancel === true
                                                         || epRow.modelData.canDismiss === true
                                                text: epRow.modelData.canDismiss === true ? "Dismiss" : "Cancel"
                                                color: rCancelMa.containsMouse ? theme.ink : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 12
                                                MouseArea { id: rCancelMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: {
                                                                if (epRow.modelData.canDismiss === true) {
                                                                    root.downloadsApi.dismissFailure(epRow.modelData.world, epRow.modelData.id);
                                                                    return;
                                                                }
                                                                // Same delegate-teardown hazard as the group cancel above:
                                                                // capture the primitives before the async dialog, never the
                                                                // delegate id itself.
                                                                var epWorld = epRow.modelData.world;
                                                                var epId = epRow.modelData.id;
                                                                root.confirmAction(
                                                                    "Cancel download?",
                                                                    "The partial file will be deleted.",
                                                                    "Cancel download",
                                                                    function() {
                                                                        root.downloadsApi.cancel(epWorld, epId);
                                                                    });
                                                            } }
                                            }
                                        }
                                        Rectangle { // per-row progress on the bottom edge
                                            anchors.left: parent.left; anchors.bottom: parent.bottom
                                            anchors.leftMargin: 52
                                            width: (parent.width - 78) * (epRow.modelData.ratio || 0)
                                            height: 2; color: theme.gold
                                            visible: epRow.modelData.state === "downloading"
                                                     && (epRow.modelData.total || 0) > 0
                                        }
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: Object.keys(root.abActive)
                            delegate: Item {
                                id: managerAudioRow
                                required property var modelData
                                readonly property var st: root.abActive[modelData] || ({})
                                width: groupsCol.width
                                height: 58

                                Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 1; color: Qt.rgba(1, 1, 1, 0.06)
                                }
                                Column {
                                    anchors.left: parent.left; anchors.leftMargin: 52
                                    anchors.right: managerAudioAction.left; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    Text {
                                        width: parent.width
                                        text: managerAudioRow.st.title || root.abTitleOf(managerAudioRow.modelData)
                                        color: theme.ink
                                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: {
                                            if (managerAudioRow.st.state === "failed")
                                                return "Audiobook · failed — " + (managerAudioRow.st.error || "download failed");
                                            if (managerAudioRow.st.state === "resolving")
                                                return "Audiobook · resolving";
                                            if (managerAudioRow.st.total > 0)
                                                return "Audiobook · " + root.fmtBytes(managerAudioRow.st.received)
                                                        + " of " + root.fmtBytes(managerAudioRow.st.total);
                                            return "Audiobook · downloading";
                                        }
                                        color: managerAudioRow.st.state === "failed" ? "#c98b8b" : theme.inkDimmer
                                        font.family: theme.ui; font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    id: managerAudioAction
                                    anchors.right: parent.right; anchors.rightMargin: 26
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: managerAudioRow.st.state === "failed" ? "Dismiss" : "Cancel"
                                    color: managerAudioMa.containsMouse ? theme.ink : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 12
                                    MouseArea {
                                        id: managerAudioMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (managerAudioRow.st.state === "failed") {
                                                root.audiobooksApi.dismissFailure(managerAudioRow.modelData);
                                                root.abRefresh();
                                                return;
                                            }
                                            // abActive is reassigned wholesale on every event (Object.assign
                                            // builds a fresh object each time), so Object.keys(root.abActive)
                                            // is a new array reference on every refresh and this Repeater's
                                            // delegates are torn down and rebuilt just like jobGroups' above.
                                            // Capture the pairKey primitive before the async dialog.
                                            var abKey = managerAudioRow.modelData;
                                            root.confirmAction(
                                                "Cancel audiobook download?",
                                                "The partial files will be deleted.",
                                                "Cancel download",
                                                function() {
                                                    root.audiobooksApi.cancelDownload(abKey);
                                                });
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ============ BACKGROUND ACTIVITY ============
            // Unified rows for offline-analysis jobs (guided comic panel
            // detection, audiobook text sync). Publishes via the BackgroundActivity
            // context property; vanishes entirely when nothing is running. The
            // typeof guard keeps DownloadsPage load-harnesses happy without the
            // context property alive.
            BackgroundActivitySection {
                width: col.width
                topPadding: 40
                registry: (typeof BackgroundActivity !== "undefined") ? BackgroundActivity : null
            }

            // ============ WORLD SHELVES ============
            Repeater {
                model: root.worlds
                delegate: Column {
                    id: lane
                    required property var modelData
                    readonly property var laneList: root.laneSeries[lane.modelData.key] || []
                    readonly property bool ledgerHere: root.openLedgerWorld === lane.modelData.key
                                                       && root.openLedgerKey.length > 0
                    width: col.width
                    topPadding: 48
                    spacing: 16

                    Row {
                        spacing: 14
                        Text { text: lane.modelData.title; color: theme.ink
                               font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            textFormat: Text.StyledText
                            text: {
                                var n = 0, bytes = 0;
                                for (var i = 0; i < lane.laneList.length; i++) {
                                    n += lane.laneList[i].itemCount;
                                    bytes += lane.laneList[i].bytes;
                                }
                                var s = "<b><font color='#f7f7f5'>" + n + "</font></b> " + lane.modelData.unit;
                                if (bytes) s += " · " + root.fmtBytes(bytes);
                                return s;
                            }
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }

                    Rectangle {
                        width: lane.width
                        implicitHeight: shelfCol.implicitHeight + 52
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge

                        Column {
                            id: shelfCol
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 26
                            spacing: 0

                            // honest empty lane
                            Column {
                                visible: lane.laneList.length === 0
                                spacing: 12
                                Text {
                                    text: "Nothing from " + lane.modelData.title + " lives here yet."
                                    color: theme.inkDim
                                    font.family: theme.display; font.italic: true; font.pixelSize: 19
                                }
                                Text {
                                    text: "Open " + lane.modelData.title + " and pick something ›"
                                    color: goMa.containsMouse ? "#ffd968" : theme.gold
                                    font.family: theme.ui; font.pixelSize: 14
                                    MouseArea { id: goMa; anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.openWorldRequested(lane.modelData.key) }
                                }
                            }

                            // series rail
                            Flickable {
                                visible: lane.laneList.length > 0
                                width: shelfCol.width
                                height: 214
                                contentWidth: railRow.width
                                contentHeight: height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick
                                boundsBehavior: Flickable.StopAtBounds
                                Row {
                                    id: railRow
                                    spacing: 16
                                    Repeater {
                                        model: lane.laneList
                                        delegate: Item {
                                            id: card
                                            required property var modelData
                                            readonly property bool on: root.openLedgerWorld === lane.modelData.key
                                                                       && root.openLedgerKey === card.modelData.key
                                            width: 148; height: 214

                                            Rectangle {
                                                id: cover
                                                width: 148; height: 198
                                                radius: 12
                                                border.width: card.on ? 2 : 1
                                                border.color: card.on ? Qt.rgba(0.94, 0.77, 0.29, 0.65) : theme.edge
                                                gradient: Gradient {
                                                    GradientStop { position: 0; color: root.coverTone(card.modelData.title || "", false) }
                                                    GradientStop { position: 1; color: root.coverTone(card.modelData.title || "", true) }
                                                }
                                                Image {
                                                    anchors.fill: parent
                                                    visible: (card.modelData.art || "").length > 0
                                                    source: card.modelData.art || ""
                                                    fillMode: Image.PreserveAspectCrop
                                                    opacity: status === Image.Ready ? 1 : 0
                                                }
                                                Rectangle { // readability foot
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    height: parent.height * 0.55
                                                    radius: 12
                                                    gradient: Gradient {
                                                        GradientStop { position: 0; color: "transparent" }
                                                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.78) }
                                                    }
                                                }
                                                Text {
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.leftMargin: 12; anchors.rightMargin: 10; anchors.bottomMargin: 30
                                                    text: card.modelData.title || "Untitled"
                                                    color: theme.ink; font.family: theme.ui
                                                    font.pixelSize: 14; font.weight: Font.DemiBold
                                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                }
                                                Text {
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.leftMargin: 12; anchors.rightMargin: 10; anchors.bottomMargin: 10
                                                    textFormat: Text.StyledText
                                                    text: "<b><font color='#f7f7f5'>" + card.modelData.itemCount + "</font></b> "
                                                          + (card.modelData.kind === "book" ? "edition"
                                                             + (card.modelData.itemCount === 1 ? "" : "s")
                                                             : card.modelData.kind === "comic" ? "issues · western"
                                                             : card.modelData.kind === "manga" ? "chapters · manga"
                                                             : card.modelData.kind === "episode" ? "episodes"
                                                             : "film")
                                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.toggleLedger(lane.modelData.key, card.modelData.key)
                                            }
                                        }
                                    }
                                }
                            }

                            // expanded item ledger
                            Column {
                                visible: lane.ledgerHere
                                width: shelfCol.width
                                topPadding: 18
                                spacing: 0

                                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.10) }

                                // season folds (theatre episode ledgers only)
                                Repeater {
                                    model: lane.ledgerHere ? root.ledgerSeasonList : []
                                    delegate: Column {
                                        id: sgrp
                                        required property var modelData
                                        readonly property bool sOpen: root.openSeasons[sgrp.modelData.season] === true
                                        width: shelfCol.width

                                        Item {
                                            width: parent.width; height: 46
                                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.toggleSeason(sgrp.modelData.season) }
                                            Text {
                                                id: sChev
                                                x: 4; anchors.verticalCenter: parent.verticalCenter
                                                text: "›"; color: theme.inkDimmer; font.pixelSize: 15
                                                rotation: sgrp.sOpen ? 90 : 0
                                                Behavior on rotation { NumberAnimation { duration: 120 } }
                                            }
                                            Text {
                                                anchors.left: sChev.right; anchors.leftMargin: 12
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "SEASON " + sgrp.modelData.season
                                                color: theme.ink; font.family: theme.ui
                                                font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.6
                                            }
                                            Text {
                                                anchors.right: parent.right; anchors.rightMargin: 4
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: {
                                                    var s = sgrp.modelData.items.length + " episode"
                                                            + (sgrp.modelData.items.length === 1 ? "" : "s");
                                                    var b = root.fmtBytes(sgrp.modelData.bytes);
                                                    if (b.length) s += " · " + b;
                                                    if (sgrp.modelData.arriving > 0)
                                                        s += " · " + sgrp.modelData.arriving + " still arriving above";
                                                    return s;
                                                }
                                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                            }
                                            Rectangle {
                                                anchors.left: parent.left; anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                height: 1; color: Qt.rgba(1, 1, 1, 0.06)
                                            }
                                        }

                                        Repeater {
                                            model: sgrp.sOpen ? sgrp.modelData.items : []
                                            delegate: LedgerRow {
                                                required property var modelData
                                                rowData: modelData; indent: 26
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: (lane.ledgerHere && root.ledgerSeasonList.length === 0)
                                           ? root.ledgerItems : []
                                    delegate: LedgerRow {
                                        required property var modelData
                                        rowData: modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // ============ AUDIOBOOKS SHELF (A2 lane — its own engine, not LocalDownloads) ============
            Column {
                id: abLane
                width: col.width
                visible: root.abDone.length > 0 || Object.keys(root.abActive).length === 0
                topPadding: 48
                spacing: 16
                readonly property var abKeys: Object.keys(root.abActive)

                Row {
                    spacing: 14
                    Text { text: "Audiobooks"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                    Text {
                        anchors.baseline: parent.children[0].baseline
                        textFormat: Text.StyledText
                        text: {
                            var bytes = 0;
                            for (var i = 0; i < root.abDone.length; i++) bytes += (root.abDone[i].bytes || 0);
                            var s = "<b><font color='#f7f7f5'>" + root.abDone.length + "</font></b> audiobooks";
                            if (bytes) s += " · " + root.fmtBytes(bytes);
                            return s;
                        }
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    }
                }

                Rectangle {
                    width: parent.width
                    implicitHeight: abShelfCol.implicitHeight + 52
                    radius: 18
                    color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                    border.width: 1; border.color: theme.edge

                    Column {
                        id: abShelfCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 26
                        spacing: 0

                        // honest empty shelf
                        Column {
                            visible: abLane.abKeys.length === 0 && root.abDone.length === 0
                            spacing: 12
                            Text {
                                text: "No audiobooks live here yet."
                                color: theme.inkDim
                                font.family: theme.display; font.italic: true; font.pixelSize: 19
                            }
                            Text {
                                text: "Open a book in Biblio and pick a release ›"
                                color: emptyAudioMa.containsMouse ? "#ffd968" : theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 14
                                MouseArea {
                                    id: emptyAudioMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openWorldRequested("audiobook")
                                }
                            }
                        }

                        // completed sets
                        Repeater {
                            model: root.abDone
                            delegate: Item {
                                id: abDoneRow
                                required property var modelData
                                width: abShelfCol.width; height: 54
                                Text {
                                    id: abMark
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    width: 22
                                    text: abDoneRow.modelData.missing ? "✕" : "✓"
                                    color: abDoneRow.modelData.missing ? theme.inkDimmer : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13
                                }
                                Column {
                                    anchors.left: abMark.right; anchors.leftMargin: 14
                                    anchors.right: abDoneActions.left; anchors.rightMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    Text {
                                        width: parent.width
                                        text: abDoneRow.modelData.title || root.abTitleOf(abDoneRow.modelData.id)
                                        color: abDoneRow.modelData.missing ? theme.inkDim : theme.ink
                                        font.family: theme.ui; font.pixelSize: 15; font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: {
                                            if (abDoneRow.modelData.missing)
                                                return "the files left the disk outside the app — delete the entry or fetch it again";
                                            var parts = [];
                                            if (abDoneRow.modelData.author) parts.push(abDoneRow.modelData.author);
                                            var b = root.fmtBytes(abDoneRow.modelData.bytes || 0);
                                            if (b) parts.push(b);
                                            var w = root.fmtWhen(abDoneRow.modelData.addedAt || 0);
                                            if (w) parts.push(w);
                                            if (abDoneRow.modelData.bookPath)
                                                parts.push("ready to listen");
                                            else
                                                parts.push("the paired book is not available locally");
                                            return parts.join(" · ");
                                        }
                                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                                Row {
                                    id: abDoneActions
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    spacing: 20
                                    Text {
                                        visible: !abDoneRow.modelData.missing
                                                 && String(abDoneRow.modelData.bookPath || "").length > 0
                                        text: "Open book"
                                        color: abOpenMa.containsMouse ? "#ffd968" : theme.gold
                                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                                        MouseArea {
                                            id: abOpenMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openAudiobookRequested(abDoneRow.modelData)
                                        }
                                    }
                                    Text {
                                        text: "Delete local copy"
                                        color: abDelMa.containsMouse ? theme.ink : theme.inkDimmer
                                        font.family: theme.ui; font.pixelSize: 13
                                        MouseArea {
                                            id: abDelMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                // Same delegate-teardown hazard: capture the id primitive before
                                                // the async dialog, not the delegate id itself.
                                                var abDoneId = abDoneRow.modelData.id;
                                                root.confirmAction(
                                                    "Delete local audiobook?",
                                                    "The downloaded audiobook files will be deleted.",
                                                    "Delete local copy",
                                                    function() {
                                                        var result = root.audiobooksApi.deleteAudiobook(abDoneId);
                                                        root.finishMutation(result, "The audiobook could not be deleted.");
                                                    });
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 40 }
        }
    }

    ScrollGlide { flick: page }

    Rectangle {
        anchors.fill: parent
        visible: root.confirmationOpen
        z: 100
        color: Qt.rgba(0, 0, 0, 0.72)
        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(480, root.width - 48)
            height: confirmContent.implicitHeight + 48
            radius: 16
            color: "#171820"
            border.width: 1
            border.color: theme.edge

            Column {
                id: confirmContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 24
                spacing: 14

                Text {
                    width: parent.width
                    text: root.confirmationTitle
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 22
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: root.confirmationBody
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
                Row {
                    anchors.right: parent.right
                    spacing: 10
                    Rectangle {
                        width: cancelConfirmText.implicitWidth + 28
                        height: 34
                        radius: 17
                        color: cancelConfirmMa.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07)
                        Text {
                            id: cancelConfirmText
                            anchors.centerIn: parent
                            text: "Go back"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: cancelConfirmMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeConfirmation()
                        }
                    }
                    Rectangle {
                        width: confirmActionText.implicitWidth + 28
                        height: 34
                        radius: 17
                        color: confirmActionMa.containsMouse ? "#ffd968" : theme.gold
                        Text {
                            id: confirmActionText
                            anchors.centerIn: parent
                            text: root.confirmationLabel
                            color: "#111217"
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: confirmActionMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.runConfirmedAction()
                        }
                    }
                }
            }
        }
    }

    // ---- fixed back / system controls (mirrors GenrePage) ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.40)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }

    component LedgerRow: Item {
        id: row
        property var rowData: null
        property int indent: 0
        width: parent ? parent.width : 0
        height: 58

        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1; color: Qt.rgba(1, 1, 1, 0.06)
        }
        Text {
            id: markT
            x: row.indent
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            text: row.rowData.missing ? "✕" : "✓"
            color: row.rowData.missing ? theme.inkDimmer : theme.inkDim
            font.family: theme.ui; font.pixelSize: 13
        }
        Column {
            anchors.left: markT.right; anchors.leftMargin: 14
            anchors.right: actRow.left; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Text {
                width: parent.width
                text: row.rowData.title || "Untitled"
                color: row.rowData.missing ? theme.inkDim : theme.ink
                font.family: theme.ui; font.pixelSize: 15; font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: {
                    if (row.rowData.missing)
                        return "the file left the disk outside the app — dismiss the entry or fetch it again";
                    var parts = [];
                    if (row.rowData.subtitle) parts.push(row.rowData.subtitle);
                    var b = root.fmtBytes(row.rowData.bytes || 0);
                    if (b) parts.push(b);
                    var w = root.fmtWhen(row.rowData.addedAt || 0);
                    if (w) parts.push(w);
                    return parts.join(" · ");
                }
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
        Row {
            id: actRow
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            Text {
                visible: !row.rowData.missing
                text: row.rowData.world === "theatre" ? "Play" : "Read"
                color: openMa.containsMouse ? "#ffd968" : theme.gold
                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                MouseArea { id: openMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openRequested(row.rowData) }
            }
            Text {
                text: row.rowData.missing ? "Dismiss missing entry" : "Delete local copy"
                color: rmMa.containsMouse ? theme.ink : theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 13
                MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var world = row.rowData.world;
                                var id = row.rowData.id;
                                root.confirmAction(
                                    row.rowData.missing ? "Dismiss missing entry?" : "Delete local copy?",
                                    row.rowData.missing
                                        ? "This removes the stale Downloads entry."
                                        : "The downloaded file will be deleted from this device.",
                                    row.rowData.missing ? "Dismiss entry" : "Delete local copy",
                                    function() {
                                        var result = root.downloadsApi.remove(world, id);
                                        root.finishMutation(result, "The local copy could not be deleted.");
                                    });
                            } }
            }
        }
    }
}
