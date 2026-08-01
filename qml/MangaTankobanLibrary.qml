// MangaTankobanLibrary — the volume shelf. A series IS its volumes here: EVERY
// canonical volume the service knows renders as a row.
//
// BUILT TO THEATRE'S EPISODE ROW (qml/TheatreSeries.qml ~1502). A tankobon is the
// same kind of object as an episode — a numbered instalment you own, are part-way
// through, or have not fetched yet — so the shelf wears Theatre's anatomy exactly:
// a 70px number rail with a hairline, the artwork, name + meta, an uppercase status,
// and circular actions on the right. Continue is Theatre's "Next up": taller, gold
// rail, gold number.
//
// The ONE adaptation that is not a copy: Theatre's still is LANDSCAPE (172x96) and
// a tankobon is portrait, so Theatre is the wrong yardstick for the artwork — the
// aspect ratios are not comparable, and no size taken from it can be right.
//
// The book is the HERO of this surface, not a thumbnail of one: 220x320 in a 356px
// row (268x390 in 428 on Continue), roughly a real tankobon's 1:1.46. Sized to be
// looked at, on Hemanth's call — a paged shelf (10 volumes at a time) is coming, so
// row height is no longer paying for scroll length. A chapter is
// a fragment and wears a 100x140 thumbnail; a volume is the whole object you are
// collecting, and it is sized to be looked at. The rail, title and blurb all scale
// with it so nothing beside the cover reads as a footnote.
//
// The signature: OWNERSHIP IS DRAWN AS A SPINE. The rail's hairline thickens into a
// gold rule and the cover picks up a lit left edge once the volume is on disk, so a
// shelf you own reads as a run of book spines. It encodes the one thing this product
// is about — collecting books — rather than decorating the row.
//
// Service seam: in the app `service` is the native `TankobanVolumes` context
// property; the offscreen harness injects a fake exposing the same API. All calls
// resolve through `serviceObject`, falling back to the context property so the
// running app needs no wiring. `progress` is the same seam over ProgressStore.
import QtQuick
import "MangaVolumes.js" as Vol

