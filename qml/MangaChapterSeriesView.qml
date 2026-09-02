// MangaChapterSeriesView — restored Theatre-inspired chapter surface (Arc 39).
// Visual oracle: Preflight Arc 39 mockups/chapter-mode-theatre-oracle-v1.html.
// One page selector only; every page materializes at most ten chapter thumbnail delegates.
import QtQuick
import "MangaChapterGrouping.js" as Grouping

Item {
    id: root
    objectName: "mangaChapterSeriesView"

    property Item backdrop
    property string seriesId: ""              // canonical Colosseum identity (mal:N)
    property string sourceSeriesId: ""        // WeebCentral provider identity
    property string seriesTitle: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int year: 0
    property string synopsis: ""
    property var genres: []
    property real score: 0
    property var chapters: []
    property var exactRangeRecord: ({})
    property var downloader: (typeof Downloads !== "undefined") ? Downloads : null
    property var collectionEntry: null
    property bool loading: false
    property string errorText: ""

    property int currentPageIndex: 0
    property bool pageMenuOpen: false
    readonly property int pageSelectorControlCount: 1
    readonly property var groupedResult: Grouping.group(root.chapters || [], root.exactRangeRecord || ({}), root.seriesId)
    readonly property var pageWindows: {
        var out = []
        var groups = root.groupedResult.groups || []
        for (var g = 0; g < groups.length; ++g) {
            var windows = groups[g].windows || []
            for (var w = 0; w < windows.length; ++w)
                out.push({ groupLabel: groups[g].label || "", chapters: windows[w].chapters || [] })
        }
        return out
    }
    readonly property int pageCount: root.pageWindows.length
    readonly property string pageSelectorLabel: "Page " + (root.currentPageIndex + 1)
    readonly property var activeChapters: root.pageCount > 0 && root.currentPageIndex < root.pageCount
        ? (root.pageWindows[root.currentPageIndex].chapters || []) : []
    readonly property int activeChapterCount: root.activeChapters.length
    readonly property int activeThumbnailDelegateCount: chapterRepeater.count
    readonly property string activeRangeLabel: {
        if (!root.activeChapters.length) return ""
        var first = root.activeChapters[0]
        var last = root.activeChapters[root.activeChapters.length - 1]
        var a = String(first.number !== undefined && first.number !== null ? first.number : "?")
        var b = String(last.number !== undefined && last.number !== null ? last.number : "?")
        return "Chapters " + a + "–" + b
    }

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal tankobanRequested()
    signal readChapterRequested(string chapterId, string chapterLabel)

    function selectPage(index) {
        var i = Math.max(0, Math.min(Number(index), root.pageCount - 1))
        if (!isFinite(i)) i = 0
        root.currentPageIndex = i
        root.pageMenuOpen = false
    }
    onPageCountChanged: {
        if (root.pageCount <= 0) root.currentPageIndex = 0
        else if (root.currentPageIndex >= root.pageCount) root.currentPageIndex = root.pageCount - 1
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#050608" }

    MangaSeriesSharedHeader {
        id: sharedHeader
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        tankobanMode: false
        seriesTitle: root.seriesTitle
        banner: root.banner
        cover: root.cover
        author: root.author
        status: root.status
        year: root.year
        score: root.score
        synopsis: root.synopsis
        collectionEntry: root.collectionEntry
        onBackRequested: root.backRequested()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onCloseRequested: root.closeRequested()
        onTankobanRequested: root.tankobanRequested()
    }

    Flickable {
        id: scroll
        anchors.top: sharedHeader.bottom
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: content.height + 54
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: scroll.width
            spacing: 0

            Item {
                width: parent.width; height: 58
                Text {
                    anchors.left: parent.left; anchors.leftMargin: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CHAPTERS  " + root.chapters.length
                    color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 12; font.letterSpacing: 2.2
                }

                Item {
                    id: pageSelector
                    objectName: "mangaChapterPageSelector"
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    width: 116; height: 34; z: 20
                    activeFocusOnTab: true
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: "Chapter page selector"
                    Rectangle { anchors.fill: parent; radius: 17; color: theme.glassTint; border.width: 1; border.color: root.pageMenuOpen ? theme.gold : theme.edge }
                    Text { anchors.centerIn: parent; text: root.pageSelectorLabel + "  ▾"; color: root.pageMenuOpen ? theme.gold : theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.pageMenuOpen = !root.pageMenuOpen }
                    Keys.onReturnPressed: root.pageMenuOpen = !root.pageMenuOpen
                    Keys.onEnterPressed: root.pageMenuOpen = !root.pageMenuOpen
                    Keys.onEscapePressed: root.pageMenuOpen = false
                }
            }
            Item {
                width: parent.width
                height: ledger.height + 54
                Rectangle {
                    id: ledger
                    anchors.left: parent.left; anchors.leftMargin: theme.margin
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.top: parent.top
                    height: 64 + chapterRepeater.count * 156
                    radius: 18
                    color: Qt.rgba(0.07, 0.08, 0.11, 0.88)
                    border.width: 1; border.color: theme.edge
                    clip: true

                    Item {
                        id: ledgerHeader
                        width: parent.width; height: 64
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 24
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.activeRangeLabel
                            color: theme.ink; font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 190
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.activeChapterCount + (root.activeChapterCount === 1 ? " chapter" : " chapters")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                        }
                        Rectangle {
                            anchors.right: parent.right; anchors.rightMargin: 22
                            anchors.verticalCenter: parent.verticalCenter
                            width: batchLabel.implicitWidth + 28; height: 30; radius: 8
                            color: batchMouse.containsMouse ? theme.glassHi : theme.glassTint
                            border.width: 1; border.color: theme.edge
                            Text { id: batchLabel; anchors.centerIn: parent; text: "↓  Download page"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                            MouseArea { id: batchMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!root.downloader) return
                                    for (var i = 0; i < root.activeChapters.length; ++i) {
                                        var c = root.activeChapters[i]
                                        root.downloader.downloadChapter(String(c.id || ""), root.seriesId, root.seriesTitle, String(c.name || c.label || ("Chapter " + c.number)))
                                    }
                                }
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                    }
                    Column {
                        anchors.top: ledgerHeader.bottom
                        width: parent.width
                        Repeater {
                            id: chapterRepeater
                            model: root.activeChapters
                            delegate: Item {
                                id: row
                                required property var modelData
                                width: ledger.width; height: 156
                                property string chapterId: String(modelData.id || "")
                                property string dlState: "none"
                                property int dlDone: 0
                                property int dlTotal: 0
                                property string liveThumb: ""
                                readonly property bool inFlight: dlState === "queued" || dlState === "downloading"
                                readonly property string chapterLabel: String(modelData.name || modelData.label || ("Chapter " + (modelData.number || "")))
                                readonly property string thumbUrl: dlState === "done" ? firstLocalUrl() : liveThumb

                                function firstLocalUrl() {
                                    if (!root.downloader) return ""
                                    var pages = root.downloader.localPages(row.chapterId) || []
                                    return pages.length ? String(pages[0].url || "") : ""
                                }
                                function refreshStatus() {
                                    if (!root.downloader) return
                                    var st = root.downloader.statusOf(row.chapterId) || ({})
                                    row.dlState = String(st.state || "none")
                                    row.dlDone = Number(st.done || 0); row.dlTotal = Number(st.total || 0)
                                }
                                function requestThumb() {
                                    if (root.downloader && row.chapterId.length)
                                        root.downloader.fetchThumb(root.sourceSeriesId.length ? root.sourceSeriesId : root.seriesId, row.chapterId)
                                }
                                function download() {
                                    if (!root.downloader || !row.chapterId.length) return
                                    root.downloader.downloadChapter(row.chapterId, root.seriesId, root.seriesTitle, row.chapterLabel)
                                }
                                Component.onCompleted: { refreshStatus(); requestThumb() }

                                Connections {
                                    target: root.downloader
                                    ignoreUnknownSignals: true
                                    function onProgress(id, done, total) { if (String(id) === row.chapterId) { row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total } }
                                    function onFinished(id) { if (String(id) === row.chapterId) row.dlState = "done" }
                                    function onFailed(id, reason) { if (String(id) === row.chapterId) row.dlState = "error" }
                                    function onRemoved(id) { if (String(id) === row.chapterId) { row.dlState = "none"; row.liveThumb = ""; row.requestThumb() } }
                                    function onThumbReady(id, url) { if (String(id) === row.chapterId) row.liveThumb = String(url || "") }
                                }
                                Rectangle { anchors.fill: parent; color: rowMouse.containsMouse ? Qt.rgba(1,1,1,0.045) : "transparent" }
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 52; horizontalAlignment: Text.AlignHCenter
                                    text: String(modelData.number !== undefined && modelData.number !== null ? modelData.number : "?")
                                    color: theme.ink; font.family: theme.display; font.pixelSize: 24; font.weight: Font.DemiBold
                                }
                                Item {
                                    id: thumb
                                    anchors.left: parent.left; anchors.leftMargin: 94
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 100; height: 140
                                    Rectangle { anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1; border.color: row.dlState === "done" ? Qt.rgba(.94,.77,.29,.5) : theme.edge }
                                    Text { anchors.centerIn: parent; visible: thumbImage.status !== Image.Ready; text: String(modelData.number || "?"); color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 28 }
                                    Image { id: thumbImage; anchors.fill: parent; anchors.margins: 1; source: row.thumbUrl; visible: status === Image.Ready; fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true; sourceSize.width: 280 }
                                }
                                Column {
                                    anchors.left: thumb.right; anchors.leftMargin: 18
                                    anchors.right: trailing.left; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 6
                                    Text { width: parent.width; text: row.chapterLabel; color: rowMouse.containsMouse ? theme.gold : theme.ink; font.family: theme.ui; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Text {
                                        width: parent.width
                                        text: row.dlState === "done" ? "Downloaded"
                                            : row.dlState === "queued" ? "Queued…"
                                            : row.dlState === "downloading" ? (row.dlTotal > 0 ? ("Downloading " + Math.round(row.dlDone / row.dlTotal * 100) + "%") : "Downloading…")
                                            : row.dlState === "error" ? "Failed · Retry" : "Available"
                                        color: row.dlState === "done" ? theme.gold : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                        font.family: theme.ui; font.pixelSize: 12
                                    }
                                }
                                Item {
                                    id: trailing
                                    anchors.right: parent.right; anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 38; height: 38
                                    Rectangle { anchors.fill: parent; radius: 19; color: trailingMouse.containsMouse ? theme.glassHi : "transparent" }
                                    Text {
                                        anchors.centerIn: parent
                                        text: row.dlState === "done" ? (trailingMouse.containsMouse ? "✕" : "✓")
                                            : row.inFlight ? "✕" : row.dlState === "error" ? "↻" : "↓"
                                        color: row.dlState === "done" ? theme.gold : (trailingMouse.containsMouse ? theme.gold : theme.inkDim)
                                        font.pixelSize: 16
                                    }
                                    MouseArea {
                                        id: trailingMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; z: 3
                                        onClicked: {
                                            if (!root.downloader) return
                                            if (row.dlState === "done") root.downloader.deleteChapter(row.chapterId)
                                            else if (row.inFlight) root.downloader.cancelDownload(row.chapterId)
                                            else row.download()
                                        }
                                    }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,.05) }
                                MouseArea {
                                    id: rowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.readChapterRequested(row.chapterId, row.chapterLabel)
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.loading || root.errorText.length > 0 || (!root.loading && !root.chapters.length)
                        anchors.centerIn: parent
                        text: root.loading ? "Loading chapters…" : (root.errorText.length ? root.errorText : "No chapters available")
                        color: root.errorText.length ? "#e6a3a3" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }
        }
    }
    Rectangle {
        id: pageDropdown
        visible: root.pageMenuOpen && root.pageCount > 0
        z: 200
        x: pageSelector.mapToItem(root, 0, 0).x
        y: pageSelector.mapToItem(root, 0, pageSelector.height + 6).y
        width: 150
        height: Math.min(root.pageCount * 36 + 12, 300)
        radius: 12
        color: Qt.rgba(.035,.04,.06,.98)
        border.width: 1; border.color: theme.edge

        ListView {
            id: pageList
            anchors.fill: parent; anchors.margins: 6
            clip: true
            model: root.pageCount
            currentIndex: root.currentPageIndex
            delegate: Rectangle {
                required property int index
                width: pageList.width; height: 36; radius: 7
                color: index === root.currentPageIndex ? theme.glassHi : (pageOptionMouse.containsMouse ? theme.glassTint : "transparent")
                Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "Page " + (index + 1); color: index === root.currentPageIndex ? theme.gold : theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { id: pageOptionMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.selectPage(index) }
            }
        }
    }

    MouseArea {
        visible: root.pageMenuOpen
        z: 190
        anchors.fill: parent
        onClicked: root.pageMenuOpen = false
    }
}
