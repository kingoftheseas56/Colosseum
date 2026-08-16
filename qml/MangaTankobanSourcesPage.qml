// MangaTankobanSourcesPage — the full-screen "Choose source" picker for ONE
// tankōbon volume, in the Colosseum house language (mirrors ComicTorrentSourcesPage:
// black base + wallpaper backdrop, volume key-art hero washing down, gold eyebrow +
// Fraunces title + identity line, a glass result table). Manga-specific: the ranked
// Nyaa releases (uploader trust → STRONG/POSSIBLE/WEAK, evidence chips explaining
// coverage) then the quieter WeebCentral "Build from chapters" fallback pinned LAST.
// The user always chooses; nothing auto-picks.
//
// Belongs to MangaSeries (a sibling of the reader; mutually-exclusive overlays). All
// acquisition rides the native TankobanVolumes service under the original volumeId —
// this page emits NO reader signal; it only kicks a native download/compile then hides.
import QtQuick
import QtQuick.Controls

Item {
    id: sheet
    // Automation surface (2026-08-12). This page had NO objectName anywhere, so the entire
    // acquisition step — the release list, each release, the download button, the
    // build-from-chapters fallback — was unreachable by the Lanista bridge: an agent could
    // walk into manga and then had nothing to press, which is why the "download fails" report
    // could not be reproduced mechanically. Naming the ROOT is the load-bearing part: it puts
    // `failureText` / `loading` / `complete` / `rows` behind qml-get, so the reason a download
    // was refused is readable directly instead of being inferred from pixels (whole-window
    // grabs are blank on this D3D backend — see the Lanista ledger).
    //
    // Names are WORLD-NAMESPACED (`tankoban*`) per the binding convention in
    // docs/colosseum-lanista-verification.md: a bare shared stem resolves DFS-first and can
    // silently match an occluded item in another world, so a click "lands" green while the
    // visible page never moves.
    objectName: "tankobanSourcesSheet"
    anchors.fill: parent

    // Injection seam (same as MangaTankobanLibrary): the harness assigns a fake; the
    // app leaves this null and the calls fall through to the TankobanVolumes context
    // property. All service access resolves through serviceObject.
    property var service: null
    readonly property var serviceObject: sheet.service
        ? sheet.service
        : ((typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null)

    property Item backdrop: null
    property var context: ({})
    property var rows: []
    property bool open: false
    property bool loading: false
    property bool complete: false
    property string failureText: ""

    // ── live-pick state (approved mock: colosseum-tankoban-sources-live-download-mock.html,
    // 2026-08-16). A pick no longer hides the sheet: the picked row's gold button becomes
    // a status disc (resolving sweep → live % → ✓), the other rows recede, a toast points
    // at the volume shelf, and once every volume started HERE has landed the sheet closes
    // itself. Backing out mid-download is always allowed — the acquisition keeps running
    // service-side and the shelf tile carries it from there. ──
    property var liveIds: []          // volumeIds started from THIS sheet, still interesting
    property var liveProgress: ({})   // volumeId -> { done, total } from `progress` ticks
    property string livePhase: ""     // "" | "live" | "done" | "failed"
    property real liveFraction: -1    // aggregated done/total; -1 = indeterminate (resolving)
    property string pickedHash: ""    // the Nyaa release that was picked (row match key)
    property bool pickedWeeb: false   // …or the WeebCentral fallback card
    property bool toastVisible: false
    property bool toastError: false

    readonly property string identityLine: buildIdentityLine()

    // ── batch mode (design 2026-07-30) ───────────────────────────────────────
    // A batch context carries volumeIds[]; a single tile carries only volumeId.
    // The context ALSO sets volumeId to the batch's FIRST volume, so show()'s
    // search and the applySources/applyFailure stale-handle guards keep working
    // byte-for-byte — the engine has no range search, every search is per volume.
    // One volume in a batch is just a single pick, so isBatch needs > 1.
    readonly property var batchIds: (context && context.volumeIds) ? context.volumeIds : []
    readonly property var batchNumbers: (context && context.volumeNumbers) ? context.volumeNumbers : []
    readonly property bool isBatch: batchIds.length > 1

    // Does this release actually contain EVERY volume of the batch? The engine
    // already parses a release title's volume span into coverageLo/coverageHi
    // (MangaNyaaSource.cpp:71-95) and publishes it on the row, so this is a read,
    // not a re-parse. A row that cannot cover the whole ask is not offered —
    // picking it would leave tiles stuck with nothing behind them.
    function coversBatch(row) {
        var lo = Number(row.coverageLo), hi = Number(row.coverageHi)
        if (!isFinite(lo) || !isFinite(hi)) return false
        var ns = sheet.batchNumbers
        if (!ns.length) return false
        for (var i = 0; i < ns.length; i++) {
            var n = Number(ns[i])
            if (!isFinite(n) || n < lo || n > hi) return false
        }
        return true
    }

    // Batch view of the service's rows: only releases covering the whole batch,
    // TIGHTEST COVERAGE FIRST (design §2 step 3 — a v01–v105 58 GB row must be
    // visible and labelled, not the default). The WeebCentral card carries no
    // coverage and is never a torrent, so it survives untouched and stays LAST:
    // it is the route that always works.
    function rowsForBatch(all) {
        var nyaa = [], other = []
        for (var i = 0; i < all.length; i++) {
            var r = all[i]
            if (String(r.kind) !== "nyaa") { other.push(r); continue }
            if (sheet.coversBatch(r)) nyaa.push(r)
        }
        nyaa.sort(function (a, b) {
            var sa = Number(a.coverageHi) - Number(a.coverageLo)
            var sb = Number(b.coverageHi) - Number(b.coverageLo)
            if (sa !== sb) return sa - sb                              // tightest first
            return (Number(b.seeders) || 0) - (Number(a.seeders) || 0) // then best seeded
        })
        return nyaa.concat(other)
    }

    signal closed()

    visible: sheet.open || sheet.opacity > 0.01
    opacity: sheet.open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Theme { id: theme }

    // ── state contract ───────────────────────────────────────────────────────
    function buildIdentityLine() {
        var parts = []
        if (context.seriesTitle) parts.push(String(context.seriesTitle))
        if (context.volumeNumber !== undefined && String(context.volumeNumber).length)
            parts.push("Vol. " + context.volumeNumber)
        if (context.coverage) parts.push(String(context.coverage))
        return parts.join("      ·      ")
    }
    function titleText() {
        // A batch names the COUNT, never a span — the volumes it covers can be
        // non-contiguous (owned ones are skipped), so "Volumes 35–44" would be a
        // lie the moment he already owns 38. Each row's own coverage chips say
        // what that source contains.
        if (sheet.isBatch) return sheet.batchIds.length + " volumes"
        if (context.volumeTitle && String(context.volumeTitle).length) return String(context.volumeTitle)
        if (context.volumeNumber !== undefined && String(context.volumeNumber).length)
            return "Vol. " + context.volumeNumber
        return "Sources"
    }

    function show(contextObject) {
        context = contextObject
        rows = []
        loading = true; complete = false; failureText = ""
        open = true
        var s = sheet.serviceObject
        if (s && context.volumeId) s.searchSources(context.volumeId)
    }
    function hide() {
        open = false
        rows = []
        loading = false; complete = false; failureText = ""
        _resetLive()
        closed()
    }

    function _resetLive() {
        liveIds = []; liveProgress = ({})
        livePhase = ""; liveFraction = -1
        pickedHash = ""; pickedWeeb = false
        _liveCloseTimer.stop()
    }

    // One place that recomputes the sheet-level phase + fraction from the service's
    // own per-volume status (statusOf reads the façade's live acquisition map, so it
    // is always the truth — never a QML-side guess).
    function _refreshLive() {
        if (!liveIds.length) return
        var s = sheet.serviceObject
        var done = 0, total = 0, anyTotal = false, anyLive = false
        for (var i = 0; i < liveIds.length; i++) {
            var vid = liveIds[i]
            var st = s && s.statusOf ? String(s.statusOf(vid).state || "none") : "none"
            if (st === "failed") { livePhase = "failed"; return }
            if (st === "resolving" || st === "downloading" || st === "ingesting" || st === "packing")
                anyLive = true
            var p = liveProgress[vid]
            if (p && Number(p.total) > 0) {
                done += Number(p.done) || 0
                total += Number(p.total)
                anyTotal = true
            }
        }
        if (!anyLive) {
            // Nothing is running any more. "done" only if EVERY volume this sheet
            // started actually landed (statusOf flips to "ready"); a volume that
            // vanished (cancelled from the Downloads page) must not wear the ✓.
            var allReady = true
            for (var j = 0; j < liveIds.length; j++) {
                var end = s && s.statusOf ? String(s.statusOf(liveIds[j]).state || "none") : "none"
                if (end !== "ready") { allReady = false; break }
            }
            livePhase = allReady ? "done" : "failed"
            if (allReady)
                _liveCloseTimer.restart()   // the ✓ beat, then the sheet closes itself
            else
                _liveFailTimer.restart()    // hand the rows back for another pick
            return
        }
        livePhase = "live"
        liveFraction = anyTotal ? Math.max(0, Math.min(1, done / total)) : -1
    }

    // The status line under the picked release: phase word first, % once bytes move.
    function statusText() {
        if (livePhase === "done") return "Done — added to your shelf"
        if (livePhase === "failed") return "Failed — pick another source"
        var s = sheet.serviceObject
        var resolving = false, packing = false, ingesting = false
        for (var i = 0; i < liveIds.length; i++) {
            var st = s && s.statusOf ? String(s.statusOf(liveIds[i]).state || "") : ""
            if (st === "resolving") resolving = true
            if (st === "packing") packing = true
            if (st === "ingesting") ingesting = true
        }
        if (packing) return "Building from chapters…"
        if (resolving) return "Resolving torrent…"
        if (ingesting) return "Adding to library…"
        return liveFraction >= 0 ? ("Downloading · " + Math.round(liveFraction * 100) + "%")
                                 : "Downloading…"
    }

    function toast(message, isError) {
        toastText.text = message
        toastError = isError === true
        toastVisible = true
        _toastTimer.restart()
    }

    // Called right after the service kick: keeps only the ids that actually went
    // in flight. A pick the service refused outright has ALREADY emitted `failed`
    // (same-thread direct signal) before downloadNyaa returned, so liveIds ends up
    // empty and the refusal surfaces as an error toast instead of a frozen disc.
    function startLive(ids, building) {
        var s = sheet.serviceObject
        var live = []
        for (var i = 0; i < ids.length; i++) {
            var st = s && s.statusOf ? String(s.statusOf(String(ids[i])).state || "none") : "none"
            if (st === "resolving" || st === "downloading" || st === "ingesting" || st === "packing")
                live.push(String(ids[i]))
        }
        if (!live.length) {
            toast(failureText.length ? failureText : "That source could not be started.", true)
            pickedHash = ""; pickedWeeb = false
            return
        }
        liveIds = live
        liveProgress = ({})
        livePhase = "live"; liveFraction = -1
        _refreshLive()
        if (building) {
            toast(live.length === 1 ? "Building from chapters — follow it on the volume shelf"
                                    : live.length + " volumes building — follow the shelf", false)
        } else if (live.length === 1) {
            var n = (context.volumeNumber !== undefined && String(context.volumeNumber).length)
                    ? String(context.volumeNumber) : ""
            toast(n.length ? ("Vol. " + n + " is downloading — follow it on the volume shelf")
                           : "Downloading — follow it on the volume shelf", false)
        } else {
            toast(live.length + " volumes are downloading — follow the shelf", false)
        }
    }

    function applySources(vid, results) {
        if (String(vid) !== String(context.volumeId)) return   // stale handle
        // Filter HERE, so the count line, the empty state and the list all agree
        // — a single-volume pick is untouched and still sees the service's order.
        rows = sheet.isBatch ? sheet.rowsForBatch(results || []) : (results || [])
        loading = false; complete = true
    }
    function applyFailure(vid, reason) {
        if (String(vid) !== String(context.volumeId)) return
        loading = false; complete = true; failureText = String(reason)
    }

    // A chosen Nyaa release → native download, then the sheet goes LIVE on that
    // row (status disc + recede + toast) instead of vanishing. A pick that can't
    // isolate the volume reports why through the service's own `failed` reason —
    // no auto-fallback here.
    function pickNyaa(modelData) {
        var s = sheet.serviceObject
        if (!s || !modelData || !modelData.infoHash) { hide(); return }
        pickedHash = String(modelData.infoHash); pickedWeeb = false
        if (sheet.isBatch) {
            // A batch acquires every volume from the ONE chosen torrent. The
            // transport is already multi-intent (one Job per infoHash holding an
            // Intent per volume, file priorities unioned), so this is N intents on
            // one download, not N downloads.
            s.downloadNyaaBatch(sheet.batchIds, modelData.infoHash)
        } else {
            s.downloadNyaa(context.volumeId, modelData.infoHash)
        }
        startLive(sheet.isBatch ? sheet.batchIds : [String(context.volumeId)], false)
    }
    // The WeebCentral fallback: disabled cards do nothing; enabled ones compile.
    // It needs no per-volume choice, so a batch is a loop over the existing
    // per-volume entry point — nothing new in the engine. Same live treatment.
    function pickWeeb(modelData) {
        if (modelData && modelData.enabled === false) return
        var s = sheet.serviceObject
        if (!s) { hide(); return }
        pickedWeeb = true; pickedHash = ""
        var ids = sheet.isBatch ? sheet.batchIds : [String(context.volumeId)]
        for (var i = 0; i < ids.length; i++) s.compileWeebCentral(String(ids[i]))
        startLive(ids, true)
    }

    // ── display helpers ──────────────────────────────────────────────────────
    function fmtSize(bytes) {
        var n = Number(bytes) || 0
        if (n <= 0) return ""
        var mb = n / 1048576
        if (mb >= 1024) return (mb / 1024).toFixed(1) + " GB"
        return (mb >= 100 ? mb.toFixed(0) : mb.toFixed(1)) + " MB"
    }
    // MATCH confidence, NOT uploader identity. Everything reaching this sheet already passed
    // the native strong-series + volume-coverage match (genuine non-matches are dropped
    // upstream, MangaNyaaSource.cpp:306), so nothing shown here is a "weak match". A
    // single-volume release is the tightest possible fit; a trusted uploader confirms it;
    // anything else is a batch that still COVERS the target volume -- a solid POSSIBLE, never
    // WEAK. Uploader trust is a bonus (the TRUSTED evidence chip), never a demotion.
    function isTightMatch(row) {
        return row && (row.standalone === true || Number(row.tier) === 1)
    }
    function confidenceColor(row) {
        return sheet.isTightMatch(row) ? theme.gold : "#8ea3c0"   // gold = tight match; blue-grey = covering batch
    }
    function confidenceLabel(row) {
        return sheet.isTightMatch(row) ? "STRONG" : "POSSIBLE"
    }
    // Evidence chips EXPLAIN coverage; they never claim the spreads inside are intact.
    function evidenceChips(modelData) {
        var out = []
        if (!modelData) return out
        if (modelData.digital) out.push("DIGITAL")
        out.push(modelData.standalone
                 ? "SINGLE VOLUME"
                 : ("VOLS " + (modelData.coverageLo || "?") + "–" + (modelData.coverageHi || "?")))
        if (Number(modelData.tier) < 99) out.push("TRUSTED")
        return out
    }

    // ── facade signals: guarded by the live volume id (stale handles ignored) ──
    Connections {
        target: sheet.serviceObject
        ignoreUnknownSignals: true
        function onSourcesReady(vid, results) { sheet.applySources(vid, results) }
        function onFailed(vid, reason) {
            sheet.applyFailure(vid, reason)
            if (sheet.liveIds.indexOf(String(vid)) < 0) return
            // A volume THIS sheet started failed mid-flight: show the disc's "!" beat,
            // tell him why, then hand the row back so another source can be picked.
            sheet.livePhase = "failed"
            sheet.toast("Couldn’t get it from that source — " + reason
                        + ". Pick another one.", true)
            sheet._liveFailTimer.restart()
        }
        function onProgress(vid, done, total) {
            if (sheet.liveIds.indexOf(String(vid)) < 0) return
            var out = {}
            for (var k in sheet.liveProgress) out[k] = sheet.liveProgress[k]
            out[String(vid)] = { "done": done, "total": total }
            sheet.liveProgress = out
            sheet._refreshLive()
        }
        function onFinished(vid) { if (sheet.liveIds.indexOf(String(vid)) >= 0) sheet._refreshLive() }
        function onRemoved(vid) { if (sheet.liveIds.indexOf(String(vid)) >= 0) sheet._refreshLive() }
    }

    // The ✓ beat before the sheet closes itself on a completed pick.
    Timer {
        id: _liveCloseTimer
        interval: 1400
        onTriggered: { if (sheet.livePhase === "done") sheet.hide() }
    }
    // The "!" beat before the rows come back for another pick.
    Timer {
        id: _liveFailTimer
        interval: 1600
        onTriggered: {
            sheet.pickedHash = ""; sheet.pickedWeeb = false
            sheet.liveIds = []; sheet.liveProgress = ({})
            sheet.livePhase = ""; sheet.liveFraction = -1
        }
    }
    Timer {
        id: _toastTimer
        interval: 3800
        onTriggered: sheet.toastVisible = false
    }

    // ── base: float over the wallpaper, not a flat void ──
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: sheet.backdrop
        live: true; hideSource: false
        visible: sheet.backdrop !== null
        opacity: 0.5
    }
    MouseArea { anchors.fill: parent }   // absorb clicks from below

    // ── the status disc that replaces a picked row's gold ↓ (approved mock). It
    // rides the sheet's aggregated live state: spinner while resolving, gold %
    // once bytes move, gold ✓ when every volume has landed, "!" on failure. ──
    component StatusDisc: Rectangle {
        width: 56; height: 56; radius: 28
        color: sheet.livePhase === "done" ? theme.gold : Qt.rgba(0.04, 0.045, 0.06, 0.92)
        border.width: sheet.livePhase === "done" ? 0 : 1
        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.6)
        Canvas {
            id: discSpin
            anchors.centerIn: parent
            width: 26; height: 26
            visible: sheet.livePhase === "live" && sheet.liveFraction < 0
            rotation: 0
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 1150
                loops: Animation.Infinite; running: discSpin.visible
            }
            onVisibleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 2.4
                ctx.strokeStyle = "#f0c44a"
                ctx.beginPath()
                ctx.arc(13, 13, 10, 0, Math.PI * 0.75)
                ctx.stroke()
            }
        }
        Text {
            anchors.centerIn: parent
            visible: sheet.livePhase === "live" && sheet.liveFraction >= 0
            text: Math.round(sheet.liveFraction * 100) + "%"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            anchors.centerIn: parent
            visible: sheet.livePhase === "done"
            text: "✓"; color: "#1a1306"
            font.family: theme.ui; font.pixelSize: 20; font.weight: Font.DemiBold
        }
        Text {
            anchors.centerIn: parent
            visible: sheet.livePhase === "failed"
            text: "!"; color: "#e6a3a3"
            font.family: theme.ui; font.pixelSize: 20; font.weight: Font.DemiBold
        }
    }

    // ── the toast: one line of confirmation that leaves on its own ──
    Rectangle {
        visible: sheet.toastVisible && sheet.open
        z: 40
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 34
        width: toastText.implicitWidth + 44; height: 46; radius: 12
        color: Qt.rgba(0.04, 0.045, 0.06, 0.95)
        border.width: 1
        border.color: sheet.toastError ? Qt.rgba(0.9, 0.4, 0.4, 0.55)
                                       : Qt.rgba(0.94, 0.77, 0.29, 0.55)
        Text {
            id: toastText
            anchors.centerIn: parent
            color: sheet.toastError ? "#e6a3a3" : theme.ink
            font.family: theme.ui; font.pixelSize: 13
        }
    }

    // ── volume key-art hero across the top, washing down ──
    Item {
        id: bannerStrip
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 300
        Image {
            anchors.fill: parent
            source: sheet.context.cover ? sheet.context.cover : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            visible: source != ""
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
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: bannerStrip.bottom; anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.9)
    }

    BackAction {
        x: theme.margin; y: 30; z: 20
        onTriggered: sheet.hide()
    }

    // ── title block + volume identity rail, pinned to the bottom of the banner ──
    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: bannerStrip.height - height - 26
        spacing: 12
        Text {
            width: parent.width
            text: "SOURCES · TANKOBAN VOLUME"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 12
            font.letterSpacing: 4; elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: sheet.titleText()
            color: theme.ink; font.family: theme.display
            font.pixelSize: 56; font.weight: Font.DemiBold
            maximumLineCount: 1; elide: Text.ElideRight
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
        Text {
            width: parent.width
            visible: sheet.identityLine.length > 0
            text: sheet.identityLine
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
            font.letterSpacing: 1; elide: Text.ElideRight
        }
        // Slice D (mock .hsyn): this volume's own synopsis when the enricher accepted one,
        // else the honest italic empty state — a per-volume synopsis is mostly absent today,
        // so the empty state is the common case, matching the mock.
        Text {
            id: heroSynopsis
            objectName: "tankobanSourcesSynopsis"
            width: parent.width
            readonly property bool hasSynopsis: String(sheet.context.synopsis || "").length > 0
            text: heroSynopsis.hasSynopsis
                ? String(sheet.context.synopsis)
                : "No synopsis for this volume yet."
            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
            font.italic: !heroSynopsis.hasSynopsis
            wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight
        }
    }

    // ── the glass table: ranked Nyaa rows, WeebCentral fallback last ──
    Glass {
        id: table
        backdrop: sheet.backdrop
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        anchors.top: bannerStrip.bottom; anchors.topMargin: 20
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        radius: 18
        track: 0

        Item {
            id: tableHead
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 52
            visible: sheet.rows.length > 0
            Text {
                anchors.left: parent.left; anchors.leftMargin: 26
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.rows.length + (sheet.rows.length === 1 ? " source" : " sources")
                      + (sheet.loading ? "   ·   still searching…" : "")
                color: theme.ink; font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 26
                anchors.verticalCenter: parent.verticalCenter
                text: "Nyaa · WeebCentral"
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 80
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            // The user-visible "why is this list empty" line. Named so a driver reads the SAME
            // sentence the reader sees, rather than reconstructing it from state.
            objectName: "tankobanSourcesEmptyText"
            visible: sheet.rows.length === 0
            text: sheet.loading ? "Searching Nyaa releases…"
                  : (sheet.failureText.length > 0
                     ? ("Some sources didn’t answer — " + sheet.failureText)
                     : "No releases matched this volume yet.")
            color: sheet.failureText.length > 0 ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 16
        }

        ListView {
            id: list
            // `count` behind qml-get answers "did the search return anything" without pixels.
            objectName: "tankobanSourcesList"
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: tableHead.bottom; anchors.bottom: parent.bottom
            anchors.topMargin: 4; anchors.bottomMargin: 8
            clip: true
            visible: sheet.rows.length > 0
            model: sheet.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: list }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                // Index-keyed so a driver can press a specific release ("the first one") without
                // knowing its hash; the row's own modelData (infoHash, releaseTitle, enabled,
                // seeders, coverage) is then readable off this same name via qml-get.
                objectName: "tankobanSourceRow_" + row.index
                width: ListView.view.width
                readonly property bool isWeeb: row.modelData && row.modelData.kind === "weebcentral"
                readonly property bool rowEnabled: row.modelData ? (row.modelData.enabled !== false) : false
                // The row THIS sheet started a download from (hash match for Nyaa,
                // the fallback flag for WeebCentral). While a pick is live the
                // chosen row carries the status disc and the others recede.
                readonly property bool isPickRow: sheet.pickedHash.length > 0 && row.modelData
                    && String(row.modelData.infoHash || "") === sheet.pickedHash
                readonly property bool carriesDisc: (row.isPickRow || (row.isWeeb && sheet.pickedWeeb))
                    && sheet.livePhase.length > 0
                readonly property bool dimmed: sheet.livePhase.length > 0 && !row.carriesDisc
                height: row.isWeeb ? 96 : 150
                opacity: row.dimmed ? 0.45 : 1
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Rectangle {
                    anchors.fill: parent
                    color: (rowMa.containsMouse && row.rowEnabled) ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                }

                // ── NYAA release row ──
                Item {
                    anchors.fill: parent
                    visible: !row.isWeeb

                    Rectangle {
                        id: srcBadge
                        anchors.left: parent.left; anchors.leftMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        width: 54; height: 54; radius: 12
                        color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent
                            text: String((row.modelData && row.modelData.uploader) || "?").charAt(0).toUpperCase()
                            color: theme.ink; font.family: theme.display; font.pixelSize: 24; font.weight: Font.DemiBold
                        }
                    }

                    Column {
                        anchors.left: srcBadge.right; anchors.leftMargin: 24
                        anchors.right: pickBtn.left; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Row {
                            spacing: 12
                            Text {
                                text: (row.modelData && row.modelData.uploader) ? row.modelData.uploader : "Nyaa"
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: sheet.confidenceLabel(row.modelData || null)
                                color: sheet.confidenceColor(row.modelData || null)
                                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.5
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            width: parent.width
                            text: (row.modelData && row.modelData.releaseTitle) ? row.modelData.releaseTitle : ""
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14; elide: Text.ElideRight
                        }
                        Row {
                            spacing: 7
                            Repeater {
                                model: sheet.evidenceChips(row.modelData)
                                delegate: Rectangle {
                                    required property string modelData
                                    width: ev.implicitWidth + 16; height: 20; radius: 6
                                    color: Qt.rgba(1, 1, 1, 0.05); border.width: 1; border.color: theme.edge
                                    Text {
                                        id: ev; anchors.centerIn: parent; text: parent.modelData
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 10
                                        font.weight: Font.DemiBold; font.letterSpacing: 0.6
                                    }
                                }
                            }
                        }
                        Text {
                            text: {
                                if (row.carriesDisc) return sheet.statusText()
                                var p = []
                                var sz = sheet.fmtSize(row.modelData ? row.modelData.sizeBytes : 0)
                                if (sz.length) p.push(sz)
                                p.push("\u{1F464} " + (row.modelData ? Number(row.modelData.seeders || 0) : 0))
                                return p.join("   ·   ")
                            }
                            color: row.carriesDisc ? theme.gold : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        id: pickBtn
                        // THE download button for a Nyaa release. Note the whole row is the
                        // click target (the MouseArea fills it); this name exists so a driver
                        // can assert the button is actually drawn and hit it precisely.
                        objectName: "tankobanSourceDownload_" + row.index
                        anchors.right: parent.right; anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        width: 56; height: 56; radius: 28; color: theme.gold
                        visible: !row.carriesDisc
                        scale: rowMa.containsMouse ? 1.05 : 1.0
                        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        Text {
                            anchors.centerIn: parent; text: "↓"; color: "#1a1306"
                            font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                    }
                    StatusDisc {
                        visible: row.carriesDisc
                        anchors.right: parent.right; anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // ── WEEBCENTRAL fallback row (quieter; disabled shows its reason) ──
                Item {
                    anchors.fill: parent
                    visible: row.isWeeb
                    opacity: row.rowEnabled ? 1.0 : 0.55

                    Rectangle {
                        id: weebBadge
                        anchors.left: parent.left; anchors.leftMargin: 26
                        anchors.verticalCenter: parent.verticalCenter
                        width: 54; height: 54; radius: 12
                        color: "transparent"; border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent; text: "W"
                            color: theme.inkDim; font.family: theme.display; font.pixelSize: 22
                        }
                    }
                    Column {
                        anchors.left: weebBadge.right; anchors.leftMargin: 24
                        anchors.right: weebBtn.left; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Text {
                            width: parent.width
                            text: (row.modelData && row.modelData.label) ? row.modelData.label : "Build from chapters"
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                            font.weight: Font.DemiBold; elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: row.carriesDisc ? sheet.statusText()
                                : (row.rowEnabled
                                   ? ("Compiles this volume from "
                                      + (row.modelData && row.modelData.chapterCount ? row.modelData.chapterCount : 0)
                                      + " WeebCentral chapters.")
                                   : ((row.modelData && row.modelData.reason) ? row.modelData.reason : "Unavailable."))
                            color: row.carriesDisc ? theme.gold : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
                        }
                    }
                    Rectangle {
                        id: weebBtn
                        // The non-torrent route: build this volume from WeebCentral chapters.
                        // One per sheet (the fallback card is always last and always single).
                        objectName: "tankobanSourceBuildFromChapters"
                        anchors.right: parent.right; anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        width: 56; height: 56; radius: 28
                        visible: row.rowEnabled && !row.carriesDisc
                        color: rowMa.containsMouse ? theme.glassHi : theme.glassTint
                        border.width: 1
                        border.color: rowMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                        Text { anchors.centerIn: parent; text: "↓"; color: theme.ink; font.pixelSize: 18 }
                    }
                    StatusDisc {
                        visible: row.carriesDisc
                        anchors.right: parent.right; anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: rowMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: row.rowEnabled && !sheet.liveIds.length ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (sheet.liveIds.length > 0) return   // a pick is running — wait or back out
                        if (row.isWeeb) sheet.pickWeeb(row.modelData)
                        else sheet.pickNyaa(row.modelData)
                    }
                }
            }
        }
        ScrollGlide { flick: list }
    }
}
