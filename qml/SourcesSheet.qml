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
import "AddonClient.js" as AddonClient
import "Magnet.js" as Magnet

Item {
    id: sheet
    anchors.fill: parent
    // Slice-2 automation surface (no visual effect): names the sheet root so the Lanista bridge can
    // read its state (`qml-get sourcesSheet.loading/rowCount/httpRowCount`) and prove the source-sheet
    // slices (5–7) in the running app instead of by eye. dump-ui sees named items only.
    objectName: "sourcesSheet"

    property Item backdrop
    property string label: ""
    property string title: ""
    property string metaLine: ""
    property string backdropUrl: ""
    property string subType: ""   // "movie" | "series" — carried to the player for online subtitle fetch
    property string subId: ""     // "tt..." (movie) or "tt...:s:e" (episode)
    property var playbackContext: ({})
    property bool open: false
    property bool loading: false
    property bool timedOut: false
    property var rows: []
    property var askedNames: []     // the extensions this ask went out to, ask-order
    property int gen: 0
    property string qualityFilter: "all"
    // Extension picker (2026-08-07, Hemanth's call after the NoTorrent burial): rows sort by ask
    // order, so a freshly-seeded extension's rows land below every earlier extension's — present
    // but off-screen (dossier: docs/research/theatre-http-source/02-notorrent-burial-dossier.md).
    // The top "All ▾" bar was decorative; it now filters the table to one extension's rows. "all"
    // keeps today's behavior exactly. Ordering itself is untouched — no silent re-ranking.
    property string extFilter: "all"
    property bool extMenuOpen: false
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

    visible: sheet.open || sheet.opacity > 0.01
    opacity: sheet.open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Theme { id: theme }

    function show(type, id, lbl, context, pickMode) {
        sheet.mode = (pickMode === "download" || pickMode === "season") ? pickMode : "play";
        sheet.pickSeason = (context && context.season !== undefined) ? Number(context.season) : 0;
        sheet.subType = type ? type : "";
        sheet.subId = id ? id : "";
        sheet.label = lbl ? lbl : "";
        sheet.title = (context && context.title) ? context.title : sheet.label;
        sheet.metaLine = (context && context.metaLine) ? context.metaLine : "";
        sheet.backdropUrl = (context && context.backdrop) ? context.backdrop : "";
        sheet.playbackContext = context || ({});
        sheet.rows = [];
        sheet.qualityFilter = "all";
        sheet.extFilter = "all";
        sheet.extMenuOpen = false;
        sheet.timedOut = false;
        sheet.refreshTitleQueued();
        sheet.loading = true;
        sheet.open = true;
        sheet.gen += 1;
        var myGen = sheet.gen;
        timeout.restart();

        var installedList = (typeof Extensions !== "undefined")
                            ? Extensions.installed() : [];

        // every enabled stream extension that accepts this title, installed order
        var exts = AddonClient.streamExtensions(installedList, type, id);
        sheet.askedNames = exts.map(function(e) {
            return (e.manifest && e.manifest.name) || e.id;
        });
        if (!exts.length) { sheet.loading = false; seasonSettle(); return; }

        AddonClient.loadStreams(exts, type, id,
            function(partialRows) {                 // one extension answered, more out
                if (myGen !== sheet.gen) return;
                sheet.rows = partialRows;
            },
            function(allRows) {                     // every extension answered or timed out
                if (myGen !== sheet.gen) return;
                sheet.rows = allRows;
                sheet.loading = false;
                timeout.stop();
                sheet.warmTopRow();
                seasonSettle();
            });
    }

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
        var rows = sheet.filteredRows();
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i];
            // Direct rows keep a synthetic url:<url> in infoHash only to ride the existing
            // play signal chain. streamKind — not hash emptiness — decides whether warming is legal.
            if (!row || row.streamKind !== "Torrent") continue;
            var h = String(row.infoHash || "");
            if (!h.length) continue;
            if (h === sheet.warmedHash) return;         // already warmed this one
            sheet.warmedHash = h;
            Stream.prefetch(h, row.fileIdx || 0);
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

    function hide() {
        sheet.gen += 1;
        sheet.open = false;
        sheet.extMenuOpen = false;
        timeout.stop();
    }

    // the mode's row universe: season mode sees only full-season torrents,
    // everything else sees the raw answer. Quality pills filter on top of this.
    function baseRows() {
        if (sheet.mode !== "season") return sheet.rows;
        var out = [];
        for (var i = 0; i < sheet.rows.length; ++i)
            if (AddonClient.isSeasonPack(sheet.rows[i], sheet.pickSeason)) out.push(sheet.rows[i]);
        return out;
    }

    // baseRows narrowed to the picked extension. The quality pills and their counts sit ON TOP of
    // this, so "1080p 12" stays truthful inside a "NoTorrent" pick.
    function extRows() {
        var base = baseRows();
        if (sheet.extFilter === "all") return base;
        var out = [];
        for (var i = 0; i < base.length; ++i)
            if (base[i].addonName === sheet.extFilter) out.push(base[i]);
        return out;
    }

    // The picker's menu: All + each extension that actually answered, with its row count.
    function extOptions() {
        var base = baseRows();
        var counts = ({});
        var order = [];
        for (var i = 0; i < base.length; ++i) {
            var n = base[i].addonName || "?";
            if (counts[n] === undefined) { counts[n] = 0; order.push(n); }
            counts[n]++;
        }
        var out = [{ name: "all", label: "All", count: base.length }];
        for (var j = 0; j < order.length; ++j)
            out.push({ name: order[j], label: order[j], count: counts[order[j]] });
        return out;
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
        interval: 22000
        repeat: false
        // partial answers in hand at the bell = a result, not an error
        onTriggered: if (sheet.loading) { sheet.loading = false; sheet.timedOut = sheet.rows.length === 0; sheet.seasonSettle() }
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
        height: 300
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
        x: theme.margin; y: 30; z: 20
        onTriggered: sheet.hide()
    }

    // ---- title block, pinned to the bottom of the banner ----
    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: bannerStrip.height - height - 26
        spacing: 12
        Text {
            visible: sheet.mode !== "play" || sheet.metaLine.length > 0
            width: parent.width
            text: (sheet.mode === "season" ? "DOWNLOAD SEASON — FULL-SEASON TORRENTS" +
                       (sheet.metaLine.length ? "  ·  " + sheet.metaLine : "")
                     : sheet.mode === "download" ? "DOWNLOAD — PICK A SOURCE" +
                       (sheet.metaLine.length ? "  ·  " + sheet.metaLine : "")
                     : sheet.metaLine).toUpperCase()
            color: theme.gold; font.family: theme.ui; font.pixelSize: 12
            font.letterSpacing: 4; elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: sheet.title.length ? sheet.title : "Sources"
            color: theme.ink; font.family: theme.display
            font.pixelSize: 56; font.weight: Font.DemiBold
            maximumLineCount: 1; elide: Text.ElideRight
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    // ===================== filters: the quality TOP BAR + quick-filter pills =====================
    // the full-width selector bar (the "top bar that separates the quality")
    Rectangle {
        id: topBar
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: bannerStrip.bottom; anchors.topMargin: 20
        height: 64; radius: 16
        color: tbMa.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: tbMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
        Behavior on border.color { ColorAnimation { duration: 140 } }
        visible: sheet.rows.length > 0

        Rectangle {                                                      // grid glyph
            id: gridBadge
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 44; height: 44; radius: 12
            color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
            Grid {
                anchors.centerIn: parent; rows: 2; columns: 2; rowSpacing: 5; columnSpacing: 5
                Repeater { model: 4
                    delegate: Rectangle { width: 7; height: 7; radius: 2; color: theme.inkDim } }
            }
        }
        Text {
            anchors.left: gridBadge.right; anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: sheet.extFilter === "all" ? "All" : sheet.extFilter
            color: sheet.extFilter === "all" ? theme.ink : theme.gold
            font.family: theme.ui; font.pixelSize: 22
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"; color: theme.inkDim; font.family: theme.display; font.pixelSize: 18
            rotation: sheet.extMenuOpen ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 140 } }
        }
        // The extension picker (2026-08-07). This bar LOOKED like a picker and did nothing —
        // now it filters the table to one extension's rows (the NoTorrent burial fix).
        MouseArea { id: tbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: sheet.extMenuOpen = !sheet.extMenuOpen }
    }

    // quality quick-filter pills (active = gold fill)
    Row {
        id: pills
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: topBar.bottom; anchors.topMargin: 14
        spacing: 12
        visible: sheet.rows.length > 0
        Repeater {
            model: ["all", "4K", "1080p", "720p", "SD"]
            delegate: Rectangle {
                id: pill
                required property string modelData
                property bool on: sheet.qualityFilter === pill.modelData
                property int n: sheet.countFor(pill.modelData)
                visible: n > 0 || pill.modelData === "all"
                width: pillRow.implicitWidth + 36; height: 40; radius: 20
                color: pill.on ? theme.gold : (pMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
                border.width: pill.on ? 0 : 1
                border.color: theme.edge
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
                    cursorShape: Qt.PointingHandCursor; onClicked: sheet.qualityFilter = pill.modelData }
            }
        }
    }

    // ===================== the glass source table =====================
    Glass {
        id: table
        backdrop: sheet.backdrop
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: pills.visible ? pills.bottom : bannerStrip.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        radius: 18
        track: 0

        Item {
            id: tableHead
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 52
            visible: sheet.visibleRows.length > 0
            Text {
                anchors.left: parent.left; anchors.leftMargin: 26; anchors.verticalCenter: parent.verticalCenter
                text: sheet.visibleRows.length + (sheet.visibleRows.length === 1 ? " source" : " sources")
                      + (sheet.loading ? "  ·  still asking…" : "")
                color: theme.ink; font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 26; anchors.verticalCenter: parent.verticalCenter
                text: sheet.askedNames.length > 2
                      ? "via " + sheet.askedNames.length + " extensions"
                      : "via " + sheet.askedNames.join(" · ")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
        }

        Text {
            anchors.centerIn: parent
            visible: sheet.visibleRows.length === 0
            text: sheet.loading ? "Finding sources…"
                  : (sheet.timedOut ? "Sources timed out. Try again."
                     : (sheet.askedNames.length === 0
                        ? "No stream extension carries this title — add one on the Extensions page."
                        : "No sources found."))
            color: sheet.timedOut ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 16
        }

        ListView {
            id: list
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: tableHead.bottom; anchors.bottom: parent.bottom
            anchors.topMargin: 4; anchors.bottomMargin: 8
            clip: true
            visible: sheet.visibleRows.length > 0
            model: sheet.visibleRows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: list }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                property bool copiedTick: false
                // Slice-2 automation surface (no visual effect): names each source row and exposes
                // read-only state so the Lanista bridge can address it and prove slices 5–7 in the
                // running app. dump-ui sees only named items; this is that name.
                objectName: "sourceRow_" + row.index
                property string streamKind: row.modelData.streamKind || "Torrent"
                property string providerName: row.modelData.addonName || row.modelData.sourceName || ""
                // Today every shown row is ready; slice 5 drives HTTP rows "checking" → "confirmed".
                // Plain string on purpose — ui-wait-for polls strict-equality only.
                property string checkState: "confirmed"
                // Slice 7 flips this when a series pre-picks the source that worked last episode.
                property bool preselected: false
                width: ListView.view.width
                height: 150

                Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent" }

                // provider logo
                Rectangle {
                    id: logo
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
                    anchors.left: logo.right; anchors.leftMargin: 24
                    anchors.right: dlBtn.visible ? dlBtn.left : copyBtn.left
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Row {
                        spacing: 12
                        Text { text: row.modelData.addonName; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { text: row.modelData.qualityLine || row.modelData.quality
                            color: theme.gold
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.5
                            anchors.verticalCenter: parent.verticalCenter }
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.release
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: sheet.metaText(row.modelData)
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight
                    }
                    Row {
                        spacing: 8
                        visible: row.modelData.audio !== undefined
                        Text { text: row.modelData.audio; color: theme.ink
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
                            model: row.modelData.tags || []
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
                        text: (row.modelData.streamKind || "Torrent") + " · " + (row.modelData.streamLabel || "P2P stream")
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }

                // always-visible action button — solid gold, real triangle glyph
                Rectangle {
                    id: play
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
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (sheet.mode !== "play") {   // download & season: the pick IS the download
                            sheet.downloadRequested(row.modelData)
                            sheet.hide()
                        } else {
                            sheet.playRequested(row.modelData.infoHash, row.modelData.fileIdx,
                                                sheet.title, sheet.backdropUrl, sheet.subType, sheet.subId,
                                                sheet.rows, sheet.playbackContext)
                        }
                    }
                }

                // copy link — the row's second verb (spec 2026-07-08). Torrents copy a
                // magnet, direct-HTTP rows copy their url; a brief tick confirms, playback
                // stays on the row click / gold button, untouched. Declared AFTER rowMa so
                // it stacks ABOVE it — copy clicks never fall through into playback.
                Rectangle {
                    id: copyBtn
                    visible: true
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
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var link = Magnet.linkFor(row.modelData)
                            if (!link.length)
                                return
                            Clipboard.copy(link)
                            row.copiedTick = true
                            copyTickTimer.restart()
                        }
                    }
                }
                Timer { id: copyTickTimer; interval: 1200; onTriggered: row.copiedTick = false }

                // download — the row's third verb (2026-07-19), beside the copy: queue
                // THIS title for download pinned to THIS torrent. Play-mode only (in
                // download mode the whole row already IS the download pick). Stays on
                // the sheet; the tick is per-TITLE, not per-row — see titleQueued above.
                Rectangle {
                    id: dlBtn
                    visible: sheet.mode === "play" && typeof Download !== "undefined"
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
                        cursorShape: sheet.titleQueued ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: {
                            if (sheet.titleQueued)
                                return
                            sheet.downloadRequested(row.modelData)
                            sheet.titleQueued = true
                        }
                    }
                }
            }
        }

        ScrollGlide { flick: list }
    }

    // ===================== extension picker menu (declared after the table so it stacks above) =====
    // Click-away closer: any click outside the menu closes it without falling through.
    MouseArea {
        anchors.fill: parent
        visible: sheet.extMenuOpen
        onClicked: sheet.extMenuOpen = false
    }
    Rectangle {
        id: extMenu
        objectName: "extPickerMenu"
        visible: sheet.extMenuOpen
        anchors.left: topBar.left; anchors.right: topBar.right
        anchors.top: topBar.bottom; anchors.topMargin: 6
        height: extMenuCol.implicitHeight + 16
        radius: 16
        color: "#141414"
        border.width: 1; border.color: theme.edge
        Column {
            id: extMenuCol
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.topMargin: 8
            Repeater {
                model: sheet.extMenuOpen ? sheet.extOptions() : []
                delegate: Item {
                    id: extOpt
                    required property var modelData
                    required property int index
                    objectName: "extPick_" + extOpt.index
                    property bool on: sheet.extFilter === extOpt.modelData.name
                    width: parent.width; height: 52
                    Rectangle {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        radius: 10
                        color: extOptMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                    }
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        text: extOpt.modelData.label
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
                        onClicked: {
                            sheet.extFilter = extOpt.modelData.name
                            sheet.extMenuOpen = false
                        }
                    }
                }
            }
        }
    }
}