Item {
    id: root
    objectName: "volumeShelf"

    property string seriesId: ""
    // Injection seam: the harness assigns a fake; the app leaves this null and the
    // calls fall through to the native TankobanVolumes context property.
    property var service: null
    readonly property var serviceObject: root.service
        ? root.service
        : ((typeof TankobanVolumes !== "undefined") ? TankobanVolumes : null)
    // Same injection seam over ProgressStore, for the Continue row.
    property var progress: null
    readonly property var progressObject: root.progress
        ? root.progress
        : ((typeof Progress !== "undefined") ? Progress : null)

    // ── VOLUME COVERS ────────────────────────────────────────────────────────
    // A volume's cover is the FIRST PAGE OF ITS FIRST CHAPTER — the same thing a
    // chapter row shows, fetched the same way. Chapter thumbnails are NOT in
    // WeebCentral's chapter-list HTML; the app scrapes each chapter's first page
    // on demand through Downloads.fetchThumb -> thumbReady(chapterId, url). So the
    // page hands us the live chapter list and we ask for one thumb per volume.
    //
    // No cover is fetched from MangaDex or Comick: MangaDex is retired, and Comick
    // serves exactly one cover per SERIES (the latest volume), not one per volume —
    // probed 2026-07-30 across four endpoint shapes.
    property var chapters: []
    property var downloader: null
    readonly property var downloaderObject: root.downloader
        ? root.downloader
        : ((typeof Downloads !== "undefined") ? Downloads : null)
    // volumeId -> scraped first-page url
    property var coverByVolume: ({})
    // chapterId -> volumeId, so a thumbReady can be routed back to its volume
    property var _thumbWanted: ({})

    // Ask for one thumbnail per volume: the first chapter that falls inside its
    // range. Only for the volumes ON SCREEN — see below.
    //
    // ⚠ WHY visibleRows AND NOT volumeRows (eyes-on 2026-07-31). Asking for every
    // volume queued 115 WeebCentral scrapes for One Piece the moment the page
    // opened. They run 3 at a time (MangaDownloader::THUMB_CONCURRENCY), the
    // chapter rows' own thumbnails queue BEHIND all of them, and WeebCentral
    // throttles under that burst. Volumes 101/102/110 and the Latest-chapters rows
    // came back empty and stayed empty. A paged shelf only ever shows ten, so ask
    // for ten: covers now arrive for what he is actually looking at, and the
    // chapter thumbnails are not starved behind a queue of a hundred.
    function requestCovers() {
        var d = root.downloaderObject
        if (!d || !d.fetchThumb || !root.seriesId.length) return
        var chs = root.chapters || []
        if (!chs.length) return
        // rowsOnPage(), never the visibleRows binding — see the note on rowsOnPage.
        var rows = root.rowsOnPage(root.activePage)
        // ACCUMULATE, never replace: volumes and chapters land at different times, so
        // this runs more than once per series. Rebuilding the map would orphan the
        // requests still in flight from the previous call and those covers would
        // never arrive.
        var wanted = {}
        for (var k in root._thumbWanted) wanted[k] = root._thumbWanted[k]
        for (var i = 0; i < rows.length; i++) {
            var vid = String(rows[i].id || "")
            if (!vid.length || root.coverByVolume[vid]) continue
            var cid = root._firstChapterIdIn(rows[i], chs)
            if (!cid.length || wanted[cid] === vid) continue   // already asked
            wanted[cid] = vid
            d.fetchThumb(root.seriesId, cid)
        }
        root._thumbWanted = wanted
    }
    // the lowest-numbered live chapter inside [chapterStart, chapterEnd]
    function _firstChapterIdIn(row, chs) {
        var lo = Number(row.chapterStart), hi = Number(row.chapterEnd)
        if (isNaN(lo) || isNaN(hi)) return ""
        var bestId = "", bestNum = Infinity
        for (var i = 0; i < chs.length; i++) {
            var n = Number(chs[i].number)
            if (isNaN(n) || n < lo || n > hi) continue
            if (n < bestNum) { bestNum = n; bestId = String(chs[i].id || "") }
        }
        return bestId
    }

    // The reader records ONE record per series under kind "tankoban", carrying the
    // volume it was left in (chapterId) plus page/max — so "where am I" is a single
    // lookup, not a scan. Re-read whenever the series changes or a record lands.
    property var _resume: null
    function refreshResume() {
        var p = root.progressObject
        root._resume = (p && root.seriesId.length) ? p.get("tankoban", root.seriesId) : null
    }
    readonly property string continueVolumeId:
        (root._resume && root._resume.chapterId) ? String(root._resume.chapterId) : ""
    readonly property int continuePage: (root._resume && root._resume.page) ? Number(root._resume.page) : 0
    readonly property int continueMax: (root._resume && root._resume.max) ? Number(root._resume.max) : 0
    readonly property real continueFraction:
        root.continueMax > 0 ? Math.max(0, Math.min(1, root.continuePage / root.continueMax)) : 0

    // canonical model — every volume the service knows for this series
    property var volumeRows: []
    // the app's owned/watched ink, matching Theatre's watched state
    readonly property color ownedInk: "#8fd6a4"
    // per-volume live download progress { volumeId: {done,total,state} } (reassigned to stay reactive)
    property var progressByVolume: ({})

    // inspection: how many volume rows the Repeater actually instantiated
    readonly property int renderedCount: rowsRepeater.count

    // ── the shelf pages in tens (design 2026-07-30) ───────────────────────────
    // A PAGE IS A SEASON. Theatre shows one season of episodes at a time and
    // switches with a selector; a 115-volume series is exactly that problem, so
    // the shelf shows ONE page of ten and switches the same way — pills up to ten
    // pages, a dropdown beyond, Theatre's own threshold (TheatreSeries.qml:1077,
    // :1168). Only the active page's rows are ever instantiated, so `renderedCount`
    // means "rows on screen now" — which is what it always measured.
    readonly property int pageSize: 10
    readonly property var pagedRows: Vol.pageGroups(root.volumeRows, root.pageSize)
    property int activePage: 0
    property bool pageMenuOpen: false
    readonly property var activePageInfo:
        (root.activePage >= 0 && root.activePage < root.pagedRows.length)
            ? root.pagedRows[root.activePage] : null
    // The rows of a given page, resolved on demand. IMPERATIVE CODE MUST USE THIS,
    // not the `visibleRows` binding below: a property-change handler can run BEFORE
    // the bindings that depend on that property have re-evaluated, so reading
    // `visibleRows` inside onActivePageChanged returns the page he just LEFT.
    // (Caught by manga_volume_cover_harness: turning to page 2 asked for page 1
    // again and page 2's covers never loaded.) pagedRows depends only on
    // volumeRows/pageSize, so it is always current here.
    function rowsOnPage(idx) {
        var pages = root.pagedRows || []
        return (idx >= 0 && idx < pages.length) ? pages[idx].volumes : []
    }
    readonly property var visibleRows: root.rowsOnPage(root.activePage)
    // What the active page's own button would fetch (see root.unownedIn).
    readonly property var activePageUnowned:
        root.activePageInfo ? root.unownedIn(root.activePageInfo.volumes) : []

    // Which page holds a given volume number, or -1.
    function pageIndexOf(number) {
        var n = Number(number)
        if (!isFinite(n)) return -1
        for (var i = 0; i < root.pagedRows.length; i++) {
            var vs = root.pagedRows[i].volumes
            for (var j = 0; j < vs.length; j++)
                if (Number(vs[j].number) === n) return i
        }
        return -1
    }

    // Open on the page he is READING, once — the same instinct as Theatre resuming
    // on the season you were watching. Only on the first load of a series: after
    // that the page is HIS choice and a background refresh must never yank him
    // back (volumeRows re-publishes on every progress tick).
    property bool _pageHomed: false
    function _homeActivePage() {
        if (root._pageHomed || !root.pagedRows.length) return
        var idx = root.pageIndexOf(root.currentNumber)
        root.activePage = idx >= 0 ? idx : 0
        root._pageHomed = true
    }
    // (the handlers that drive this live with the other lifecycle handlers below —
    //  QML allows exactly one onXChanged per property per object)
    // A batch was asked for: the volume NUMBERS it covers plus a human label.
    signal batchRequested(var numbers, string label)

    // { <volumeNumber>: true } for every volume a batch must NOT ask for: already
    // on the device, or already coming. Owned is the hard fence (spec: never
    // re-download); in-flight is here because the service rejects a second request
    // for a live volume ("Already acquiring this volume."), and a button that
    // names volumes already downloading would earn that error ten times over.
    // Reads the SAME live progress the tiles read, so button and tiles agree.
    readonly property var unavailableNumbers: {
        var out = {}
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            var n = Number(rows[i].number)
            if (!isFinite(n)) continue
            var live = root.progressByVolume[String(rows[i].id)]
            var st = (live !== undefined && live !== null)
                     ? String(live.state || "downloading")
                     : String(rows[i].state || "none")
            if (st === "ready" || root._inFlight(st)) out[n] = true
        }
        return out
    }

    // How many volumes are genuinely ON THIS DEVICE. Distinct from
    // unavailableNumbers, which also covers volumes merely on their way — this one
    // is what the header counts, so an in-flight volume must not inflate it.
    readonly property int ownedCount: {
        var n = 0
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].state) === "ready") n++
        return n
    }

    // The volume the reader is in, as a NUMBER (0 when the series was never
    // opened). continueVolumeId is an id; map it back through the rows.
    readonly property int currentNumber: {
        var rows = root.volumeRows || []
        if (!root.continueVolumeId.length) return 0
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === root.continueVolumeId)
                return Number(rows[i].number) || 0
        return 0
    }

    // The one press: the next `pageSize` volumes he does not have, walking FORWARD
    // from where he is reading. Holes behind him are left alone — the per-page
    // buttons are how those get filled deliberately (Hemanth, 2026-07-30).
    readonly property var nextBatch: Vol.nextBatch(root.volumeRows,
                                                   root.unavailableNumbers,
                                                   root.currentNumber, root.pageSize)

    // The volume NUMBERS a batch over `rows` would actually fetch: the rows minus
    // anything already here or already coming. Empty → that button has nothing to
    // do and hides. A function (not a per-delegate property) so the offscreen
    // harness can assert it without reaching inside a delegate; QML still tracks
    // unavailableNumbers as a binding dependency through the call.
    function unownedIn(rows) {
        var out = []
        var vs = rows || []
        for (var i = 0; i < vs.length; i++) {
            var n = Number(vs[i].number)
            if (isFinite(n) && !root.unavailableNumbers[n]) out.push(n)
        }
        return out
    }

    // Opening a downloaded volume in the reader is a later layer; surfaced as a
    // signal so the page can wire it without this surface knowing about readers.
    signal openVolumeRequested(string volumeId)
    // "Choose source" on an undownloaded volume raises this with the volume's
    // identity; MangaSeries merges the series id/title and opens the full-screen
    // MangaTankobanSourcesPage. This surface never renders the picker itself.
    signal sourcesRequested(var context)

    Theme { id: theme }

    implicitHeight: listCol.height
    height: listCol.height

    Component.onCompleted: { root.refresh(); root.refreshResume(); root.requestCovers() }
    onSeriesIdChanged: {
        root.coverByVolume = ({})       // covers belong to the OLD series — drop them
        root._thumbWanted = ({})
        root._pageHomed = false         // …and so does the page he was on
        root.activePage = 0
        root.pageMenuOpen = false
        root.refresh(); root.refreshResume(); root.requestCovers()
    }
    // volumes and chapters arrive from different sources at different times; ask
    // again whenever either lands, since a cover needs both.
    onVolumeRowsChanged: { root._homeActivePage(); root.requestCovers() }
    // Turning the page is exactly when the next ten covers are needed — and it is
    // also the retry, for any that a throttled scrape left empty last time.
    onActivePageChanged: root.requestCovers()
    onChaptersChanged: root.requestCovers()

    // route a scraped first-page url back to the volume that asked for it
    Connections {
        target: root.downloaderObject
        ignoreUnknownSignals: true
        function onThumbReady(chapterId, url) {
            var cid = String(chapterId)
            var vid = root._thumbWanted[cid]
            if (!vid) return
            if (!url || !String(url).length) {
                // An empty answer is NOT proof this volume has no cover — under a
                // burst WeebCentral throttles and the scrape simply fails. Forget
                // that we asked, so coming back to this page asks again instead of
                // showing a numbered placeholder for the rest of the session.
                var w = {}
                for (var k in root._thumbWanted) if (k !== cid) w[k] = root._thumbWanted[k]
                root._thumbWanted = w
                return
            }
            root.coverByVolume = root._reassign(root.coverByVolume, vid, String(url))
        }
    }

    // A downloaded volume shows its OWN first page — the real book, not a stand-in.
    // Falls back to the scraped first chapter page, then to the numbered placeholder.
    function coverFor(row) {
        var vid = String(row.id || "")
        if (String(row.state) === "ready") {
            var s = root.serviceObject
            if (s && s.localPages) {
                var lp = s.localPages(vid)
                if (lp && lp.length && lp[0].url) return String(lp[0].url)
            }
        }
        if (row.cover && String(row.cover).length) return String(row.cover)
        return root.coverByVolume[vid] || ""
    }
    // Coming back from the reader must move the Continue row, so re-read on reveal.
    onVisibleChanged: if (visible) root.refreshResume()

    function refresh() {
        var s = root.serviceObject
        root.volumeRows = s ? s.volumesForSeries(root.seriesId) : []
    }
    function _ownsVolume(vid) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (String(rows[i].id) === String(vid)) return true
        return false
    }
    function _reassign(map, key, value) {
        var m = {}
        for (var k in map) m[k] = map[k]
        m[key] = value
        return m
    }
    function _inFlight(st) {
        return st === "resolving" || st === "downloading" || st === "ingesting" || st === "packing"
    }
    function clearProgress(vid) {
        if (root.progressByVolume[vid] === undefined) return
        var m = {}
        for (var k in root.progressByVolume) if (k !== vid) m[k] = root.progressByVolume[k]
        root.progressByVolume = m
    }
    // "Choose source" on an undownloaded volume -> emit the volume's identity so
    // MangaSeries opens the full-screen sources picker. The picker (not this surface)
    // kicks the native searchSources; the row just reflects the resulting in-flight state.
    function chooseSource(vid) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            if (String(rows[i].id) === String(vid)) {
                root.sourcesRequested({
                    "volumeId": String(vid),
                    "number": rows[i].number,
                    "title": (rows[i].title && String(rows[i].title).length) ? String(rows[i].title) : "",
                    "cover": rows[i].cover ? String(rows[i].cover) : ""
                })
                return
            }
        }
        // Volume not in the current rows (e.g. a reader escape before refresh) — still
        // open the picker keyed on the id; identity fields fill in when volumes land.
        root.sourcesRequested({ "volumeId": String(vid), "number": "", "title": "", "cover": "" })
    }
    // The single per-row action, dispatched off the volume's live (effective) state.
    function primaryAction(rowItem) {
        var st = String(rowItem.effectiveState)
        var vid = rowItem.volumeId
        if (st === "ready") { root.openVolumeRequested(vid); return }
        if (root._inFlight(st)) {
            var s = root.serviceObject
            if (s) s.cancel(vid)
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.cancel(vid)
            return
        }
        root.chooseSource(vid)
    }

    // ── Cancel remaining (design 2026-07-30 §3) ──────────────────────────────
    // Every volume of this series still on its way. A batch is not a transaction,
    // so "remaining" is simply "not finished yet" — a volume that already landed
    // is READY, is not in this list, and is therefore never touched by the cancel.
    readonly property var inFlightIds: {
        var out = []
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            var vid = String(rows[i].id || "")
            if (!vid.length) continue
            var live = root.progressByVolume[vid]
            var st = (live !== undefined && live !== null)
                     ? String(live.state || "downloading")
                     : String(rows[i].state || "none")
            if (root._inFlight(st)) out.push(vid)
        }
        return out
    }

    // Stop everything still queued, keep everything already downloaded. Reuses the
    // SAME per-volume cancel the row's own stop action uses — no batch-level
    // teardown, so there is nothing new that could take a finished volume with it.
    function cancelRemaining() {
        var s = root.serviceObject
        var ids = root.inFlightIds
        for (var i = 0; i < ids.length; i++) {
            if (s) s.cancel(ids[i])
            else if (typeof TankobanVolumes !== "undefined") TankobanVolumes.cancel(ids[i])
        }
    }

    Connections {
        target: root.serviceObject
        ignoreUnknownSignals: true
        function onVolumesChanged(sid) { if (sid === root.seriesId) root.refresh() }
        function onProgress(vid, done, total) {
            if (!root._ownsVolume(vid)) return
            var s = root.serviceObject
            var st = (s && s.statusOf) ? String(s.statusOf(vid).state || "downloading") : "downloading"
            if (st === "none" || st === "ready") st = "downloading"
            root.progressByVolume = root._reassign(root.progressByVolume, vid,
                { "done": done, "total": total, "state": st })
        }
        function onFinished(vid) { root.clearProgress(vid); root.refresh() }
        function onFailed(vid, reason) { root.clearProgress(vid); root.refresh() }
        function onRemoved(vid) {
            root.clearProgress(vid)
            root.refresh()
        }
        function onSynopsisReady(vid) { root.refresh() }
    }

    Column {
        id: listCol
        width: root.width
        spacing: 0

        // ── the page selector — Theatre's season strip, one medium over ───────
        // Up to ten pages ride a horizontal pill strip; beyond that a dropdown,
        // because the strip has no sideways-browsing affordance in this app.
        // Both are lifted from TheatreSeries.qml (:1077 dropdown, :1168 strip).
        Item {
            id: pagerRow
            objectName: "shelfPager"
            width: listCol.width
            height: root.pagedRows.length > 1 ? 62 : 0
            visible: height > 0
            z: 40

            // ≤10 pages: the strip.
            Flickable {
                anchors.fill: parent
                visible: root.pagedRows.length <= 10
                contentWidth: pillRow.width + theme.margin
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: pillRow
                    x: theme.margin
                    spacing: 22
                    topPadding: 18
                    Repeater {
                        model: root.pagedRows
                        // Delegate root is an Item, NOT the Column: a MouseArea with
                        // anchors.fill inside a positioner is ignored (0x0, dead clicks).
                        delegate: Item {
                            id: pageBtn
                            required property var modelData
                            required property int index
                            width: pageCol.width
                            height: pageCol.height
                            property bool on: root.activePage === pageBtn.index
                            Column {
                                id: pageCol
                                spacing: 5
                                Text {
                                    text: pageBtn.modelData.first + "–" + pageBtn.modelData.last
                                    color: pageBtn.on ? theme.gold
                                         : (pageMa.containsMouse ? theme.ink : theme.inkDim)
                                    font.family: theme.ui
                                    font.pixelSize: 15
                                    font.weight: pageBtn.on ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    visible: pageBtn.on
                                    width: 26; height: 2; radius: 2
                                    color: theme.gold
                                }
                            }
                            MouseArea {
                                id: pageMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.activePage = pageBtn.index
                            }
                        }
                    }
                }
            }

            // 11+ pages: the dropdown.
            Rectangle {
                id: pageTrigger
                x: theme.margin
                anchors.verticalCenter: parent.verticalCenter
                visible: root.pagedRows.length > 10
                width: pageTrigT.implicitWidth + 52
                height: 38
                radius: 19
                color: pageTrigMa.containsMouse || root.pageMenuOpen
                       ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: root.pageMenuOpen ? theme.gold : theme.edge
                Text {
                    id: pageTrigT
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.activePageInfo ? root.activePageInfo.label : ""
                    color: theme.ink
                    font.family: theme.ui; font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: root.pageMenuOpen ? theme.gold : theme.inkDim
                    font.pixelSize: 12
                }
                MouseArea {
                    id: pageTrigMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pageMenuOpen = !root.pageMenuOpen
                }
            }
            Item {
                // zero-height overlay host: the menu floats, never reflows the shelf
                x: theme.margin
                anchors.top: pageTrigger.bottom
                anchors.topMargin: 8
                width: 236
                height: 0
                Rectangle {
                    width: parent.width
                    height: Math.min(304, pageMenuList.contentHeight + 12)
                    visible: root.pageMenuOpen
                    radius: 14
                    color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
                    border.width: 1
                    border.color: theme.edge
                    ListView {
                        id: pageMenuList
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: root.pagedRows
                        boundsBehavior: Flickable.StopAtBounds
                        delegate: Rectangle {
                            id: pmRow
                            required property var modelData
                            required property int index
                            width: pageMenuList.width
                            height: 36
                            radius: 9
                            color: pmMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                            Text {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: pmRow.modelData.label
                                color: root.activePage === pmRow.index ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: root.activePage === pmRow.index
                                             ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                id: pmMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.activePage = pmRow.index
                                    root.pageMenuOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── the ledger header — Theatre's episodeLedgerHeader (TheatreSeries.qml
        //    :1229), one medium over. The page's name and tally on the left, the
        //    download action as a glass tablet on the right, exactly where
        //    "Download season" sits on a show page (Hemanth, eyes-on 2026-07-31).
        Item {
            id: volumeLedgerHeader
            objectName: "shelfLedger"
            x: theme.margin
            width: listCol.width - 2 * theme.margin
            height: root.pagedRows.length ? 86 : 0
            visible: height > 0

            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                Text {
                    text: root.activePageInfo ? root.activePageInfo.label : ""
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }
                Text {
                    text: {
                        var vs = root.visibleRows
                        if (!vs.length) return ""
                        var owned = 0, coming = 0
                        for (var i = 0; i < vs.length; i++) {
                            var live = root.progressByVolume[String(vs[i].id)]
                            var st = (live !== undefined && live !== null)
                                     ? String(live.state || "downloading")
                                     : String(vs[i].state || "none")
                            if (st === "ready") owned++
                            else if (root._inFlight(st)) coming++
                        }
                        var parts = [vs.length + (vs.length === 1 ? " book" : " books")]
                        if (owned > 0) parts.push(owned + " on this device")
                        if (coming > 0) parts.push(coming + " downloading")
                        return parts.join("  /  ")
                    }
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.letterSpacing: 0.3
                }
            }

            // Stop what is still coming, keep what already landed. Sits left of the
            // download tablet and only exists while something is genuinely in flight.
            Rectangle {
                id: cancelRemainingAction
                objectName: "cancelRemainingAction"   // geometry is asserted offscreen
                visible: root.inFlightIds.length > 0
                // Anchored to the download tablet itself, NOT to its `visible` —
                // `visible` is inherited from ancestors, so keying layout off it
                // makes the two actions overlap in any context where the shelf is
                // not yet shown. The tablet is present whenever a page exists, and
                // this control only appears when volumes are in flight.
                anchors.right: pageDownloadAction.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 168
                height: 40
                radius: 9
                color: cancelMa.containsMouse ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: theme.edge
                Row {
                    anchors.centerIn: parent
                    spacing: 9
                    PlayerIcon {
                        width: 16; height: 16
                        kind: "cancel"
                        ink: cancelMa.containsMouse ? "#e6a3a3" : theme.inkDim
                    }
                    Text {
                        text: "Cancel remaining"
                        color: cancelMa.containsMouse ? "#e6a3a3" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cancelRemaining()
                }
            }

            // Theatre's "Download season", one medium over: acquire every volume of
            // the ACTIVE page that is not already here or already coming. It always
            // names its range, so it can never surprise him about what it fetches.
            Rectangle {
                id: pageDownloadAction
                objectName: "pageDownloadAction"   // geometry is asserted offscreen
                visible: root.pagedRows.length > 0 && root.activePageInfo !== null
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 176
                height: 40
                radius: 9
                readonly property bool armed: root.activePageUnowned.length > 0
                color: dlPageMa.containsMouse && pageDownloadAction.armed
                       ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: theme.edge
                Row {
                    anchors.centerIn: parent
                    spacing: 9
                    PlayerIcon {
                        width: 16; height: 16
                        kind: pageDownloadAction.armed ? "download" : "check"
                        ink: pageDownloadAction.armed ? theme.inkDim : root.ownedInk
                    }
                    Text {
                        text: !pageDownloadAction.armed ? "On this device"
                            : ("Download " + root.activePageInfo.first
                               + "–" + root.activePageInfo.last)
                        color: !pageDownloadAction.armed ? theme.inkDim
                             : (dlPageMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.ui; font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    id: dlPageMa
                    anchors.fill: parent
                    enabled: pageDownloadAction.armed
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.batchRequested(root.activePageUnowned,
                                                   root.activePageInfo.label)
                }
            }
        }

        Repeater {
            id: rowsRepeater
            model: root.visibleRows

            delegate: Item {
                id: vrow
                required property var modelData
                width: listCol.width
                implicitHeight: rowMain.height
                height: rowMain.height

                readonly property string volumeId: String(modelData.id || "")
                readonly property var prog: root.progressByVolume[volumeId]
                // effective state: index/acquisition state, but any LIVE progress means
                // this row is acquiring even before volumeRows refreshes.
                readonly property string effectiveState: (prog !== undefined && prog !== null)
                    ? String(prog.state || "downloading")
                    : String(modelData.state || "none")
                readonly property string synopsisText:
                    (modelData.synopsis && String(modelData.synopsis).length) ? String(modelData.synopsis) : ""

                // Theatre's "Next up", one medium over: the volume the reader was left
                // in, taller and gold. Only while it is genuinely part-read — a finished
                // volume is not something to continue.
                readonly property bool isContinue: root.continueVolumeId.length > 0
                    && root.continueVolumeId === vrow.volumeId
                    && root.continueFraction > 0.005 && root.continueFraction < 0.995
                readonly property bool owned: vrow.effectiveState === "ready"
                // one dim uppercase line, exactly as Theatre states an episode
                readonly property string stateWord: {
                    if (vrow.isContinue) return "Continue · page " + root.continuePage
                    if (vrow.effectiveState === "failed") return "Couldn’t finish"
                    if (root._inFlight(vrow.effectiveState)) {
                        var f = vrow.progressFraction()
                        return f >= 0 ? Math.round(f * 100) + "% downloaded" : "Downloading"
                    }
                    if (vrow.owned) return "On this device"
                    return "Available"
                }
                readonly property color stateInk: vrow.isContinue ? theme.gold
                    : (vrow.effectiveState === "failed" ? "#e6a3a3"
                       : (vrow.owned ? root.ownedInk : theme.inkDimmer))
                readonly property string chapterSpan: {
                    var a = vrow.modelData.chapterStart, b = vrow.modelData.chapterEnd
                    if (!a || !String(a).length || !b || !String(b).length) return ""
                    return String(a) === String(b) ? ("Chapter " + a) : ("Chapters " + a + "–" + b)
                }

                function progressFraction() {
                    if (vrow.prog === undefined || vrow.prog === null) return -1
                    var t = Number(vrow.prog.total) || 0
                    if (t <= 0) return -1
                    return (Number(vrow.prog.done) || 0) / t
                }
                function progText(verb) {
                    var f = vrow.progressFraction()
                    return f >= 0 ? (verb + " " + Math.round(f * 100) + "%") : (verb + "…")
                }
                function statusLine() {
                    switch (vrow.effectiveState) {
                    case "ready": return "● Downloaded"
                    case "resolving": return "Finding source…"
                    case "ingesting": return "Adding to library…"
                    case "packing": return vrow.progText("Building")
                    case "downloading": return vrow.progText("Downloading")
                    case "failed": return "Couldn’t finish — choose another source"
                    default: return ""
                    }
                }

                // ── the row: Theatre's episode anatomy, portrait artwork ──
                Item {
                    id: rowMain
                    width: parent.width
                    height: vrow.isContinue ? 428 : 356

                    // hover / continue tint, inset to the page margins like Theatre
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: theme.margin
                        anchors.rightMargin: theme.margin
                        color: vrow.isContinue ? Qt.rgba(0.94, 0.77, 0.29, 0.035)
                             : (rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent")
                    }
                    // Theatre's 2px gold rail on the row you are mid-way through
                    Rectangle {
                        x: theme.margin; width: 2; height: parent.height
                        visible: vrow.isContinue; color: theme.gold
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.leftMargin: theme.margin
                        anchors.right: parent.right; anchors.rightMargin: theme.margin
                        anchors.bottom: parent.bottom; height: 1; color: theme.edge
                    }

                    // ── number rail — carries THE SPINE when the book is on disk ──
                    Item {
                        id: numberRail
                        x: theme.margin + 2
                        width: 84; height: parent.height
                        Column {
                            anchors.centerIn: parent; spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: vrow.modelData.number || "?"
                                color: vrow.isContinue ? theme.gold : theme.ink
                                font.family: theme.display
                                font.pixelSize: vrow.isContinue ? 42 : 34
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "VOL"
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 9
                                font.letterSpacing: 1.1; font.weight: Font.DemiBold
                            }
                        }
                        // a hairline normally; a gold SPINE once the book is yours
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: vrow.owned ? 2 : 1
                            height: parent.height - 28
                            color: vrow.owned ? theme.gold : theme.edge
                            opacity: vrow.owned ? 0.55 : 1.0
                        }
                    }

                    // ── the book: portrait, where Theatre has a landscape still ──
                    Item {
                        id: cov
                        x: numberRail.x + numberRail.width + 16
                        y: (parent.height - height) / 2
                        width: vrow.isContinue ? 268 : 220
                        height: vrow.isContinue ? 390 : 320
                        Rectangle {
                            anchors.fill: parent; radius: 6; clip: true
                            color: "#15171f"
                            border.width: 1
                            border.color: vrow.owned ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
                            Text {
                                anchors.centerIn: parent
                                visible: coverImg.status !== Image.Ready
                                text: vrow.modelData.number || "?"
                                color: Qt.rgba(1, 1, 1, 0.5)
                                font.family: theme.display
                                font.pixelSize: vrow.isContinue ? 78 : 64
                            }
                            Image {
                                id: coverImg
                                anchors.fill: parent; anchors.margins: 1
                                source: root.coverFor(vrow.modelData)
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true; cache: true
                                sourceSize.width: 440   // 2x the display width — past that is wasted
                                                        // pixels, not visible sharpness
                            }
                            // a book you own catches the light down its spine edge
                            Rectangle {
                                visible: vrow.owned
                                anchors.left: parent.left; anchors.top: parent.top
                                anchors.bottom: parent.bottom; anchors.margins: 1
                                width: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.28) }
                                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                                }
                            }
                            // how far into THIS book you are — Theatre's thumbnail bar
                            Rectangle {
                                visible: vrow.isContinue
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 4; color: Qt.rgba(0, 0, 0, 0.5)
                                Rectangle {
                                    width: parent.width * root.continueFraction
                                    height: parent.height; color: theme.gold
                                }
                            }
                        }
                    }

                    // ── name · chapter span · blurb ──
                    Column {
                        anchors.left: cov.right; anchors.leftMargin: 18
                        anchors.right: statusBlock.left; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: vrow.isContinue ? 12 : 9
                        Text {
                            width: parent.width
                            text: (vrow.modelData.title && String(vrow.modelData.title).length)
                                ? vrow.modelData.title : ("Volume " + (vrow.modelData.number || ""))
                            color: theme.ink; font.family: theme.ui
                            font.pixelSize: vrow.isContinue ? 26 : 22
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: vrow.chapterSpan.length > 0
                            text: vrow.chapterSpan
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 15
                        }
                        Text {
                            visible: vrow.synopsisText.length > 0
                            width: parent.width; text: vrow.synopsisText
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                            lineHeight: 1.45; wrapMode: Text.WordWrap
                            maximumLineCount: vrow.isContinue ? 5 : 4
                            elide: Text.ElideRight
                        }
                        // live download bar, only while acquiring with a known total
                        Row {
                            visible: vrow.progressFraction() >= 0
                            spacing: 10
                            Rectangle {
                                width: 180; height: 3; radius: 2
                                anchors.verticalCenter: parent.verticalCenter
                                color: Qt.rgba(1, 1, 1, 0.14)
                                Rectangle {
                                    height: parent.height; radius: 2; color: theme.gold
                                    width: parent.width * Math.max(0, Math.min(1, vrow.progressFraction()))
                                }
                            }
                            Text {
                                text: vrow.statusLine()
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            id: appleAttrib
                            readonly property bool isLink: vrow.modelData.synopsisSource === "apple"
                                && !!vrow.modelData.synopsisSourceUrl
                            visible: vrow.modelData.synopsisSource === "apple"
                            text: "Synopsis via Apple Books"
                            color: (appleAttrib.isLink && attribMa.containsMouse) ? theme.gold : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11
                            MouseArea {
                                id: attribMa
                                anchors.fill: parent
                                enabled: appleAttrib.isLink
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (vrow.modelData.synopsisSourceUrl)
                                    Qt.openUrlExternally(vrow.modelData.synopsisSourceUrl)
                            }
                        }
                    }

                    // ── the state, said once, in Theatre's uppercase ──
                    Row {
                        id: statusBlock
                        anchors.right: rowActions.left; anchors.rightMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        visible: rowMain.width > 900
                        PlayerIcon {
                            visible: vrow.owned && !vrow.isContinue
                            width: 15; height: 15; kind: "check"; ink: root.ownedInk
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: vrow.stateWord
                            color: vrow.stateInk
                            font.family: theme.ui; font.pixelSize: 10
                            font.letterSpacing: 1.1; font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // ── circular actions, Theatre's 38px pair ──
                    Row {
                        id: rowActions
                        anchors.right: parent.right; anchors.rightMargin: theme.margin + 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Rectangle {
                            width: 38; height: 38; radius: 19
                            color: primMa.containsMouse ? theme.ink : Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1
                            border.color: vrow.isContinue ? theme.gold : theme.edge
                            PlayerIcon {
                                anchors.centerIn: parent
                                width: 16; height: 16
                                kind: vrow.owned ? "play"
                                      : (root._inFlight(vrow.effectiveState) ? "cancel" : "download")
                                ink: primMa.containsMouse ? "#111111"
                                     : (vrow.isContinue ? theme.gold : theme.ink)
                            }
                            MouseArea {
                                id: primMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.primaryAction(vrow)
                            }
                        }
                        // inert owned marker — the slot Theatre gives the download button
                        Rectangle {
                            visible: vrow.owned
                            width: 38; height: 38; radius: 19
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1; border.color: root.ownedInk
                            PlayerIcon {
                                anchors.centerIn: parent
                                width: 16; height: 16; kind: "check"; ink: root.ownedInk
                            }
                        }
                    }

                    MouseArea {
                        id: rowMa; anchors.fill: parent; hoverEnabled: true; z: -1
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.primaryAction(vrow)
                    }
                }

            }
        }
    }
}
