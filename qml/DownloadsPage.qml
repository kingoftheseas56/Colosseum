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
    signal closeRequested()
    signal searchClicked()
    signal openRequested(var item)           // completed row → host routes by world/kind
    signal openWorldRequested(string world)  // empty-lane CTA → host opens that world

    Theme { id: theme }

    // ---- read-model bindings (revision-driven refresh) ----
    property var jobs: []
    property var jobGroups: []
    property int liveJobCount: 0
    property var openGroups: ({})
    property var laneSeries: ({})      // world -> series list
    property var totalsMap: ({})
    property string openLedgerWorld: ""
    property string openLedgerKey: ""
    property var ledgerItems: []

    // ---- source-cooldown visibility (Task 11) ----
    // MangaDownloader (exposed to QML as `Downloads`) emits paused(chapterId,
    // resumeInMs) each time an xoxo soft-block parks a page-image download for
    // 120s. Nothing consumed it, so a cooling job read as stuck ("0 of 58 pages").
    // We hold resume-at wall-clock per chapterId and surface an honest live
    // countdown on its "Now arriving" row, cleared on the next progress/finish/fail.
    property var coolMap: ({})              // chapterId -> resume-at epoch ms (absent = not cooling)
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
        if (typeof LocalDownloads === "undefined") return;
        jobs = LocalDownloads.activeJobs();
        jobGroups = groupJobs(jobs);
        var live = 0;
        for (var k = 0; k < jobs.length; k++)
            if (jobs[k].state !== "done") live++;
        liveJobCount = live;
        // drop cooldowns whose job left the queue without a terminal signal
        // (e.g. cancelled) so a stale countdown never lingers.
        var present = {};
        for (var c = 0; c < jobs.length; c++) present[jobs[c].id] = true;
        var pruned = {}, dropped = false;
        for (var ck in coolMap) { if (present[ck]) pruned[ck] = coolMap[ck]; else dropped = true; }
        if (dropped) coolMap = pruned;
        totalsMap = LocalDownloads.totals;
        var lanes = {};
        for (var i = 0; i < worlds.length; i++)
            lanes[worlds[i].key] = LocalDownloads.series(worlds[i].key);
        laneSeries = lanes;
        if (openLedgerKey.length) {
            ledgerItems = LocalDownloads.items(openLedgerWorld, openLedgerKey);
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
        ledgerItems = (typeof LocalDownloads !== "undefined")
                      ? LocalDownloads.items(world, key) : [];
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
                      speed: 0, eta: -1, ratioSum: 0 };
                byKey[key] = g;
                groups.push(g);
            }
            g.rows.push(j);
            if (j.state === "done") { g.doneCount++; g.ratioSum += 1; }
            else { g.liveCount++; g.ratioSum += (j.ratio || 0); }
            g.received += (j.received || 0);
            g.total += (j.total || 0);
            if (j.state === "downloading") g.speed += (j.speed || 0);
            var eta = (j.etaSec === undefined || j.etaSec === null) ? -1 : j.etaSec;
            if (eta >= 0) g.eta = Math.max(g.eta, eta);
        }
        for (var k = 0; k < groups.length; k++) {
            var g2 = groups[k];
            g2.count = g2.rows.length;
            g2.single = g2.count === 1;
            g2.ratio = g2.count > 0 ? g2.ratioSum / g2.count : 0;
            var first = g2.rows[0];
            g2.season = first.season || 0;
            g2.seriesTitle = first.seriesTitle || "";
            g2.title = g2.single ? (first.title || "Download")
                     : (g2.seriesTitle || first.title || "Download")
                       + " — Season " + g2.season;
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
        target: typeof LocalDownloads !== "undefined" ? LocalDownloads : null
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
                    if (t.active) parts.push("<b><font color='#f0c44a'>" + t.active + " arriving</font></b>");
                    return parts.join("  ·  ");
                }
            }

            // ============ NOW ARRIVING — the manager zone ============
            // Ratified: agents/colosseum-downloads-manager-mock.html (2026-07-05).
            // A checkout folds to ONE collapsible group; rows carry their own
            // numbers and exact-row controls. Detail is honest: progress, speed,
            // ETA, size — our files arrive over plain HTTP from our own engine.
            Column {
                width: col.width
                visible: root.jobGroups.length > 0
                topPadding: 40
                spacing: 16

                Row {
                    spacing: 14
                    Text { text: "Now arriving"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28; font.letterSpacing: -0.2 }
                    Text { anchors.baseline: parent.children[0].baseline
                           text: root.liveJobCount + (root.liveJobCount === 1 ? " live job" : " live jobs")
                                 + " — this zone leaves when the last one lands"
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
                                                    // honest cooldown: xoxo parked this job's page
                                                    // download; show the live resume countdown so it
                                                    // never reads as stuck at "0 of 58 pages".
                                                    var cd = root.coolMsFor(r0.id);
                                                    if (cd > 0)
                                                        base += " · source cooling down — resumes in "
                                                                + root.fmtCooldown(cd);
                                                    return base;
                                                }
                                                return wn + " · season checkout · " + grp.modelData.count + " episodes";
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
                                            visible: grp.modelData.single
                                                     && grp.modelData.rows[0].state === "failed"
                                            text: "Retry"
                                            color: hRetryMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                            MouseArea { id: hRetryMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: LocalDownloads.retry(grp.modelData.world, grp.modelData.rows[0].id) }
                                        }
                                        Text {
                                            id: hPauseT
                                            readonly property bool anyRunning: {
                                                var rows = grp.modelData.rows;
                                                for (var i = 0; i < rows.length; i++)
                                                    if (rows[i].state === "downloading" || rows[i].state === "resolving"
                                                        || rows[i].state === "queued") return true;
                                                return false;
                                            }
                                            readonly property bool anyPaused: {
                                                var rows = grp.modelData.rows;
                                                for (var i = 0; i < rows.length; i++)
                                                    if (rows[i].state === "paused") return true;
                                                return false;
                                            }
                                            visible: grp.modelData.world === "theatre" && (anyRunning || anyPaused)
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
                                                                if (pausing && (rows[i].state === "downloading"
                                                                        || rows[i].state === "resolving" || rows[i].state === "queued"))
                                                                    LocalDownloads.pause(grp.modelData.world, rows[i].id);
                                                                else if (!pausing && rows[i].state === "paused")
                                                                    LocalDownloads.resume(grp.modelData.world, rows[i].id);
                                                            }
                                                        } }
                                        }
                                        Text {
                                            visible: grp.modelData.liveCount > 0
                                            text: grp.modelData.single
                                                  ? (grp.modelData.rows[0].state === "failed" ? "Remove" : "Cancel")
                                                  : "Cancel season"
                                            color: hCancelMa.containsMouse ? theme.ink : theme.inkDimmer
                                            font.family: theme.ui; font.pixelSize: 12
                                            MouseArea { id: hCancelMa; anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            var rows = grp.modelData.rows;
                                                            for (var i = rows.length - 1; i >= 0; i--)
                                                                if (rows[i].state !== "done")
                                                                    LocalDownloads.cancel(grp.modelData.world, rows[i].id);
                                                        } }
                                        }
                                    }
                                    Rectangle { // aggregate: gold lives on the bottom edge
                                        anchors.left: parent.left; anchors.bottom: parent.bottom
                                        width: parent.width * grp.modelData.ratio
                                        height: 3; color: theme.gold
                                        visible: grp.modelData.liveCount > 0
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
                                            width: 40
                                            text: epRow.modelData.episode > 0
                                                  ? "E" + (epRow.modelData.episode < 10 ? "0" : "") + epRow.modelData.episode
                                                  : ""
                                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
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
                                                    parts.push(Math.round((m.ratio || 0) * 100) + "%");
                                                    if (m.total > 0)
                                                        parts.push(root.fmtBytes(m.received || 0) + " of " + root.fmtBytes(m.total));
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
                                                visible: epRow.modelData.world === "theatre"
                                                         && (epRow.modelData.state === "downloading"
                                                             || epRow.modelData.state === "resolving"
                                                             || epRow.modelData.state === "queued"
                                                             || epRow.modelData.state === "paused")
                                                text: epRow.modelData.state === "paused" ? "Resume" : "Pause"
                                                color: rPauseMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                                MouseArea { id: rPauseMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: epRow.modelData.state === "paused"
                                                                       ? LocalDownloads.resume(epRow.modelData.world, epRow.modelData.id)
                                                                       : LocalDownloads.pause(epRow.modelData.world, epRow.modelData.id) }
                                            }
                                            Text {
                                                visible: epRow.modelData.state === "failed"
                                                text: "Retry"
                                                color: rRetryMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                                                MouseArea { id: rRetryMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: LocalDownloads.retry(epRow.modelData.world, epRow.modelData.id) }
                                            }
                                            Text {
                                                visible: epRow.modelData.state !== "done"
                                                text: epRow.modelData.state === "failed" ? "Remove"
                                                    : epRow.modelData.state === "downloading"
                                                      || epRow.modelData.state === "resolving" ? "Cancel" : "Remove"
                                                color: rCancelMa.containsMouse ? theme.ink : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 12
                                                MouseArea { id: rCancelMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: LocalDownloads.cancel(epRow.modelData.world, epRow.modelData.id) }
                                            }
                                        }
                                        Rectangle { // per-row progress on the bottom edge
                                            anchors.left: parent.left; anchors.bottom: parent.bottom
                                            anchors.leftMargin: 52
                                            width: (parent.width - 78) * (epRow.modelData.ratio || 0)
                                            height: 2; color: theme.gold
                                            visible: epRow.modelData.state === "downloading"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
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
            Item { width: 1; height: 40 }
        }
    }

    ScrollGlide { flick: page }

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
                        return "the file left the disk outside the app — remove the entry or fetch it again";
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
                text: "Remove"
                color: rmMa.containsMouse ? theme.ink : theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 13
                MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                LocalDownloads.remove(row.rowData.world, row.rowData.id)
                                root.refresh()
                            } }
            }
        }
    }
}
