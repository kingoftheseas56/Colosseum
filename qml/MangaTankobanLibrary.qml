// MangaTankobanLibrary - the Pages/Flow-derived volume continuum for a Tankoban series.
//
// v2.3 adoption (arc-08, 2026-08-21, re-derived against the LANDED catalogue-independence
// tree — Slices 2-5 + R1 are all in on master by the time this landed). Supersedes the
// 2026-08-14 vertical bookshelf. Governing docs, in force order: POLISH-DELTA.md (the
// amending contract) over DESIGN-CONTRACT.md (v1), both against the approved v2.3 oracle
// reference/visual/colosseum-manga-series-volume-flow-mock-v2.html (Preflight arc-08).
// Eyes-on verdict on the v2.3 direction: "perfect" (Hemanth, 2026-08-20).
//
// Reconciled against LIVE drift the arc's own candidate could not see (STATUS.md Adoption
// risk #1, ground-truthed live during this adoption):
//   - coverFor() is the LIVE ladder, not the candidate's — catalogue-independence Slice 3
//     already deleted the WeebCentral thumb-scrape machinery and CuratedVolumeCovers.js
//     entirely (Qt6 blocks file XHR; that lookup was dead code). A row's cover is either
//     already baked into it (TankobanCatalog, via MangaSeries.qml's _prepareTankoban) or,
//     once the volume is on disk, its own first extracted page. Ladder: catalogue cover ->
//     localPages() first page when ready -> NO COVER glass. `chapters`, `curatedCovers`,
//     `requestCovers`/`visibleRowsForCovers`/`visibleGridRows`/`_firstChapterIdIn`/
//     `_thumbWanted`/`coverByVolume`/the cover-prefetch timer/the onThumbReady Connections
//     do not exist in this file — tests/manga_reading_room_harness.qml asserts their
//     absence by typeof, not just their being unused.
//   - Range captions are dead (POLISH-DELTA ruling #1): the caption slot reads the volume's
//     real catalogue name via volumeNameFor() (a redundant "Volume N" name collapses to
//     nothing) and an in-flight/failed state line via stateLineFor().
//   - The Select-mode header toggle ("Hemanth greenlit KEEP-IT", 2026-08-14 handoff) is
//     LIVE, separately-approved work the v2.3 candidate never saw (it was authored against
//     an earlier header shape) — it is preserved here, transplanted onto the new flow's
//     lane header, rather than silently dropped.
//   - The per-volume card keeps its LIVE automation name (`tankobanVolumeCard_<token>`,
//     catalogue-independence Slice 3/4's own naming law — the committed Lanista scenarios
//     click it directly) instead of the candidate's bare `volumeFlowTile` name.
//   - `tankobanShelfState` (rowCount/coveredCount bridge scalars) is preserved unchanged —
//     the committed cover-ladder harness assertion depends on it.
//
// What the v2.3 pass itself changes (from the pre-arc vertical grid, POLISH-DELTA rulings):
// no strict division lines (#11) — a whisper "VOLUMES" label, no bordered header/select-bar;
// dynamic never-cropped cover clamp measured from the actual flow viewport (#10); 2px gold
// owned mark, label-free (#6); NO COVER fallback matches the app's existing glass language
// (#5); Get/Read/Retry/percent state vocabulary lives on one shallow action bar tied to the
// centred volume (never a second acquisition path); long-series PageUp/PageDown/Home/End and
// Shift+wheel jump 10, plain wheel/arrow keys step 1 (#7); resume-centering on the existing
// auto-land cursor (#8).
//
// `focusIndex`/`focusToken` and their small `focusAtNumber`/`focusAtIndex`/`jumpToNumber` API
// now drive the flow's own centring (scaleForIndex/centreFlow) — a real visual job, not the
// inert-cursor status they carried in the pre-v2.3 vertical grid.
pragma ComponentBehavior: Bound
import QtQuick
import "MangaVolumes.js" as Vol

