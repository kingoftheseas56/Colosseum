// TankobanChapterList — Chapter Mode's list for the rebuilt Tankoban title page
// (TankobanSeriesView.qml), built to replace MangaChapterSeriesView.qml.
//
// World feel design, 2026-09-24: Chapter Mode keeps its pages of ten until chapter
// thumbnails prove reliable on Hemanth's connection (spec, "Long lists", the Chapter Mode
// gate). What changes now: one slim bar (count · range · ‹ Page N of M › · Go to chapter ·
// Download page), compact 88px rows with the loud gold focus, hover-is-focus, PgUp/PgDn to
// turn pages, typing digits to jump, and thumbnails that are only requested for a page you
// actually stay on. Chapters are never grouped by volume (settled: the metadata is a lost
// cause), so the dormant exact-volume mode in MangaChapterGrouping.js is never fed.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "MangaChapterGrouping.js" as Grouping

Item {
    id: root
    objectName: "mangaChapterSeriesView"

    property string seriesId: ""              // canonical Colosseum identity (mal:N)
    property string sourceSeriesId: ""        // Tankoyomi provider identity
    property string seriesTitle: ""
    property var chapters: []
    property var downloader: (typeof Downloads !== "undefined") ? Downloads : null
    property bool loading: false
    property string errorText: ""
    property bool sourceEnabled: true
    readonly property bool extensionGateVisible: !root.sourceEnabled

    // Same paging contract as MangaChapterSeriesView (numeric windows of ten, never volumes).
    property int currentPageIndex: 0
    readonly property var groupedResult: Grouping.group(root.chapters || [], ({}), root.seriesId)
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
    property string focusChapterId: ""        // the row to focus after a Go-to lands on its page
    readonly property color focusGold: Qt.rgba(0.94, 0.77, 0.29, 1.0)

    signal readChapterRequested(string chapterId, string chapterLabel)
    signal openExtensionsRequested()

    function requestExtensions() { root.openExtensionsRequested() }
    function selectPage(index) {
        var i = Math.max(0, Math.min(Number(index), root.pageCount - 1))
        if (!isFinite(i)) i = 0
        root.currentPageIndex = i
        pageMenu.visible = false
        scroll.contentY = 0
    }
    function stepPage(delta) {
        if (root.pageCount <= 0) return
        root.selectPage(root.currentPageIndex + delta)
        Qt.callLater(root.focusFirstRow)
    }
    function focusFirstRow() {
        var it = chapterRepeater.itemAt(0)
        if (it) it.takeFocus()
    }
    function chapterLabelOf(c) {
        return String(c.name || c.label || ("Chapter " + (c.number !== undefined ? c.number : "")))
    }
    // Go to a chapter by its number: find the page that holds it, open that page, focus the row.
    function goToChapter(text) {
        var want = String(text || "").trim()
        if (!want.length) return false
        for (var p = 0; p < root.pageWindows.length; ++p) {
            var list = root.pageWindows[p].chapters || []
            for (var i = 0; i < list.length; ++i) {
                if (String(list[i].number) === want || Number(list[i].number) === Number(want)) {
                    root.focusChapterId = String(list[i].id || "")
                    root.selectPage(p)
                    Qt.callLater(root.focusPendingRow)
                    return true
                }
            }
        }
        return false
    }
    function focusPendingRow() {
        for (var i = 0; i < chapterRepeater.count; ++i) {
            var it = chapterRepeater.itemAt(i)
            if (it && it.chapterId === root.focusChapterId) {
                root.focusChapterId = ""
                it.takeFocus()
                var p = it.mapToItem(listCol, 0, 0)
                if (p.y + it.height > scroll.contentY + scroll.height) scroll.contentY = p.y + it.height - scroll.height + 12
                else if (p.y < scroll.contentY) scroll.contentY = Math.max(0, p.y - 12)
                return
            }
        }
    }
    onPageCountChanged: {
        if (root.pageCount <= 0) {
            root.currentPageIndex = 0
            pageMenu.visible = false
        } else if (root.currentPageIndex >= root.pageCount) {
            root.currentPageIndex = root.pageCount - 1
        }
    }

    Theme { id: theme }

    // ---- the one bar: count · range · pager · Go to · Download page ----
    Item {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        z: 10
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.024, 0.027, 0.043, 0.96) }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.edge }

        Row {
            x: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: root.activeRangeLabel.length ? root.activeRangeLabel : "Chapters"
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Text {
                    text: root.chapters.length.toLocaleString(Qt.locale(), "f", 0) + (root.chapters.length === 1 ? " chapter" : " chapters")
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.3
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // ‹ Page N of M ›  — the arrows turn pages; the label opens the page list.
            Rectangle {
                id: pageSelector
                objectName: "mangaChapterPageSelector"
                visible: root.pageCount > 1
                height: 36
                width: pagerRow.implicitWidth + 8
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: pageMenu.visible ? theme.gold : theme.edge
                Row {
                    id: pagerRow
                    anchors.centerIn: parent
                    Item {
                        width: 32; height: 32
                        opacity: root.currentPageIndex > 0 ? 1.0 : 0.35
                        Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 20 }
                        KeyboardAction {
                            objectName: "mangaChapterPagePrev"
                            anchors.fill: parent
                            enabled: root.currentPageIndex > 0
                            accessibleName: "Previous page"
                            focusRadius: 16
                            focusColor: root.focusGold
                            onTriggered: root.stepPage(-1)
                        }
                    }
                    Item {
                        width: pageLabel.implicitWidth + 16; height: 32
                        Text {
                            id: pageLabel
                            anchors.centerIn: parent
                            text: "Page " + (root.currentPageIndex + 1) + " of " + root.pageCount
                            color: pageMenu.visible ? theme.gold : theme.ink
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                        }
                        KeyboardAction {
                            objectName: "mangaChapterPageMenu"
                            anchors.fill: parent
                            accessibleName: "Choose chapter page"
                            focusRadius: 10
                            focusColor: root.focusGold
                            onTriggered: {
                                pageMenu.visible = !pageMenu.visible
                                if (pageMenu.visible) {
                                    pageList.currentIndex = root.currentPageIndex
                                    pageList.positionViewAtIndex(root.currentPageIndex, ListView.Center)
                                    pageList.forceActiveFocus(Qt.PopupFocusReason)
                                }
                            }
                        }
                    }
                    Item {
                        width: 32; height: 32
                        opacity: root.currentPageIndex < root.pageCount - 1 ? 1.0 : 0.35
                        Text { anchors.centerIn: parent; text: "›"; color: theme.ink; font.pixelSize: 20 }
                        KeyboardAction {
                            objectName: "mangaChapterPageNext"
                            anchors.fill: parent
                            enabled: root.currentPageIndex < root.pageCount - 1
                            accessibleName: "Next page"
                            focusRadius: 16
                            focusColor: root.focusGold
                            onTriggered: root.stepPage(1)
                        }
                    }
                }
            }

            // Go to chapter: jumps to the page that holds it and focuses the row.
            Rectangle {
                visible: root.chapters.length > 10
                height: 36
                width: 150
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: goTo.activeFocus ? 3 : 1
                border.color: goTo.activeFocus ? theme.gold : theme.edge
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: goTo.text.length === 0
                    text: "Go to chapter"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                }
                TextInput {
                    id: goTo
                    objectName: "mangaChapterGoTo"
                    anchors.fill: parent
                    anchors.leftMargin: 14; anchors.rightMargin: 14
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.ink
                    selectionColor: theme.gold
                    selectedTextColor: "#111111"
                    font.family: theme.ui; font.pixelSize: 13
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}([.][0-9]{0,2})?/ }
                    activeFocusOnTab: true
                    Keys.onReturnPressed: { if (root.goToChapter(text)) text = "" }
                    Keys.onEnterPressed: { if (root.goToChapter(text)) text = "" }
                    Keys.onEscapePressed: { text = ""; root.focusFirstRow() }
                    Keys.onDownPressed: root.focusFirstRow()
                }
            }

            Rectangle {
                visible: root.activeChapters.length > 0 && root.downloader !== null
                height: 36
                width: dlPageText.implicitWidth + 32
                radius: 18
                color: dlPage.interactionActive ? Qt.rgba(1, 1, 1, 0.11) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: theme.edge
                Text {
                    id: dlPageText
                    anchors.centerIn: parent
                    text: "↓  Download page"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
                KeyboardAction {
                    id: dlPage
                    objectName: "mangaChapterDownloadPage"
                    anchors.fill: parent
                    accessibleName: "Download every chapter on this page"
                    focusRadius: 18
                    focusColor: root.focusGold
                    onTriggered: {
                        if (!root.downloader) return
                        for (var i = 0; i < root.activeChapters.length; ++i) {
                            var c = root.activeChapters[i]
                            root.downloader.downloadChapter(String(c.id || ""), root.seriesId, root.seriesTitle, root.chapterLabelOf(c))
                        }
                    }
                }
            }
        }

        // the page list (same window; floats under the pager)
        Rectangle {
            id: pageMenu
            objectName: "mangaChapterPagePopup"
            visible: false
            z: 40
            anchors.top: parent.bottom
            anchors.topMargin: 6
            x: pageSelector.visible ? (pageSelector.mapToItem(bar, 0, 0).x + pageSelector.width - width) : 0
            width: 180
            height: Math.min(root.pageCount * 36 + 12, 320)
            radius: 12
            color: Qt.rgba(0.035, 0.04, 0.06, 0.98)
            border.width: 1
            border.color: theme.edge
            ListView {
                id: pageList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.pageCount
                boundsBehavior: Flickable.StopAtBounds
                Keys.onReturnPressed: root.selectPage(currentIndex)
                Keys.onEnterPressed: root.selectPage(currentIndex)
                Keys.onEscapePressed: pageMenu.visible = false
                delegate: Rectangle {
                    id: pageRow
                    required property int index
                    width: pageList.width
                    height: 36
                    radius: 8
                    color: pageList.currentIndex === pageRow.index && pageList.activeFocus
                        ? Qt.rgba(0.94, 0.77, 0.29, 0.14)
                        : (pageRowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                    border.width: pageList.currentIndex === pageRow.index && pageList.activeFocus ? 2 : 0
                    border.color: theme.gold
                    Text {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: {
                            var w = root.pageWindows[pageRow.index]
                            var cs = w ? (w.chapters || []) : []
                            if (!cs.length) return "Page " + (pageRow.index + 1)
                            return "Page " + (pageRow.index + 1) + "  ·  " + cs[0].number + "–" + cs[cs.length - 1].number
                        }
                        color: pageRow.index === root.currentPageIndex ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    MouseArea {
                        id: pageRowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: pageList.currentIndex = pageRow.index
                        onClicked: root.selectPage(pageRow.index)
                    }
                }
            }
        }
    }

    // ---- the chapters of this page ----
    Flickable {
        id: scroll
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: listCol.height + 110
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: scroll }

        Column {
            id: listCol
            y: 10
            width: scroll.width
            spacing: 0

            Repeater {
                id: chapterRepeater
                model: root.activeChapters
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: listCol.width
                    height: 88
                    readonly property string chapterId: String(modelData.id || "")
                    readonly property string chapterLabel: root.chapterLabelOf(modelData)
                    property string dlState: "none"
                    property int dlDone: 0
                    property int dlTotal: 0
                    property string liveThumb: ""
                    readonly property bool inFlight: dlState === "queued" || dlState === "downloading"
                    readonly property string thumbUrl: dlState === "done" ? firstLocalUrl() : liveThumb
                    readonly property bool focused: rowAction.activeFocus

                    function takeFocus() { rowAction.forceActiveFocus(Qt.OtherFocusReason) }
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
                    function runTrailing() {
                        if (!root.downloader) return
                        if (row.dlState === "done") root.downloader.deleteChapter(row.chapterId)
                        else if (row.inFlight) root.downloader.cancelDownload(row.chapterId)
                        else row.download()
                    }
                    Component.onCompleted: refreshStatus()
                    // Thumbnails are asked for only once the page has been stayed on — flipping
                    // through pages quickly never queues fetches for pages you skipped.
                    Timer { interval: 250; running: true; repeat: false; onTriggered: row.requestThumb() }

                    Connections {
                        target: root.downloader
                        ignoreUnknownSignals: true
                        function onProgress(id, done, total) { if (String(id) === row.chapterId) { row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total } }
                        function onFinished(id) { if (String(id) === row.chapterId) row.dlState = "done" }
                        function onFailed(id, reason) { if (String(id) === row.chapterId) row.dlState = "error" }
                        function onRemoved(id) { if (String(id) === row.chapterId) { row.dlState = "none"; row.liveThumb = ""; row.requestThumb() } }
                        function onThumbReady(id, url) { if (String(id) === row.chapterId) row.liveThumb = String(url || "") }
                    }

                    Rectangle {
                        x: theme.margin
                        y: 3
                        width: parent.width - 2 * theme.margin
                        height: parent.height - 6
                        radius: 10
                        color: row.focused ? Qt.rgba(0.94, 0.77, 0.29, 0.10) : "transparent"
                        border.width: row.focused ? 3 : 0
                        border.color: theme.gold
                    }
                    Rectangle {
                        x: theme.margin + 12; width: parent.width - 2 * theme.margin - 24
                        anchors.bottom: parent.bottom; height: 1
                        color: Qt.rgba(1, 1, 1, 0.05)
                        visible: !row.focused
                    }

                    Text {
                        id: numText
                        x: theme.margin + 8
                        width: 70
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: String(row.modelData.number !== undefined && row.modelData.number !== null ? row.modelData.number : "?")
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 22; font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                    Item {
                        id: thumb
                        x: numText.x + numText.width + 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 52; height: 74
                        Rectangle { anchors.fill: parent; radius: 5; color: "#15171f"; border.width: 1
                                    border.color: row.dlState === "done" ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge }
                        Text { anchors.centerIn: parent; visible: thumbImage.status !== Image.Ready
                               text: String(row.modelData.number || "?"); color: theme.inkDimmer
                               font.family: theme.display; font.pixelSize: 16 }
                        Image {
                            id: thumbImage
                            anchors.fill: parent; anchors.margins: 1
                            source: row.thumbUrl
                            visible: status === Image.Ready
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true; cache: true
                            sourceSize.width: 104
                        }
                    }
                    Column {
                        anchors.left: thumb.right; anchors.leftMargin: 18
                        anchors.right: trailing.left; anchors.rightMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text {
                            width: parent.width
                            text: row.chapterLabel
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            // Only what differs from "available" is said out loud.
                            text: row.dlState === "done" ? "Downloaded"
                                : row.dlState === "queued" ? "Queued…"
                                : row.dlState === "downloading" ? (row.dlTotal > 0 ? ("Downloading " + Math.round(row.dlDone / row.dlTotal * 100) + "%") : "Downloading…")
                                : row.dlState === "error" ? "Failed · press the retry button" : ""
                            color: row.dlState === "done" ? theme.gold : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }

                    KeyboardAction {
                        id: rowAction
                        objectName: "mangaChapterRow_" + row.chapterId
                        anchors.fill: parent
                        anchors.leftMargin: theme.margin
                        anchors.rightMargin: theme.margin + 70
                        showFocusFrame: false
                        accessibleName: "Read " + row.chapterLabel
                        onTriggered: root.readChapterRequested(row.chapterId, row.chapterLabel)
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Down && row.index + 1 < chapterRepeater.count) {
                                chapterRepeater.itemAt(row.index + 1).takeFocus(); event.accepted = true
                            } else if (event.key === Qt.Key_Up && row.index > 0) {
                                chapterRepeater.itemAt(row.index - 1).takeFocus(); event.accepted = true
                            } else if (event.key === Qt.Key_Right) {
                                trailingAction.forceActiveFocus(Qt.TabFocusReason); event.accepted = true
                            } else if (event.key === Qt.Key_PageDown) {
                                root.stepPage(1); event.accepted = true
                            } else if (event.key === Qt.Key_PageUp) {
                                root.stepPage(-1); event.accepted = true
                            } else if (event.text.length === 1 && event.text >= "0" && event.text <= "9") {
                                goTo.text = event.text
                                goTo.forceActiveFocus(Qt.ShortcutFocusReason)
                                goTo.cursorPosition = goTo.text.length
                                event.accepted = true
                            }
                        }
                        onActiveFocusChanged: if (activeFocus) {
                            var p = row.mapToItem(listCol, 0, 0)
                            if (p.y + row.height > scroll.contentY + scroll.height) scroll.contentY = p.y + row.height - scroll.height + 12
                            else if (p.y < scroll.contentY) scroll.contentY = Math.max(0, p.y - 12)
                        }
                    }
                    MouseArea {     // hover is focus; the KeyboardAction below it still owns the click
                        anchors.fill: rowAction
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onPositionChanged: if (!rowAction.activeFocus) rowAction.forceActiveFocus(Qt.MouseFocusReason)
                    }

                    Item {
                        id: trailing
                        anchors.right: parent.right; anchors.rightMargin: theme.margin + 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40; height: 40
                        Rectangle {
                            anchors.fill: parent; radius: 20
                            color: trailingAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: row.dlState === "done" ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                        }
                        Text {
                            anchors.centerIn: parent
                            text: row.dlState === "done" ? (trailingAction.interactionActive ? "✕" : "✓")
                                : row.inFlight ? "✕" : row.dlState === "error" ? "↻" : "↓"
                            color: row.dlState === "done" ? theme.gold : (trailingAction.interactionActive ? theme.gold : theme.inkDim)
                            font.pixelSize: 16
                        }
                        KeyboardAction {
                            id: trailingAction
                            objectName: "mangaChapterDownload_" + row.chapterId
                            anchors.fill: parent
                            focusRadius: 20
                            focusColor: root.focusGold
                            accessibleName: row.dlState === "done" ? ("Delete download of " + row.chapterLabel)
                                          : row.inFlight ? ("Cancel download of " + row.chapterLabel)
                                          : ("Download " + row.chapterLabel)
                            onTriggered: row.runTrailing()
                            Keys.onLeftPressed: row.takeFocus()
                        }
                    }
                }
            }

            // empty / loading / error / extension-gate states
            Item {
                width: parent.width
                height: 220
                visible: root.loading || root.errorText.length > 0 || (!root.loading && !root.chapters.length)
                Column {
                    anchors.centerIn: parent
                    spacing: 14
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.loading ? "Loading chapters…" : (root.errorText.length ? root.errorText : "No chapters available")
                        color: root.errorText.length ? "#e6a3a3" : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14
                    }
                    Rectangle {
                        visible: root.extensionGateVisible
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: extensionsLabel.implicitWidth + 32; height: 36; radius: 18
                        color: extensionsAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1; border.color: theme.edge
                        Text {
                            id: extensionsLabel
                            anchors.centerIn: parent
                            text: "Open Extensions"
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                        }
                        KeyboardAction {
                            id: extensionsAction
                            anchors.fill: parent
                            accessibleName: "Open Extensions"
                            focusRadius: 18
                            focusColor: root.focusGold
                            onTriggered: root.requestExtensions()
                        }
                    }
                }
            }
        }
    }
    ScrollGlide { id: chapterGlide; flick: scroll }
}
