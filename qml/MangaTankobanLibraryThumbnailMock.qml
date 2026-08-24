// MangaTankobanLibrary - the volume shelf for a Tankoban series.
//
// 2026-08-14 bookshelf rebuild (approved mock: colosseum-manga-series-bookshelf-mock.html).
// The shelf is a vertical cover grid that opens the page - every canonical volume is a
// card (cover + state chip + Vol/name/chapter-range caption). Chapters past the last
// mapped volume ("the X bucket", still computed by MangaReadingRoom via MangaVolumes.js)
// surface as a persistent "Latest chapters" tail below the grid, never a separate tab.
//
// `focusIndex`/`focusToken` and their small `focusAtNumber`/`focusAtIndex`/`jumpToNumber`
// API SURVIVE this rebuild - not as visual state (nothing highlights or centers on them
// any more) but because they are the load-bearing cursor for the cover-prefetch burst
// window (see `visibleRowsForCovers()` below). tests/manga_volume_cover_harness.qml pins
// the exact production bug this guards against: opening a 115-volume series must never
// fire a thumbnail scrape for every volume at once (2026-07-31, WeebCentral throttled and
// left the tail of the shelf permanently blank). Removing the cursor outright would silently
// reopen that bug, so it stays as a headless prefetch cursor only.
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
    property bool coverFetchingEnabled: true

    property var chapters: []
    property var chapterRows: []
    readonly property bool showVolumes: root.volumeRows.length > 0
    property var coverByVolume: ({})
    property var _thumbWanted: ({})
    property var _resume: null
    property var volumeRows: []
    property var progressByVolume: ({})

    // Kept for the service and batch contracts. The shelf owns the full canonical
    // model; legacy page groups are batch vocabulary only.
    readonly property int pageSize: 10
    readonly property var pagedRows: Vol.pageGroups(root.volumeRows, root.pageSize)
    property int activePage: 0
    readonly property var visibleRows: root.rowsOnPage(root.activePage)
    readonly property var activePageInfo:
        root.activePage >= 0 && root.activePage < root.pagedRows.length
            ? root.pagedRows[root.activePage] : null

    // Select-mode batch-download state (TB-002, 2026-07-30). FLAGGED in the 2026-08-14
    // rebuild handoff: the approved mock has no entry point for Select / "Download next
    // 10" any more, so nothing in the new grid can ever set `selecting` true. The state,
    // signal, and functions are kept alive untouched for the batch-download contract
    // (native service + tests/manga_volume_batch.* drive them directly), pending a
    // product call on where - if anywhere - they resurface visually.
    property bool selecting: false
    property bool _dragSelecting: false
    property var selectedNumbers: []

    // The prefetch cursor (see header note). No longer paints anything.
    property var focusNumber: 1
    property string focusToken: "1"
    readonly property int visibleContinuumCount: root.width > 1500 ? 11 : (root.width > 1180 ? 9 : 7)
    readonly property int focusIndex: root.indexOfNumber(root.focusToken)
    property int _landedIndex: -1
    readonly property int autoLandIndex: root._landedIndex
    readonly property int autoLandNumber: root.currentNumber

    // `renderedCount` remains the canonical-model count for the established batch
    // harness. `liveVolumeTiles` is the real delegate count and proves the grid is
    // virtualized. `flowCurrentIndex` mirrors the GridView's own currentIndex, kept
    // in sync with the prefetch cursor (no scrolling/highlight side effect).
    readonly property int renderedCount: root.volumeRows.length
    property int liveVolumeTiles: 0
    readonly property int flowCurrentIndex: volumeGrid ? volumeGrid.currentIndex : -1

    implicitWidth: 640
    implicitHeight: 480

    signal batchRequested(var numbers, string label)
    signal openVolumeRequested(string volumeId)
    signal sourcesRequested(var context)
    signal openChapterRequested(string chapterId, string label)
    signal chapterDownloadRequested(string chapterId, string label)

    Theme { id: theme }

    // ------------------------------------------------------------------
    // Native seams and canonical state
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

    // Compatibility page state remains available to batch logic and old
    // callers, but never controls the shelf's visual layout.
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

    // Kept for the resume/progress contract (still fed by continueFraction/isContinue
    // below); the 2026-08-14 mock caption is a fixed Vol/name/chapter-range triple with
    // no state-word slot, so this is no longer painted on a card. See handoff report.
    function stateWordFor(row) {
        switch (root.effectiveState(row)) {
        case "ready": return "On this device"
        case "resolving": return "Finding source"
        case "ingesting": return "Adding to library"
        case "packing": return "Building"
        case "downloading": return "Downloading"
        case "failed": return "Retry source"
        default: return "Available"
        }
    }

    function isContinue(row) {
        return root.continueVolumeId.length > 0
            && String(row && row.id) === root.continueVolumeId
            && root.continueFraction > 0.005 && root.continueFraction < 0.995
    }

    // Kept for the resume contract (see stateWordFor above) - no longer rendered on a
    // card caption by this file; the masthead's primary CTA still surfaces "Continue".
    function volumeCaptionFor(row) {
        var n = Vol.volumeToken(row)
        if (root.isContinue(row)) return "Continue - p. " + root.continuePage
        var f = root.progressFraction(row)
        if (root._inFlight(root.effectiveState(row)) && f >= 0)
            return "Downloading - " + Math.round(f * 100) + "%"
        if (root.effectiveState(row) === "failed") return "Retry source"
        if (root.effectiveState(row) === "ready") return "READ"
        return "OPEN"
    }

    function chapterSpanFor(row) {
        var first = row && row.chapterStart ? String(row.chapterStart) : ""
        var last = row && row.chapterEnd ? String(row.chapterEnd) : ""
        if (!first.length || !last.length) return ""
        return first === last ? "Chapter " + first : "Chapters " + first + "-" + last
    }

    // Mock caption format is "Ch a-b" (abbreviated, en dash) - reuses chapterSpanFor's
    // own first/last derivation, just reformatted to match the approved copy.
    function shelfRangeFor(row) {
        var span = root.chapterSpanFor(row)
        if (!span.length) return ""
        return span.replace(/^Chapters?\s+/, "Ch ").replace("-", "–")
    }

    // Mock chip vocabulary (Owned / Failed — in-flight moved OFF the chip). The
    // 2026-08-16 live-tile mock (colosseum-tankoban-series-volume-live-mock.html)
    // gives an acquiring volume its own top-right status disc (ring + %) plus the
    // gold caption, so the old top-left "Downloading" chip would duplicate it.
    function chipTextFor(row) {
        var state = root.effectiveState(row)
        if (state === "ready") return "Owned"
        if (state === "failed") return "Failed"
        return ""
    }

    // The live caption that REPLACES a tile's title while it acquires (approved
    // mock): phase word first, the count once bytes move. Empty = not in flight.
    function liveCaptionFor(row) {
        var state = root.effectiveState(row)
        if (!root._inFlight(state)) return ""
        var f = root.progressFraction(row)
        if (state === "resolving") return "Resolving…"
        if (state === "packing") return "Building…"
        if (state === "ingesting") return "Adding to library…"
        return f >= 0 ? ("Downloading · " + Math.round(f * 100) + "%") : "Downloading…"
    }

    // ------------------------------------------------------------------
    // Covers: fetched for a bounded window around the prefetch cursor, then kept
    // cached. See the file header note - this window is what keeps a 115-volume
    // series from bursting 115 thumbnail scrapes at once.
    // ------------------------------------------------------------------

    function _firstChapterIdIn(row, chs) {
        var lo = Number(row && row.chapterStart), hi = Number(row && row.chapterEnd)
        if (isNaN(lo) || isNaN(hi)) return ""
        var bestId = "", bestNumber = Infinity
        for (var i = 0; i < chs.length; i++) {
            var n = Number(chs[i].number)
            if (isNaN(n) || n < lo || n > hi) continue
            if (n < bestNumber) { bestNumber = n; bestId = String(chs[i].id || "") }
        }
        return bestId
    }

    function visibleRowsForCovers() {
        var rows = root.volumeRows || []
        if (!rows.length) return []
        var center = root.focusIndex >= 0 ? root.focusIndex : 0
        var radius = Math.ceil(root.visibleContinuumCount / 2) + 2
        var first = Math.max(0, center - radius)
        var last = Math.min(rows.length, center + radius + 1)
        return rows.slice(first, last)
    }

    // The vertical grid's actually-visible rows (+ a small buffer above/below), so the
    // cover-prefetch window FOLLOWS the scroll instead of the old horizontal focus cursor
    // (which sits at the top in a grid and left every row past the first blank). Still a
    // BOUNDED window -- never the whole shelf -- so the 115-volume throttle guard holds.
    function visibleGridRows() {
        var rows = root.volumeRows || []
        if (!rows.length) return []
        var cols = volumeGrid ? volumeGrid.columns : 1
        var ch = volumeGrid ? volumeGrid.cellHeight : 0
        if (cols < 1 || ch <= 0) return rows.slice(0, 24)
        var firstRow = Math.max(0, Math.floor(volumeGrid.contentY / ch) - 1)
        var rowSpan = Math.ceil(volumeGrid.height / ch) + 2
        var first = firstRow * cols
        var last = Math.min(rows.length, first + rowSpan * cols)
        return rows.slice(first, last)
    }

    function requestCovers(rowsOverride) {
        if (!root.coverFetchingEnabled) return
        var d = root.downloaderObject
        if (!d || !d.fetchThumb || !root.seriesId.length || !(root.chapters || []).length) return
        var wanted = {}
        for (var oldKey in root._thumbWanted) wanted[oldKey] = root._thumbWanted[oldKey]
        var rows = rowsOverride || root.visibleRowsForCovers()
        var chs = root.chapters || []
        for (var i = 0; i < rows.length; i++) {
            var vid = String(rows[i].id || "")
            if (!vid.length || root.coverByVolume[vid]) continue
            var cid = root._firstChapterIdIn(rows[i], chs)
            if (!cid.length || wanted[cid] === vid) continue
            wanted[cid] = vid
            d.fetchThumb(root.seriesId, cid)
        }
        root._thumbWanted = wanted
    }

    function coverFor(row) {
        var vid = String(row && row.id || "")
        if (root.effectiveState(row) === "ready") {
            var s = root.serviceObject
            if (s && s.localPages) {
                var pages = s.localPages(vid)
                if (pages && pages.length && pages[0].url) return String(pages[0].url)
            }
        }
        if (row && row.cover && String(row.cover).length) return String(row.cover)
        return root.coverByVolume[vid] || ""
    }

    Connections {
        target: root.downloaderObject
        ignoreUnknownSignals: true
        function onThumbReady(chapterId, url) {
            var cid = String(chapterId)
            var vid = root._thumbWanted[cid]
            if (!vid) return
            if (!url || !String(url).length) {
                var retry = {}
                for (var k in root._thumbWanted) if (k !== cid) retry[k] = root._thumbWanted[k]
                root._thumbWanted = retry
                return
            }
            root.coverByVolume = root._reassign(root.coverByVolume, vid, String(url))
        }
    }

    // ------------------------------------------------------------------
    // Prefetch cursor + selection + actions
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
        root.requestCovers()
    }

    function focusAtIndex(index) {
        var rows = root.volumeRows || []
        if (!rows.length) return
        var idx = Math.max(0, Math.min(rows.length - 1, Math.round(Number(index) || 0)))
        root.focusAtNumber(String(rows[idx].number !== undefined ? rows[idx].number : (idx + 1)))
    }

    // Headless activation by index - not called by any tap in the new grid (a real
    // pointer tap always goes straight to primaryAction, per the approved mock), but
    // kept for the Select-mode batch contract that already had no visual entry point
    // (see the `selecting` note above) and for programmatic/keyboard callers.
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

    // Compatibility jump API: still the semantic target the cover-prefetch cursor
    // reads, without any second visual index surface.
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
        root.refresh(); root.refreshResume()
        Qt.callLater(root.requestCovers); root._autoLand()
    }
    onSeriesIdChanged: {
        root.coverByVolume = ({}); root._thumbWanted = ({}); root._resume = null
        root.selectedNumbers = []; root._landedIndex = -1; root._pageHomed = false
        root.refresh(); root.refreshResume(); Qt.callLater(root.requestCovers); root._autoLand()
    }
    onVolumeRowsChanged: {
        if (root.indexOfNumber(root.focusToken) < 0) root.focusAtNumber(root.initialFocusNumber())
        Qt.callLater(root.requestCovers)
        root._autoLand()
    }
    onChaptersChanged: _coverPrefetchTimer.restart()
    onFocusNumberChanged: root.requestCovers()
    onFocusTokenChanged: root.requestCovers()

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

    // ------------------------------------------------------------------
    // The shelf - one continuous vertical scroll: grid header ("VOLUMES"), the
    // cover-card grid, then the "Latest chapters" tail. GridView.header/footer keep
    // this to ONE Flickable so the grid stays properly virtualized (liveVolumeTiles
    // stays < volumeRows.length even for a 115-volume series) while the header/tail
    // scroll with it, matching the mock's single-page flow.
    // ------------------------------------------------------------------

    // Debounce for the scroll-driven cover prefetch (see visibleGridRows / requestCovers):
    // a fast flick must not spam WeebCentral with a thumb request per frame.
    Timer {
        id: _coverPrefetchTimer
        interval: 120
        onTriggered: root.requestCovers(root.visibleGridRows())
    }

    GridView {
        id: volumeGrid
        objectName: "volumeShelfGrid"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 640
        model: root.volumeRows
        currentIndex: root.focusIndex
        highlightFollowsCurrentItem: false

        readonly property int columnGap: 14
        readonly property int rowGap: 18
        readonly property int captionHeight: 78
        property int columns: Math.max(1, Math.floor((width + columnGap) / (150 + columnGap)))
        cellWidth: width / Math.max(1, columns)
        cellHeight: Math.round((cellWidth - columnGap) * 1.5) + rowGap + captionHeight

        // Cover prefetch follows the scroll (debounced) + fills on initial layout/relayout.
        onContentYChanged: _coverPrefetchTimer.restart()
        onHeightChanged: _coverPrefetchTimer.restart()
        Component.onCompleted: _coverPrefetchTimer.restart()

        header: Component {
            Item {
                width: volumeGrid.width
                height: root.volumeRows.length > 0 ? 44 : 0
                Text {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
                    visible: root.volumeRows.length > 0
                    text: "VOLUMES"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.4
                }

                // Select-mode entry point (Hemanth greenlit KEEP-IT, 2026-08-14 handoff): the
                // rebuild dropped the old paneHeader that used to trigger batch download, but
                // every batch function it drove (`selecting`, `selectedNumbers`,
                // `downloadSelected`, `requestNextMissing`, the floating "N selected" bar) was
                // kept alive untouched. This is the ONE sanctioned addition back onto the mock's
                // otherwise-clean header: a plain gray text toggle, no glass, no border — reusing
                // the preserved functions as-is, nothing new wired into native.
                Row {
                    id: selectRow
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 13
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
        }

        delegate: Item {
            id: card
            objectName: "volumeTile"
            required property var modelData
            required property int index
            width: volumeGrid.cellWidth - volumeGrid.columnGap
            height: volumeGrid.cellHeight - volumeGrid.rowGap
            property string cardState: root.effectiveState(card.modelData)
            property real fraction: root.progressFraction(card.modelData)
            property string chipText: root.chipTextFor(card.modelData)
            property string spanText: root.shelfRangeFor(card.modelData)
            property string liveCaption: root.liveCaptionFor(card.modelData)
            readonly property bool live: root._inFlight(card.cardState)

            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Volume " + Vol.volumeToken(card.modelData)
            Keys.onReturnPressed: root.primaryAction(card.modelData)
            Keys.onEnterPressed: root.primaryAction(card.modelData)

            Rectangle {
                id: coverBox
                width: parent.width
                height: Math.round(width * 1.5)
                radius: 6
                clip: true
                color: theme.glassTint
                border.width: 1
                border.color: cardMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.34) : theme.edge

                Image {
                    id: coverImage
                    anchors.fill: parent
                    source: root.coverFor(card.modelData)
                    sourceSize: Qt.size(Math.ceil(width * 1.6), Math.ceil(height * 1.6))
                    asynchronous: true; cache: true; retainWhileLoading: true
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                }
                Column {
                    visible: coverImage.status !== Image.Ready
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Vol.volumeToken(card.modelData)
                        color: theme.inkDim; font.family: theme.display; font.pixelSize: 30
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "NO COVER"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.6
                    }
                }
                Rectangle {
                    visible: card.chipText.length > 0
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 7
                    radius: 9; height: chipLabel.implicitHeight + 6; width: chipLabel.implicitWidth + 16
                    color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: card.chipText
                        color: card.cardState === "ready" ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.2
                    }
                }
                // ── the live status disc (approved mock 2026-08-16): top-right pill
                // with a spinning ring while resolving/indeterminate, the gold %
                // once bytes move. This is what makes an acquiring tile readable
                // from across the shelf. ──
                Rectangle {
                    visible: card.live
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 7
                    radius: 10; height: 20
                    width: discRow.implicitWidth + 18
                    color: Qt.rgba(0.04, 0.045, 0.06, 0.82)
                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.55)
                    Row {
                        id: discRow
                        anchors.centerIn: parent
                        spacing: 4
                        Canvas {
                            id: discRing
                            visible: card.fraction < 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 11; height: 11
                            rotation: 0
                            RotationAnimation on rotation {
                                from: 0; to: 360; duration: 1150
                                loops: Animation.Infinite; running: discRing.visible
                            }
                            onVisibleChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.lineWidth = 1.8
                                ctx.strokeStyle = "#f0c44a"
                                ctx.beginPath()
                                ctx.arc(5.5, 5.5, 4, 0, Math.PI * 0.75)
                                ctx.stroke()
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.fraction >= 0 ? (Math.round(card.fraction * 100) + "%") : "···"
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                }
                // ── the breathing gold edge: an acquiring tile glows softly so the
                // eye finds it without reading anything (mock's breathe). ──
                Rectangle {
                    id: liveGlow
                    visible: card.live
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                    SequentialAnimation on opacity {
                        running: liveGlow.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.4; to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.0; to: 0.4; duration: 1200; easing.type: Easing.InOutSine }
                    }
                }
                Rectangle {
                    visible: card.live
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 3; color: Qt.rgba(1, 1, 1, 0.13)
                    Rectangle { width: Math.max(0, card.fraction) * parent.width; height: parent.height; color: theme.gold }
                }
            }

            Column {
                anchors.top: coverBox.bottom; anchors.topMargin: 8
                anchors.left: parent.left; anchors.right: parent.right
                spacing: 2
                Text {
                    text: "VOL " + Vol.volumeToken(card.modelData)
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.8
                }
                Text {
                    width: parent.width
                    text: card.liveCaption.length ? card.liveCaption : (card.modelData.title || "")
                    visible: text.length > 0
                    color: card.liveCaption.length ? theme.gold : theme.ink
                    font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                }
                Text {
                    text: card.spanText.length ? card.spanText : "chapters not mapped yet"
                    color: card.spanText.length ? theme.inkDim : theme.inkDimmer
                    font.italic: !card.spanText.length
                    font.family: theme.ui; font.pixelSize: 12
                }
            }

            MouseArea {
                id: cardMouse
                anchors.fill: parent
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onPressed: card.forceActiveFocus()
                onClicked: root.primaryAction(card.modelData)
            }

            Component.onCompleted: root.liveVolumeTiles += 1
            Component.onDestruction: root.liveVolumeTiles -= 1
        }

        footer: Component {
            Item {
                id: tailRoot
                width: volumeGrid.width
                visible: root.chapterRows.length > 0
                height: visible ? tailColumn.height : 0

                Column {
                    id: tailColumn
                    width: parent.width
                    topPadding: 40
                    spacing: 0

                    Item {
                        id: tailHeader
                        width: parent.width
                        height: 48

                        Text {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 12
                            text: "LATEST CHAPTERS"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.letterSpacing: 2.4
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 12
                            text: root.chapterRows.length + (root.chapterRows.length === 1 ? " chapter" : " chapters")
                                + (root.showVolumes ? " not yet collected into a volume" : "")
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: theme.edge
                        }
                    }

                    Column {
                        id: tailRows
                        width: parent.width

                        Repeater {
                            model: root.chapterRows

                            delegate: Item {
                                id: chapterRow
                                required property var modelData
                                required property int index
                                width: tailRows.width
                                height: 104

                                property string chapterId: String(chapterRow.modelData.id || "")
                                property string chapterLabel:
                                    (chapterRow.modelData.name && String(chapterRow.modelData.name).length)
                                        ? String(chapterRow.modelData.name)
                                        : ("Chapter " + String(chapterRow.modelData.number || ""))
                                property string chapterState: "none"
                                property int chapterDone: 0
                                property int chapterTotal: 0
                                property string liveThumb: ""

                                readonly property bool inFlight:
                                    chapterRow.chapterState === "downloading" || chapterRow.chapterState === "queued"
                                readonly property int progressPercent:
                                    chapterRow.chapterTotal > 0
                                        ? Math.max(0, Math.min(100, Math.round(chapterRow.chapterDone / chapterRow.chapterTotal * 100)))
                                        : 0
                                readonly property string thumbUrl: {
                                    if (chapterRow.chapterState === "done") {
                                        var localUrl = chapterRow.firstLocalUrl()
                                        if (localUrl.length) return localUrl
                                    }
                                    return chapterRow.liveThumb
                                }
                                readonly property string stateLabel:
                                    chapterRow.chapterState === "done" ? "ON DEVICE"
                                    : chapterRow.inFlight
                                        ? (chapterRow.progressPercent > 0 ? chapterRow.progressPercent + "%" : "DOWNLOADING")
                                    : chapterRow.chapterState === "failed" || chapterRow.chapterState === "error" ? "RETRY"
                                    : "GET"
                                readonly property string subtitle:
                                    chapterRow.chapterState === "done" ? "On this device"
                                    : chapterRow.inFlight
                                        ? (chapterRow.progressPercent > 0
                                            ? "Downloading " + chapterRow.progressPercent + "%"
                                            : "Downloading")
                                    : chapterRow.chapterState === "failed" || chapterRow.chapterState === "error"
                                        ? "Download failed"
                                    : "WeebCentral first page"
                                function firstLocalUrl() {
                                    var d = root.downloaderObject
                                    if (!d || !d.localPages) return ""
                                    var pages = d.localPages(chapterRow.chapterId)
                                    return (pages && pages.length) ? String(pages[0].url || "") : ""
                                }

                                function refreshState() {
                                    var d = root.downloaderObject
                                    if (!d || !d.statusOf) return
                                    var st = d.statusOf(chapterRow.chapterId)
                                    chapterRow.chapterState = String(st.state || "none")
                                    chapterRow.chapterDone = Number(st.done || 0)
                                    chapterRow.chapterTotal = Number(st.total || 0)
                                }

                                function requestThumb() {
                                    var d = root.downloaderObject
                                    if (d && d.fetchThumb && chapterRow.chapterId.length)
                                        d.fetchThumb(root.seriesId, chapterRow.chapterId)
                                }

                                function primary() {
                                    if (chapterRow.chapterState === "done") {
                                        root.openChapterRequested(chapterRow.chapterId, chapterRow.chapterLabel)
                                        return
                                    }
                                    if (!chapterRow.inFlight) {
                                        root.chapterDownloadRequested(chapterRow.chapterId, chapterRow.chapterLabel)
                                        chapterRow.chapterState = "downloading"
                                    }
                                }

                                Component.onCompleted: {
                                    chapterRow.refreshState()
                                    chapterRow.requestThumb()
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: chapterMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
                                }
                                Rectangle {
                                    visible: chapterRow.inFlight
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.topMargin: 11
                                    anchors.bottomMargin: 11
                                    width: 2
                                    color: theme.gold
                                }
                                Rectangle {
                                    visible: chapterRow.index < root.chapterRows.length - 1
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Qt.rgba(1, 1, 1, 0.07)
                                }

                                Item {
                                    id: chapterThumb
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 64
                                    height: 90

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6
                                        color: "#15171f"
                                        border.width: 1
                                        border.color: chapterRow.inFlight
                                            ? Qt.rgba(0.94, 0.77, 0.29, 0.62)
                                            : theme.edge

                                        Text {
                                            anchors.centerIn: parent
                                            visible: thumbImage.status !== Image.Ready
                                            text: String(chapterRow.modelData.number || "?")
                                            color: theme.inkDimmer
                                            font.family: theme.display
                                            font.pixelSize: 24
                                        }
                                    }

                                    Image {
                                        id: thumbImage
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        source: chapterRow.thumbUrl
                                        visible: status === Image.Ready
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        sourceSize.width: 180
                                    }
                                    Rectangle {
                                        visible: chapterRow.inFlight
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 3
                                        color: Qt.rgba(0, 0, 0, 0.45)

                                        Rectangle {
                                            width: chapterRow.progressPercent > 0
                                                ? parent.width * chapterRow.progressPercent / 100
                                                : parent.width * 0.15
                                            height: parent.height
                                            color: theme.gold
                                        }
                                    }
                                }

                                Column {
                                    anchors.left: chapterThumb.right
                                    anchors.leftMargin: 18
                                    anchors.right: chapterActions.left
                                    anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 7
                                    Text {
                                        width: parent.width
                                        text: chapterRow.chapterLabel
                                        color: chapterMouse.containsMouse ? theme.gold : theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: chapterRow.subtitle
                                        color: chapterRow.chapterState === "done" || chapterRow.inFlight
                                            ? theme.gold
                                            : theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }

                                Row {
                                    id: chapterActions
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 18

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: chapterRow.stateLabel
                                        color: chapterRow.chapterState === "done" || chapterRow.inFlight
                                            ? theme.gold
                                            : theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 1.1
                                    }

                                    Item {
                                        width: 38
                                        height: 38

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 19
                                            color: "transparent"
                                            border.width: 1
                                            border.color: actionMouse.containsMouse
                                                ? Qt.rgba(0.94, 0.77, 0.29, 0.55)
                                                : theme.edge
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: chapterRow.chapterState === "done" ? "✓"
                                                : chapterRow.inFlight ? "×"
                                                : chapterRow.chapterState === "failed" || chapterRow.chapterState === "error" ? "↻"
                                                : "↓"
                                            color: actionMouse.containsMouse ? theme.ink
                                                : chapterRow.chapterState === "done" || chapterRow.inFlight ? theme.gold
                                                : theme.inkDim
                                            font.pixelSize: 17
                                        }
                                        MouseArea {
                                            id: actionMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                var d = root.downloaderObject
                                                if (chapterRow.inFlight && d && d.cancelDownload)
                                                    d.cancelDownload(chapterRow.chapterId)
                                                else
                                                    chapterRow.primary()
                                            }
                                        }
                                    }
                                }

                                MouseArea {
                                    id: chapterMouse
                                    anchors.fill: parent
                                    anchors.rightMargin: 66
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: chapterRow.primary()
                                }

                                Connections {
                                    target: root.downloaderObject
                                    ignoreUnknownSignals: true

                                    function onProgress(cid, done, total) {
                                        if (String(cid) !== chapterRow.chapterId) return
                                        chapterRow.chapterState = "downloading"
                                        chapterRow.chapterDone = Number(done || 0)
                                        chapterRow.chapterTotal = Number(total || 0)
                                    }
                                    function onFinished(cid) {
                                        if (String(cid) !== chapterRow.chapterId) return
                                        chapterRow.chapterState = "done"
                                        chapterRow.refreshState()
                                    }
                                    function onRemoved(cid) {
                                        if (String(cid) !== chapterRow.chapterId) return
                                        chapterRow.chapterState = "none"
                                        chapterRow.chapterDone = 0
                                        chapterRow.chapterTotal = 0
                                        chapterRow.liveThumb = ""
                                        chapterRow.requestThumb()
                                    }
                                    function onFailed(cid, reason) {
                                        if (String(cid) !== chapterRow.chapterId) return
                                        chapterRow.chapterState = "failed"
                                    }
                                    function onThumbReady(cid, url) {
                                        if (String(cid) === chapterRow.chapterId && String(url || "").length)
                                            chapterRow.liveThumb = String(url)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Select-mode floating action bar. Unreachable today (see the `selecting` note
    // above - nothing in the new grid can ever flip it true), kept only so the
    // batch-download contract has somewhere to land if a future entry point is added.
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