Item {
    id: root
    objectName: "volumeShelf"

    property string seriesId: ""
    property string seriesTitle: ""
    property var service: null
    readonly property var serviceObject: root.service
        ? root.service
        : ((typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null)
    property var progress: null
    readonly property var progressObject: root.progress
        ? root.progress
        : ((typeof Progress !== "undefined") ? Progress : null)
    property var downloader: null
    readonly property var downloaderObject: root.downloader
        ? root.downloader
        : ((typeof Downloads !== "undefined") ? Downloads : null)
    // Vestigial (Slice 3, 2026-08-20): the WC thumb-scrape ladder this once gated is gone;
    // no live caller flips it false anymore. Kept only so a stale caller binding does not
    // hard-error; not read anywhere in this file.
    property bool coverFetchingEnabled: true

    readonly property bool showVolumes: root.volumeRows.length > 0
    property var _resume: null
    property var volumeRows: []
    property var progressByVolume: ({})

    // Kept for the service and batch contracts. The flow owns the full canonical model;
    // legacy page groups are batch vocabulary only.
    readonly property int pageSize: 10
    readonly property var pagedRows: Vol.pageGroups(root.volumeRows, root.pageSize)
    property int activePage: 0
    readonly property var visibleRows: root.rowsOnPage(root.activePage)
    readonly property var activePageInfo:
        root.activePage >= 0 && root.activePage < root.pagedRows.length
            ? root.pagedRows[root.activePage] : null

    // Select-mode batch-download state (TB-002, 2026-07-30; header toggle greenlit
    // 2026-08-14). Untouched by the v2.3 pass — see the file header note.
    property bool selecting: false
    property bool _dragSelecting: false
    property var selectedNumbers: []

    // The flow's centring cursor (see file header note - upgraded from inert to live-visual
    // by the v2.3 pass).
    property var focusNumber: 1
    property string focusToken: "1"
    readonly property int visibleContinuumCount: root.width > 1500 ? 11 : (root.width > 1180 ? 9 : 7)
    readonly property int focusIndex: root.indexOfNumber(root.focusToken)
    property int _landedIndex: -1
    readonly property int autoLandIndex: root._landedIndex
    readonly property int autoLandNumber: root.currentNumber

    // `renderedCount` remains the canonical-model count for the established batch harness.
    // `liveVolumeTiles` is the real delegate count and proves the flow is virtualized.
    // `flowCurrentIndex` mirrors the ListView's own currentIndex.
    readonly property int renderedCount: root.volumeRows.length
    property int liveVolumeTiles: 0
    readonly property int flowCurrentIndex: volumeFlow ? volumeFlow.currentIndex : -1

    implicitWidth: 640
    implicitHeight: 480

    signal batchRequested(var numbers, string label)
    signal openVolumeRequested(string volumeId)
    signal sourcesRequested(var context)

    Theme { id: theme }

    // ------------------------------------------------------------------
    // Native seams and canonical state (unchanged from the pre-arc baseline)
    // ------------------------------------------------------------------

    function refresh() {
        var s = root.serviceObject
        root.volumeRows = s && s.volumesForSeries ? s.volumesForSeries(root.seriesId) : []
    }

    function refreshResume() {
        var p = root.progressObject
        root._resume = p && root.seriesId.length ? p.get("tankoban", root.seriesId) : null
    }

    readonly property string continueVolumeId:
        (root._resume && root._resume.chapterId) ? String(root._resume.chapterId) : ""
    readonly property int continuePage:
        (root._resume && root._resume.page) ? Number(root._resume.page) : 0
    readonly property int continueMax:
        (root._resume && root._resume.max) ? Number(root._resume.max) : 0
    readonly property real continueFraction:
        root.continueMax > 0 ? Math.max(0, Math.min(1, root.continuePage / root.continueMax)) : 0

    readonly property int ownedCount: {
        var total = 0
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].state) === "ready") total++
        return total
    }

    readonly property int currentNumber: {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === root.continueVolumeId)
                return Number(rows[i].number) || 0
        return 0
    }

    readonly property var unavailableNumbers: {
        var out = {}
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            var n = Number(rows[i].number)
            if (!isFinite(n)) continue
            var live = root.progressByVolume[String(rows[i].id)]
            var state = live ? String(live.state || "downloading") : String(rows[i].state || "none")
            if (state === "ready" || root._inFlight(state)) out[n] = true
        }
        return out
    }

    readonly property var nextBatch: Vol.nextBatch(root.volumeRows,
                                                    root.unavailableNumbers,
                                                    root.currentNumber,
                                                    root.pageSize)
    readonly property var activePageUnowned:
        root.activePageInfo ? root.unownedIn(root.activePageInfo.volumes) : []

    function rowsOnPage(index) {
        var pages = root.pagedRows || []
        return index >= 0 && index < pages.length ? pages[index].volumes : []
    }

    function pageIndexOf(number) {
        var n = Number(number)
        for (var i = 0; i < root.pagedRows.length; i++) {
            var rows = root.pagedRows[i].volumes
            for (var j = 0; j < rows.length; j++)
                if (Number(rows[j].number) === n) return i
        }
        return -1
    }

    // Compatibility page state remains available to batch logic and old callers, but never
    // controls the flow's visual layout.
    property bool _pageHomed: false
    function _homeActivePage() {
        if (root._pageHomed) return
        var index = root.pageIndexOf(root.currentNumber)
        root.activePage = index >= 0 ? index : 0
        root._pageHomed = true
    }

    function _inFlight(state) {
        return state === "resolving" || state === "ingesting" || state === "packing"
            || state === "downloading"
    }

    function _reassign(map, key, value) {
        var out = {}
        for (var k in map) out[k] = map[k]
        out[key] = value
        return out
    }

    function _ownsVolume(volumeId) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === String(volumeId)) return true
        return false
    }

    function clearProgress(volumeId) {
        if (root.progressByVolume[volumeId] === undefined) return
        var out = {}
        for (var k in root.progressByVolume)
            if (k !== volumeId) out[k] = root.progressByVolume[k]
        root.progressByVolume = out
    }

    function effectiveState(row) {
        var live = root.progressByVolume[String(row && row.id)]
        return live ? String(live.state || "downloading") : String((row && row.state) || "none")
    }

    function progressFraction(row) {
        var live = root.progressByVolume[String(row && row.id)]
        if (!live) return -1
        var total = Number(live.total) || 0
        return total > 0 ? Math.max(0, Math.min(1, (Number(live.done) || 0) / total)) : -1
    }

    // ------------------------------------------------------------------
    // Caption vocabulary (v2.3): real catalogue name, never a range. A redundant "Volume N"
    // name (the common case until BookWalker display names enrich the catalogue further)
    // collapses to nothing rather than restating the number already printed above it.
    // ------------------------------------------------------------------

    function _escapeRegExp(s) { return String(s).replace(/[.*+?^${}()|[\]\\]/g, "\\$&") }

    // Defensive on the row shape: the baked catalogue feeds `.name`; TankobanVolumes rows
    // still carry the legacy `.title` field. Neither is invented here - an empty/missing
    // field on both simply means no name shows.
    function volumeNameFor(row) {
        var raw = ""
        if (row && row.name && String(row.name).length) raw = String(row.name)
        else if (row && row.title && String(row.title).length) raw = String(row.title)
        if (!raw.length) return ""
        var trimmed = raw.trim()
        var tok = Vol.volumeToken(row)
        var redundant = new RegExp("^vol(ume)?\\.?\\s*0*" + root._escapeRegExp(tok) + "$", "i")
        return redundant.test(trimmed) ? "" : trimmed
    }

    // The caption's third line: acquisition state, never a chapter word. Empty when the
    // volume is simply available (Get) or already owned (Read) - those two live on the
    // action bar/owned mark, not the caption.
    function stateLineFor(row) {
        var state = root.effectiveState(row)
        if (state === "failed") return "failed"
        if (root._inFlight(state)) {
            var f = root.progressFraction(row)
            return (f >= 0 ? (Math.round(f * 100) + "% | ") : "") + "downloading"
        }
        return ""
    }

    // ------------------------------------------------------------------
    // Covers (catalogue-independence Slice 3, 2026-08-20): no live thumb scraping, no
    // bounded prefetch window - a row's cover is either already baked into it
    // (TankobanCatalog, via MangaSeries.qml's _prepareTankoban) or, once the volume is on
    // disk, its own first extracted page. Ladder: catalogue cover -> localPages() first
    // page when ready (app-owned bytes) -> NO COVER glass (the delegate's own
    // coverImage.status !== Ready branch).
    // ------------------------------------------------------------------

    function coverFor(row) {
        var catalogueCover = (row && row.cover && String(row.cover).length) ? String(row.cover) : ""
        if (catalogueCover.length) return catalogueCover
        if (root.effectiveState(row) === "ready") {
            var vid = String(row && row.id || "")
            var s = root.serviceObject
            if (s && s.localPages) {
                var pages = s.localPages(vid)
                if (pages && pages.length && pages[0].url) return String(pages[0].url)
            }
        }
        return ""
    }

    // ------------------------------------------------------------------
    // Prefetch cursor + selection + actions (unchanged from the pre-arc baseline)
    // ------------------------------------------------------------------

    function initialFocusNumber() {
        var rows = root.volumeRows || []
        if (!rows.length) return 1
        var current = root.currentNumber
        if (current > 0) return String(current)
        return String(rows[0].number !== undefined ? rows[0].number : 1)
    }

    function indexOfNumber(number) {
        var n = String(number)
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].number) === n) return i
        return -1
    }

    function focusAtNumber(number) {
        var rows = root.volumeRows || []
        if (!rows.length) return
        var idx = root.indexOfNumber(number)
        if (idx < 0) idx = 0
        root.focusToken = String(rows[idx].number !== undefined ? rows[idx].number : (idx + 1))
        root.focusNumber = rows[idx].number !== undefined ? rows[idx].number : (idx + 1)
        root._landedIndex = idx
        Qt.callLater(root.centreFlow)
    }

    function focusAtIndex(index) {
        var rows = root.volumeRows || []
        if (!rows.length) return
        var idx = Math.max(0, Math.min(rows.length - 1, Math.round(Number(index) || 0)))
        root.focusAtNumber(String(rows[idx].number !== undefined ? rows[idx].number : (idx + 1)))
    }

    // Headless activation by index - a real pointer tap always goes straight to
    // primaryAction (per the approved mock's "click a neighbour to centre it, click the
    // centred book to act" rule), but this is kept for the Select-mode batch contract and
    // for programmatic/keyboard callers.
    function pressVolume(index) {
        var rows = root.volumeRows || []
        var target = Math.max(0, Math.min(rows.length - 1, Math.round(index)))
        if (target < 0 || target >= rows.length) return false
        if (root.selecting) root.selectNumber(rows[target].number)
        else if (target !== root.focusIndex) root.focusAtIndex(target)
        else root.primaryAction(rows[target])
        return true
    }

    function chooseSource(volumeId) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            if (String(rows[i].id) === String(volumeId)) {
                root.sourcesRequested({ "volumeId": String(volumeId), "number": rows[i].number,
                                        "title": rows[i].title || "", "cover": rows[i].cover || "" })
                return
            }
        }
        root.sourcesRequested({ "volumeId": String(volumeId), "number": "", "title": "", "cover": "" })
    }

    function primaryAction(row) {
        var state = root.effectiveState(row)
        if (state === "ready") { root.openVolumeRequested(String(row.id)); return }
        if (root._inFlight(state)) return
        root.chooseSource(String(row.id))
    }

    function unownedIn(rows) {
        var out = []
        for (var i = 0; i < (rows || []).length; i++) {
            var n = Number(rows[i].number)
            if (isFinite(n) && !root.unavailableNumbers[n]) out.push(n)
        }
        return out
    }

    function requestNextMissing() {
        if (root.nextBatch.numbers.length) root.batchRequested(root.nextBatch.numbers, "Download next 10")
    }

    function selectionToken(number) { return String(number === undefined || number === null ? "" : number) }

    function selectNumber(number) {
        var n = root.selectionToken(number)
        if (typeof n === "string" && !n.length) return
        var out = root.selectedNumbers.slice(); var at = out.indexOf(n)
        if (at >= 0) out.splice(at, 1); else out.push(n)
        out.sort(function(a, b) {
            var na = Number(a), nb = Number(b), fa = isFinite(na), fb = isFinite(nb)
            if (fa && fb) return na - nb
            if (fa) return -1
            if (fb) return 1
            return String(a).localeCompare(String(b))
        })
        root.selectedNumbers = out
    }

    function ensureSelectedNumber(number) {
        var n = root.selectionToken(number)
        if (typeof n === "string" && !n.length) return
        if (root.selectedNumbers.indexOf(n) >= 0) return
        var out = root.selectedNumbers.slice(); out.push(n)
        out.sort(function(a, b) {
            var na = Number(a), nb = Number(b), fa = isFinite(na), fb = isFinite(nb)
            if (fa && fb) return na - nb
            if (fa) return -1
            if (fb) return 1
            return String(a).localeCompare(String(b))
        })
        root.selectedNumbers = out
    }

    function clearSelection() { root.selectedNumbers = []; root.selecting = false }
    function downloadSelected() {
        if (root.selectedNumbers.length) root.batchRequested(root.selectedNumbers.slice(), "Download selected")
    }

    // Compatibility jump API: still the semantic target the centring cursor reads, without
    // any second visual index surface.
    readonly property string currentJumpNumber: root.focusToken
    function jumpToNumber(number) { root.focusAtNumber(number) }

    readonly property var inFlightIds: {
        var out = [], rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) if (root._inFlight(root.effectiveState(rows[i]))) out.push(String(rows[i].id))
        return out
    }
    function cancelRemaining() {
        var s = root.serviceObject, ids = root.inFlightIds
        for (var i = 0; i < ids.length; i++) if (s && s.cancel) s.cancel(ids[i])
    }
    function cancelVolume(volumeId) { var s = root.serviceObject; if (s && s.cancel) s.cancel(String(volumeId)) }

    function _autoLand() {
        var rows = root.volumeRows || []
        if (!rows.length) return
        var n = root.initialFocusNumber(), idx = root.indexOfNumber(n)
        root.focusAtNumber(n)
        root._landedIndex = idx >= 0 ? idx : 0
    }

    Component.onCompleted: {
        root.refresh(); root.refreshResume(); root._autoLand()
    }
    onSeriesIdChanged: {
        root._resume = null
        root.selectedNumbers = []; root._landedIndex = -1; root._pageHomed = false
        root.refresh(); root.refreshResume(); root._autoLand()
    }
    onVolumeRowsChanged: {
        if (root.indexOfNumber(root.focusToken) < 0) root.focusAtNumber(root.initialFocusNumber())
        root._autoLand()
    }

    Connections {
        target: root.serviceObject
        ignoreUnknownSignals: true
        function onVolumesChanged(seriesId) { if (String(seriesId) === root.seriesId) root.refresh() }
        function onProgress(volumeId, done, total) {
            if (!root._ownsVolume(volumeId)) return
            var s = root.serviceObject
            var state = s && s.statusOf ? String(s.statusOf(volumeId).state || "downloading") : "downloading"
            if (state === "none" || state === "ready") state = "downloading"
            root.progressByVolume = root._reassign(root.progressByVolume, String(volumeId),
                ({ "done": done, "total": total, "state": state }))
        }
        function onFinished(volumeId) { root.clearProgress(String(volumeId)); root.refresh() }
        function onFailed(volumeId, reason) { root.clearProgress(String(volumeId)); root.refresh() }
        function onRemoved(volumeId) { root.clearProgress(String(volumeId)); root.refresh() }
        function onSynopsisReady(volumeId) { root.refresh() }
    }

    // Bridge automation surface (world-namespaced per the naming law). Plain scalars only,
    // per the Lanista ledger's qml-get vocabulary — catalogue-independence Slice 3,
    // 2026-08-20; preserved unchanged by the v2.3 flow adoption.
    Item {
        id: tankobanShelfState
        objectName: "tankobanShelfState"
        visible: false
        property int rowCount: root.volumeRows.length
        property int coveredCount: {
            var n = 0, rows = root.volumeRows || []
            for (var i = 0; i < rows.length; i++)
                if (root.coverFor(rows[i]).length > 0) n++
            return n
        }
    }

    // ------------------------------------------------------------------
    // Pages/Flow-derived Tankoban surface (v2.3). One continuous surface, no lanes, no
    // dividers: a whisper "VOLUMES N" label, the horizontal flow, and a shallow action bar.
    // The selected volume plus its caption always fits between them - never cropped, because
    // bookHeight is derived from the measured space actually left over (POLISH-DELTA #10).
    // ------------------------------------------------------------------

    readonly property int laneHeaderHeight: 44
    readonly property int actionBarHeight: root.showVolumes ? 52 : 0
    readonly property int captionHeight: 54
    // The Cover-Flow current-item scale. Named so bookHeight's own budget and
    // scaleForIndex's centre case read from one source instead of restating 1.10 twice.
    readonly property real currentItemScale: 1.10
    // v2.3's own rule (POLISH-DELTA #10): cover height is measured space, not a formula
    // fudge. flowViewport.height already excludes the lane header and the action bar (it is
    // anchored between them), so the only things this budget has to subtract are the
    // caption block and a small breathing margin; the current-item scale divides the whole
    // budget so the DRAWN size (height * currentItemScale) - not the base height - is what
    // actually has to fit.
    readonly property int bookHeight: Math.max(190, Math.min(276,
        Math.floor((flowViewport.height - root.captionHeight - 16) / root.currentItemScale)))
    readonly property int bookWidth: Math.round(root.bookHeight * 2 / 3)
    readonly property var currentRow:
        root.focusIndex >= 0 && root.focusIndex < root.volumeRows.length ? root.volumeRows[root.focusIndex] : null
    readonly property string currentActionLabel: {
        var row = root.currentRow
        if (!row) return ""
        var state = root.effectiveState(row)
        if (state === "ready") return "Read"
        if (root._inFlight(state)) {
            var f = root.progressFraction(row)
            return f >= 0 ? Math.round(f * 100) + "%" : "Working"
        }
        return state === "failed" ? "Retry" : "Get"
    }
    readonly property real flowViewportHeight: flowViewport.height
    readonly property real maxScaledVolumeHeight: (root.bookHeight + root.captionHeight) * root.currentItemScale

    function scaleForIndex(index) {
        var d = Math.abs(Math.round(index) - root.focusIndex)
        if (d === 0) return root.currentItemScale
        if (d === 1) return 1.00
        if (d === 2) return 0.95
        if (d === 3) return 0.90
        return 0.86
    }

    function centreFlow() {
        if (!volumeFlow || root.focusIndex < 0 || volumeFlow.count <= 0) return
        volumeFlow.currentIndex = root.focusIndex
        volumeFlow.forceLayout()
        volumeFlow.positionViewAtIndex(root.focusIndex, ListView.Center)
        volumeFlow.forceLayout()
        volumeFlow.positionViewAtIndex(root.focusIndex, ListView.Center)
    }

    function activateCurrent() {
        if (!root.currentRow) return
        root.primaryAction(root.currentRow)
    }

    // Long-series navigation (POLISH-DELTA #7): a 10-volume jump, clamped to the rails.
    function jumpBy(step) { root.focusAtIndex(root.focusIndex + step) }

    Item {
        id: flowHead
        objectName: "volumeFlowHead"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.showVolumes ? root.laneHeaderHeight : 0
        visible: root.showVolumes

        Row {
            anchors.left: parent.left
            anchors.leftMargin: theme.margin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 12
            Text {
                text: "VOLUMES"
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.4
            }
            Text {
                text: String(root.volumeRows.length)
                color: theme.ink
                font.family: theme.display; font.pixelSize: 15
            }
        }

        // Select-mode entry point (Hemanth greenlit KEEP-IT, 2026-08-14 handoff), transplanted
        // onto the v2.3 flow's lane header — still-live, separately-approved work the arc's
        // own candidate never saw. Every batch function it drives (`selecting`,
        // `selectedNumbers`, `downloadSelected`, `requestNextMissing`, the floating
        // "N selected" bar) is unchanged.
        Row {
            id: selectRow
            anchors.right: parent.right
            anchors.rightMargin: theme.margin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 13
            visible: root.volumeRows.length > 0
            spacing: 16
            Text {
                objectName: "volumeDownloadNextAction"
                visible: root.selecting
                text: "Download next 10"
                color: nextMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.6
                MouseArea {
                    id: nextMa
                    anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.requestNextMissing()
                }
            }
            Text {
                objectName: "volumeSelectToggle"
                text: root.selecting ? "Done" : "Select"
                color: selectMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.6
                MouseArea {
                    id: selectMa
                    anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.selecting) root.clearSelection()
                        else root.selecting = true
                    }
                }
            }
        }
    }

    Item {
        id: flowViewport
        anchors.top: flowHead.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: actionBar.top
        visible: root.showVolumes
        clip: true

        ListView {
            id: volumeFlow
            objectName: "volumeFlow"
            anchors.fill: parent
            orientation: ListView.Horizontal
            spacing: 18
            model: root.volumeRows
            cacheBuffer: Math.max(root.bookWidth * 4, 720)
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: false
            clip: true
            currentIndex: -1
            Keys.onLeftPressed: root.focusAtIndex(root.focusIndex - 1)
            Keys.onRightPressed: root.focusAtIndex(root.focusIndex + 1)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_PageDown) { root.jumpBy(10); event.accepted = true }
                else if (event.key === Qt.Key_PageUp) { root.jumpBy(-10); event.accepted = true }
                else if (event.key === Qt.Key_Home) { root.focusAtIndex(0); event.accepted = true }
                else if (event.key === Qt.Key_End) { root.focusAtIndex(root.volumeRows.length - 1); event.accepted = true }
            }
            onWidthChanged: Qt.callLater(root.centreFlow)

            // ADOPTION RISK (STATUS.md #4, unresolved by this adoption — needs a real mouse
            // to confirm): a WheelHandler with target:null layered on the Flickable-derived
            // ListView, so a plain notch steps one volume and Shift+notch steps ten, per
            // POLISH-DELTA #7 - instead of letting QQuickFlickable's own built-in wheel
            // scrolling free-flick the content. Qt6 pointer handlers get first look at an
            // event before an item's legacy wheelEvent(), which is the mechanism this relies
            // on.
            WheelHandler {
                id: flowWheel
                target: null
                onWheel: (event) => {
                    var step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
                    var dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                    if (dy < 0) root.jumpBy(step)
                    else if (dy > 0) root.jumpBy(-step)
                }
            }

            header: Item {
                width: Math.max(0, volumeFlow.width / 2 - root.bookWidth / 2 - 9)
                height: 1
            }
            footer: Item {
                width: Math.max(0, volumeFlow.width / 2 - root.bookWidth / 2 - 9)
                height: 1
            }

            delegate: Item {
                id: card
                // world-namespaced per-volume name (catalogue-independence Slice 3,
                // 2026-08-20; the committed Lanista scenarios click this exact stem) - never
                // the arc candidate's bare "volumeFlowTile".
                objectName: "tankobanVolumeCard_" + Vol.volumeToken(card.modelData)
                required property var modelData
                required property int index
                width: root.bookWidth + 18
                height: root.bookHeight + root.captionHeight
                y: Math.round((volumeFlow.height - height) / 2)
                scale: root.scaleForIndex(card.index)
                opacity: card.index === root.focusIndex ? 1.0 : 0.80
                z: card.index === root.focusIndex ? 4 : Math.max(0, 3 - Math.abs(card.index - root.focusIndex))
                transformOrigin: Item.Bottom
                Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 160 } }

                readonly property string cardState: root.effectiveState(card.modelData)
                readonly property real fraction: root.progressFraction(card.modelData)
                readonly property bool live: root._inFlight(card.cardState)
                readonly property string nameText: root.volumeNameFor(card.modelData)
                readonly property string stateText: root.stateLineFor(card.modelData)

                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Volume " + Vol.volumeToken(card.modelData)
                Keys.onReturnPressed: root.pressVolume(card.index)
                Keys.onEnterPressed: root.pressVolume(card.index)

                Rectangle {
                    id: coverBox
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.bookWidth
                    height: root.bookHeight
                    radius: 6
                    clip: true
                    color: theme.glassTint
                    border.width: card.index === root.focusIndex ? 2 : 1
                    border.color: card.index === root.focusIndex ? theme.gold
                        : (cardMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.34) : theme.edge)

                    Image {
                        id: coverImage
                        anchors.fill: parent
                        source: root.coverFor(card.modelData)
                        sourceSize: Qt.size(Math.ceil(width * 1.6), Math.ceil(height * 1.6))
                        asynchronous: true
                        cache: true
                        retainWhileLoading: true
                        fillMode: Image.PreserveAspectCrop
                        visible: status === Image.Ready
                    }
                    // NO COVER fallback (ruling #5): the app's existing glass language - a
                    // centred Fraunces numeral over a small letterspaced whisper.
                    Column {
                        anchors.centerIn: parent
                        visible: coverImage.status !== Image.Ready
                        spacing: 8
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Vol.volumeToken(card.modelData)
                            color: theme.inkDimmer
                            font.family: theme.display
                            font.pixelSize: 30
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "NO COVER"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                        }
                    }

                    // Owned mark (ruling #6): 2px gold on the cover's own bottom edge,
                    // label-free, only when this card is not the one already carrying the
                    // gold focus border.
                    Rectangle {
                        visible: card.cardState === "ready" && card.index !== root.focusIndex
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: theme.gold
                    }
                    Rectangle {
                        visible: card.live
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: Qt.rgba(1, 1, 1, 0.13)
                        Rectangle {
                            width: card.fraction >= 0 ? parent.width * card.fraction : parent.width * 0.15
                            height: parent.height
                            color: theme.gold
                        }
                    }
                }

                // Caption: VOL N, the real volume name when the catalogue has one (never a
                // redundant restatement of the number), and the acquisition state line.
                Column {
                    anchors.top: coverBox.bottom
                    anchors.topMargin: 9
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.bookWidth
                    spacing: 3

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "VOL " + Vol.volumeToken(card.modelData)
                        color: card.index === root.focusIndex ? theme.gold : theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 1.8
                    }
                    Text {
                        width: parent.width
                        visible: card.nameText.length > 0
                        // A fixed height (not implicitHeight) avoids a layout binding loop:
                        // Text's implicit height recomputes during the same pass an
                        // elide-driven height change would trigger, oscillating forever.
                        // 15px comfortably fits this font's single line.
                        height: visible ? 15 : 0
                        horizontalAlignment: Text.AlignHCenter
                        text: card.nameText
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        height: 12
                        horizontalAlignment: Text.AlignHCenter
                        text: card.stateText
                        color: card.live ? theme.gold : "#e6a3a3"
                        font.family: theme.ui
                        font.pixelSize: 10
                    }
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: card.forceActiveFocus()
                    onClicked: root.pressVolume(card.index)
                }

                Component.onCompleted: root.liveVolumeTiles += 1
                Component.onDestruction: root.liveVolumeTiles -= 1
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 120
            z: 8
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#050608" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 120
            z: 8
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#050608" }
            }
        }
    }

    Rectangle {
        id: actionBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: root.actionBarHeight
        visible: height > 0
        color: "transparent"
        border.width: 0

        Row {
            anchors.left: parent.left; anchors.leftMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter; spacing: 12
            Text {
                text: root.currentRow ? ("Vol. " + Vol.volumeToken(root.currentRow)) : ""
                color: theme.ink; font.family: theme.display; font.pixelSize: 18
            }
            Text {
                text: root.currentRow ? root.volumeNameFor(root.currentRow) : ""
                visible: text.length > 0
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
            }
        }

        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            width: actionBarText.implicitWidth + 36; height: 34; radius: 9
            color: root.currentRow && root.effectiveState(root.currentRow) === "ready"
                ? theme.glassTint : theme.gold
            border.width: root.currentRow && root.effectiveState(root.currentRow) === "ready" ? 1 : 0
            border.color: theme.edge
            opacity: root.currentRow && root._inFlight(root.effectiveState(root.currentRow)) ? 0.58 : 1.0
            Text {
                id: actionBarText; anchors.centerIn: parent
                text: root.currentActionLabel
                color: root.currentRow && root.effectiveState(root.currentRow) === "ready" ? theme.ink : "#171205"
                font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.currentRow && !root._inFlight(root.effectiveState(root.currentRow))
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.activateCurrent()
            }
        }
    }

    // Select-mode floating action bar. Reachable via the lane header's Select toggle above;
    // kept exactly as the pre-arc baseline had it.
    Rectangle {
        visible: root.selecting && root.selectedNumbers.length > 0
        z: 20; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        height: 42; width: selectedText.implicitWidth + downloadSelectedText.implicitWidth + 58; radius: 10
        color: Qt.rgba(0.03, 0.035, 0.045, 0.96); border.width: 1; border.color: theme.edge
        Text { id: selectedText; anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; text: root.selectedNumbers.length + " selected"; color: theme.ink; font.family: theme.ui; font.pixelSize: 11 }
        Rectangle { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; width: downloadSelectedText.implicitWidth + 20; height: 30; radius: 7; color: theme.gold
            Text { id: downloadSelectedText; anchors.centerIn: parent; text: "Download selected"; color: "#171205"; font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.downloadSelected() }
        }
    }
}
