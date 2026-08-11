// MangaTankobanLibrary - the Reading Room collection pane.
//
// The native seams are deliberately unchanged: `service` resolves to the native
// TankobanVolumes context object, `progress` to Progress, and `downloader` to
// Downloads. This file owns presentation and the small amount of state needed to
// make a volume shelf feel immediate; MangaSeries remains the reader/source/page
// authority.
import QtQuick
import QtQuick.Controls
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

    // `chapters` is the full live chapter list used to derive per-volume covers.
    // `chapterRows` is the honest list shown by the Chapters tab: the loose tail
    // for a qualified series, or the complete run for a chapter-only series.
    property var chapters: []
    property var chapterRows: []
    readonly property bool showVolumes: root.volumeRows.length > 0

    property var coverByVolume: ({})
    property var _thumbWanted: ({})
    property var _resume: null
    property var volumeRows: []
    property var progressByVolume: ({})

    // Compatibility helpers retained for existing service/logic harnesses. They
    // are no longer the visual model: the GridView below consumes every row.
    readonly property int pageSize: 10
    readonly property var pagedRows: Vol.pageGroups(root.volumeRows, root.pageSize)
    property int activePage: 0
    readonly property var visibleRows: root.rowsOnPage(root.activePage)
    readonly property var activePageInfo:
        root.activePage >= 0 && root.activePage < root.pagedRows.length
            ? root.pagedRows[root.activePage] : null

    // Reading Room state.
    property string activeTab: root.showVolumes ? "volumes" : "chapters"
    property bool selecting: false
    property var selectedNumbers: []
    property var detailVolume: null
    readonly property int renderedCount: root.visibleRows.length
    implicitWidth: 640
    implicitHeight: 480
    readonly property int minimumTileWidth: 126
    readonly property int tileHeight: 210
    readonly property int gridGap: 16
    readonly property int autoLandNumber: root.currentNumber

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

    function stateWordFor(row) {
        switch (root.effectiveState(row)) {
        case "ready": return "On this device"
        case "resolving": return "Finding source…"
        case "ingesting": return "Adding to library…"
        case "packing": return "Building…"
        case "downloading": return "Downloading…"
        case "failed": return "Couldn't finish"
        default: return "Available"
        }
    }

    function isContinue(row) {
        return root.continueVolumeId.length > 0
            && String(row && row.id) === root.continueVolumeId
            && root.continueFraction > 0.005 && root.continueFraction < 0.995
    }

    function volumeCaptionFor(row) {
        var n = Vol.volumeToken(row)
        if (root.isContinue(row)) return "Vol " + n + " · p. " + root.continuePage
        var f = root.progressFraction(row)
        if (root._inFlight(root.effectiveState(row)) && f >= 0)
            return "Vol " + n + " · " + Math.round(f * 100) + "%"
        if (root.effectiveState(row) === "failed") return "Vol " + n + " · Couldn't finish"
        return "Vol " + n
    }

    function chapterSpanFor(row) {
        var first = row && row.chapterStart ? String(row.chapterStart) : ""
        var last = row && row.chapterEnd ? String(row.chapterEnd) : ""
        if (!first.length || !last.length) return ""
        return first === last ? "Chapter " + first : "Chapters " + first + "–" + last
    }

    // ------------------------------------------------------------------
    // Covers: viewport only, accumulated and retryable
    // ------------------------------------------------------------------

    function _firstChapterIdIn(row, chs) {
        var lo = Number(row && row.chapterStart), hi = Number(row && row.chapterEnd)
        if (isNaN(lo) || isNaN(hi)) return ""
        var bestId = "", bestNumber = Infinity
        for (var i = 0; i < chs.length; i++) {
            var n = Number(chs[i].number)
            if (isNaN(n) || n < lo || n > hi) continue
            if (n < bestNumber) {
                bestNumber = n
                bestId = String(chs[i].id || "")
            }
        }
        return bestId
    }

    function visibleRowsForCovers() {
        var rows = root.volumeRows || []
        if (!rows.length) return []
        var cols = volumeGrid.columns > 0 ? volumeGrid.columns : 1
        var cell = volumeGrid.cellHeight > 0 ? volumeGrid.cellHeight : root.tileHeight + 46
        var top = volumeGrid.height > 0 ? Math.max(0, Math.floor(volumeGrid.contentY / cell) - 1) : 0
        var bottom = volumeGrid.height > 0
            ? Math.ceil((volumeGrid.contentY + volumeGrid.height) / cell) + 1
            : 3
        var first = Math.max(0, top * cols)
        var last = Math.min(rows.length, Math.max(first + cols, bottom * cols))
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
                for (var k in root._thumbWanted)
                    if (k !== cid) retry[k] = root._thumbWanted[k]
                root._thumbWanted = retry
                return
            }
            root.coverByVolume = root._reassign(root.coverByVolume, vid, String(url))
        }
    }

    // ------------------------------------------------------------------
    // Click, batch, select, and jump actions
    // ------------------------------------------------------------------

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
        if (state === "ready") {
            root.openVolumeRequested(String(row.id))
            return
        }
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
        if (root.nextBatch.numbers.length)
            root.batchRequested(root.nextBatch.numbers, "Get next 10 missing")
    }

    function selectNumber(number) {
        var n = Number(number)
        if (!isFinite(n)) return
        var out = root.selectedNumbers.slice()
        var at = out.indexOf(n)
        if (at >= 0) out.splice(at, 1)
        else out.push(n)
        out.sort(function(a, b) { return a - b })
        root.selectedNumbers = out
    }

    function clearSelection() {
        root.selectedNumbers = []
        root.selecting = false
    }

    function downloadSelected() {
        var numbers = root.selectedNumbers.slice()
        if (!numbers.length) return
        root.batchRequested(numbers, "Download selected")
    }

    function jumpNumbers() {
        var out = []
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            var n = Number(rows[i].number)
            if (!isFinite(n)) continue
            if (n === 1 || n % 10 === 0) out.push(n)
        }
        return out
    }

    function jumpToNumber(number) {
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            if (Number(rows[i].number) === Number(number)) {
                volumeGrid.positionViewAtIndex(i, GridView.Beginning)
                // The position change is asynchronous. Ask for the jump target
                // immediately as well; the later contentY signal fills the
                // surrounding viewport.
                root.requestCovers([rows[i]])
                return
            }
        }
    }

    readonly property var inFlightIds: {
        var out = []
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++)
            if (root._inFlight(root.effectiveState(rows[i]))) out.push(String(rows[i].id))
        return out
    }

    function cancelRemaining() {
        var s = root.serviceObject
        var ids = root.inFlightIds
        for (var i = 0; i < ids.length; i++)
            if (s && s.cancel) s.cancel(ids[i])
    }

    // ------------------------------------------------------------------
    // Lifecycle and live service updates
    // ------------------------------------------------------------------

    Component.onCompleted: {
        root.refresh()
        root.refreshResume()
        Qt.callLater(root.requestCovers)
        Qt.callLater(root._autoLand)
    }

    onSeriesIdChanged: {
        root.coverByVolume = ({})
        root._thumbWanted = ({})
        root._resume = null
        root.selectedNumbers = []
        root.refresh()
        root.refreshResume()
        Qt.callLater(root.requestCovers)
    }
    onVolumeRowsChanged: {
        if (!root.showVolumes) root.activeTab = "chapters"
        else if (root.activeTab !== "chapters") root.activeTab = "volumes"
        Qt.callLater(root.requestCovers)
        Qt.callLater(root._autoLand)
    }
    onChaptersChanged: root.requestCovers()
    onActiveTabChanged: Qt.callLater(root.requestCovers)

    function _autoLand() {
        if (!root.showVolumes || !root.continueVolumeId.length) return
        var rows = root.volumeRows || []
        for (var i = 0; i < rows.length; i++) {
            if (String(rows[i].id) === root.continueVolumeId) {
                volumeGrid.positionViewAtIndex(i, GridView.Center)
                root.requestCovers()
                return
            }
        }
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

    // ------------------------------------------------------------------
    // Pane chrome
    // ------------------------------------------------------------------

    Item {
        id: paneHeader
        objectName: "readingRoomPaneHeader"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 66

        Row {
            id: tabs
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 26

            Item {
                visible: root.showVolumes
                width: volumesTab.implicitWidth
                height: 42
                Text {
                    id: volumesTab
                    anchors.centerIn: parent
                    text: "Volumes  " + root.volumeRows.length + " books"
                    color: root.activeTab === "volumes" ? theme.ink : theme.inkDimmer
                    font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: root.activeTab === "volumes"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 2; color: theme.gold
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: root.activeTab = "volumes"
                }
            }

            Item {
                width: chaptersTab.implicitWidth
                height: 42
                Text {
                    id: chaptersTab
                    anchors.centerIn: parent
                    text: root.showVolumes
                        ? "Chapters  latest " + root._chapterRangeText()
                        : "Chapters  " + root.chapterRows.length + " chapters"
                    color: root.activeTab === "chapters" ? theme.ink : theme.inkDimmer
                    font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: root.activeTab === "chapters"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 2; color: theme.gold
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: root.activeTab = "chapters"
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Rectangle {
                visible: root.showVolumes && root.nextBatch.numbers.length > 0
                width: nextText.implicitWidth + 28; height: 44; radius: 9
                color: nextMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.10) : "transparent"
                border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.45)
                Text { id: nextText; anchors.centerIn: parent; text: "Get next 10 missing"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { id: nextMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: root.requestNextMissing() }
            }

            Rectangle {
                visible: root.showVolumes
                width: selectText.implicitWidth + 28; height: 44; radius: 9
                color: root.selecting ? theme.ink : "transparent"
                border.width: 1; border.color: root.selecting ? theme.ink : theme.edge
                Text { id: selectText; anchors.centerIn: parent; text: root.selecting ? "Done" : "Select"
                    color: root.selecting ? "#1a1306" : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { root.selecting = !root.selecting; if (!root.selecting) root.selectedNumbers = [] } }
            }
        }

        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1; color: theme.edge }
    }

    Item {
        id: paneBody
        anchors.top: paneHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        GridView {
            id: volumeGrid
            objectName: "readingRoomVolumeGrid"
            anchors.fill: parent
            anchors.topMargin: 20
            anchors.leftMargin: 2
            anchors.rightMargin: root.longSeries ? 54 : 0
            anchors.bottomMargin: 22
            visible: root.activeTab === "volumes"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.volumeRows
            property int columns: Math.max(1, Math.floor((width + root.gridGap)
                / (root.minimumTileWidth + root.gridGap)))
            cellWidth: columns > 0 ? (width - (columns - 1) * root.gridGap) / columns + root.gridGap : width
            cellHeight: root.tileHeight + 48
            cacheBuffer: root.tileHeight * 2
            onContentYChanged: root.requestCovers()
            onWidthChanged: root.requestCovers()

            delegate: Item {
                id: tile
                required property var modelData
                width: volumeGrid.cellWidth - root.gridGap
                height: root.tileHeight + 34
                property string volumeId: String(modelData.id || "")
                property string tileState: root.effectiveState(modelData)
                property real fraction: root.progressFraction(modelData)
                property bool continuation: root.isContinue(modelData)
                property bool selected: root.selectedNumbers.indexOf(Number(modelData.number)) >= 0

                Rectangle {
                    id: coverFrame
                    width: parent.width
                    height: root.tileHeight
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.05)
                    opacity: tile.tileState === "ready" ? 1.0
                           : tile.tileState === "failed" ? 0.45
                           : root._inFlight(tile.tileState) ? 0.60
                           : tile.tileState === "none" ? 0.42 : 0.70
                    border.width: tile.selected ? 2 : (tile.continuation ? 2 : 0)
                    border.color: tile.selected || tile.continuation ? theme.gold : "transparent"

                    Image {
                        anchors.fill: parent
                        source: root.coverFor(tile.modelData)
                        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        visible: status === Image.Ready
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: parent.children[0].status !== Image.Ready
                        text: Vol.volumeToken(tile.modelData)
                        color: Qt.rgba(1, 1, 1, 0.26)
                        font.family: theme.display; font.pixelSize: 42; font.weight: Font.Black
                    }
                    Rectangle {
                        visible: root._inFlight(tile.tileState) || tile.continuation
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 3; color: Qt.rgba(0, 0, 0, 0.55)
                        Rectangle { width: Math.max(0, Math.min(1, tile.fraction < 0 ? 0 : tile.fraction)) * parent.width
                            height: parent.height; color: theme.gold }
                    }
                    Rectangle {
                        visible: tile.tileState === "failed"
                        anchors.fill: parent; color: "#e6a3a3"; opacity: 0.18; radius: 8
                    }
                    Rectangle {
                        visible: tile.tileState === "none" && tileMa.containsMouse && !root.selecting
                        anchors.centerIn: parent; width: getText.implicitWidth + 26; height: 32; radius: 8
                        color: Qt.rgba(0.04, 0.04, 0.05, 0.88)
                        border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.55)
                        Text { id: getText; anchors.centerIn: parent; text: "Get"; color: theme.gold
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                    }
                }

                Row {
                    anchors.top: coverFrame.bottom; anchors.topMargin: 8
                    anchors.left: parent.left; anchors.right: parent.right
                    spacing: 6
                    Rectangle { visible: tile.tileState === "ready"; width: 5; height: 5; radius: 3
                        color: theme.gold; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: root.volumeCaptionFor(tile.modelData)
                        color: tile.continuation ? theme.gold
                             : (tile.tileState === "failed" ? "#e6a3a3" : theme.inkDimmer)
                        font.family: theme.ui; font.pixelSize: 12
                        font.weight: tile.continuation ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight; maximumLineCount: 1
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: coverFrame.bottom; anchors.topMargin: 28
                    text: root.chapterSpanFor(tile.modelData) + " · " + root.stateWordFor(tile.modelData)
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
                    elide: Text.ElideRight; maximumLineCount: 1
                    visible: tileMa.containsMouse
                }
                MouseArea {
                    id: tileMa
                    anchors.fill: coverFrame
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.detailVolume = tile.modelData
                            detailPopup.open()
                            return
                        }
                        if (root.selecting) root.selectNumber(tile.modelData.number)
                        else root.primaryAction(tile.modelData)
                    }
                }
            }
        }

        ListView {
            id: chapterScroll
            anchors.fill: parent
            anchors.topMargin: 20
            anchors.bottomMargin: 20
            visible: root.activeTab === "chapters"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: height * 2
            model: root.chapterRows
            delegate: Item {
                id: chapterRow
                required property var modelData
                width: chapterScroll.width - 10
                height: 54
                property string chapterId: String(modelData.id || "")
                property string chapterLabel: (modelData.name && String(modelData.name).length)
                    ? String(modelData.name) : ("Chapter " + String(modelData.number || ""))
                property string chapterState: "none"

                function refreshState() {
                    var d = root.downloaderObject
                    chapterState = d && d.statusOf ? String(d.statusOf(chapterId).state || "none") : "none"
                }
                Component.onCompleted: refreshState()

                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.number || ""; color: theme.ink; font.family: theme.display; font.pixelSize: 16
                    font.weight: Font.DemiBold; width: 62; horizontalAlignment: Text.AlignRight }
                Text { anchors.left: parent.left; anchors.leftMargin: 92; anchors.right: statusText.left
                    anchors.verticalCenter: parent.verticalCenter; text: chapterRow.chapterLabel
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                Text { id: statusText; anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: chapterRow.chapterState === "done" ? "On this device"
                        : (chapterRow.chapterState === "downloading" ? "Downloading…" : "Get")
                    color: chapterRow.chapterState === "done" ? theme.gold : theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12 }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (chapterRow.chapterState === "done") {
                            root.openChapterRequested(chapterRow.chapterId, chapterRow.chapterLabel)
                        } else if (chapterRow.chapterState !== "downloading") {
                            var d = root.downloaderObject
                            if (d && d.downloadChapter)
                                d.downloadChapter(chapterRow.chapterId, root.seriesId,
                                                  root.seriesTitle, chapterRow.chapterLabel)
                            root.chapterDownloadRequested(chapterRow.chapterId, chapterRow.chapterLabel)
                            chapterRow.refreshState()
                        }
                    }
                }
                Connections {
                    target: root.downloaderObject
                    ignoreUnknownSignals: true
                    function onProgress(cid, done, total) {
                        if (String(cid) === chapterRow.chapterId) chapterRow.chapterState = "downloading"
                    }
                    function onFinished(cid) {
                        if (String(cid) === chapterRow.chapterId) chapterRow.chapterState = "done"
                    }
                    function onRemoved(cid) {
                        if (String(cid) === chapterRow.chapterId) chapterRow.chapterState = "none"
                    }
                    function onFailed(cid, reason) {
                        if (String(cid) === chapterRow.chapterId) chapterRow.chapterState = "failed"
                    }
                }
            }
        }

        Item {
            id: jumpStrip
            visible: root.longSeries && root.activeTab === "volumes"
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
            width: 48
            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 1; color: Qt.rgba(1, 1, 1, 0.08) }
            Column {
                anchors.centerIn: parent; spacing: 2
                Repeater {
                    model: root.jumpNumbers()
                    delegate: Text {
                        required property var modelData
                        text: String(modelData)
                        color: root._jumpInView(Number(modelData)) ? theme.gold : theme.inkDimmer
                        font.family: theme.display; font.pixelSize: 12; font.weight: Font.DemiBold
                        width: 32; horizontalAlignment: Text.AlignHCenter
                        topPadding: 3; bottomPadding: 3
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: root.jumpToNumber(Number(modelData)) }
                    }
                }
            }
        }
    }

    readonly property bool longSeries: root.volumeRows.length > 0 && volumeGrid.contentHeight > height * 3
    function _jumpInView(number) {
        var rows = root.volumeRows || []
        var index = -1
        for (var i = 0; i < rows.length; i++)
            if (Number(rows[i].number) === Number(number)) { index = i; break }
        if (index < 0 || volumeGrid.columns <= 0) return false
        var y = Math.floor(index / volumeGrid.columns) * volumeGrid.cellHeight
        return y >= volumeGrid.contentY && y < volumeGrid.contentY + volumeGrid.height
    }

    function _chapterRangeText() {
        var rows = root.chapterRows || []
        if (!rows.length) return "none"
        var first = rows[0].number || "", last = rows[rows.length - 1].number || ""
        return String(first) + "–" + String(last)
    }

    Popup {
        id: detailPopup
        parent: root
        x: Math.max(12, root.width - width - 24)
        y: 78
        width: Math.min(360, root.width - 24)
        padding: 18
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 12; color: Qt.rgba(0.04, 0.04, 0.05, 0.96)
            border.width: 1; border.color: theme.edge }
        contentItem: Column {
            spacing: 8
            Text { text: root.detailVolume ? ("Vol " + Vol.volumeToken(root.detailVolume)) : ""
                color: theme.ink; font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold }
            Text { width: parent.width; text: root.detailVolume && root.detailVolume.synopsis
                    ? String(root.detailVolume.synopsis) : root.stateWordFor(root.detailVolume || ({}))
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Text { text: "Right-click menu"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 }
        }
    }

    // Floating selection bar: it belongs to this pane, not to the fixed rail.
    Rectangle {
        visible: root.selecting && root.selectedNumbers.length > 0
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 18
        height: 48; width: selectedText.implicitWidth + downloadSelectedText.implicitWidth + 64
        radius: 12; color: Qt.rgba(0.04, 0.04, 0.05, 0.94)
        border.width: 1; border.color: theme.edge
        Text { id: selectedText; anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter
            text: root.selectedNumbers.length + " selected"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
        Rectangle { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
            width: downloadSelectedText.implicitWidth + 24; height: 34; radius: 8; color: theme.gold
            Text { id: downloadSelectedText; anchors.centerIn: parent; text: "Download selected"; color: "#1a1306"
                font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: root.downloadSelected() }
        }
    }
}
