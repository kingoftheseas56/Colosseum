// MangaChapterSeriesView — restored Theatre-inspired chapter surface (Arc 39).
// Visual oracle: Preflight Arc 39 mockups/chapter-mode-theatre-oracle-v1.html.
// One page selector only; every page materializes at most ten chapter thumbnail delegates.
import QtQuick
import QtQuick.Controls
import "MangaChapterGrouping.js" as Grouping

Item {
    id: root
    objectName: "mangaChapterSeriesView"
    readonly property bool phoneNarrow: win.androidHost && win.width < 840

    property Item backdrop
    property string seriesId: ""              // canonical Colosseum identity (mal:N)
    property string sourceSeriesId: ""        // Tankoyomi provider identity
    property string publicationLabel: "Manga"
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
    property var chapterLanguages: []
    property string selectedChapterLanguage: "en"
    property bool hasSeriesLanguageOverride: false
    property string continueChapterId: ""
    property string continueChapterLabel: ""
    property bool continueChapterReady: false
    property bool loading: false
    property string errorText: ""
    property bool sourceEnabled: true
    property bool allowTankobanMode: true
    readonly property bool extensionGateVisible: !root.sourceEnabled

    property int currentPageIndex: 0
    readonly property bool pageMenuOpen: pagePopup.opened
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
    signal chapterLanguageRequested(string code)
    signal chapterDefaultLanguageRequested()
    signal languageSettingsRequested()
    signal readChapterRequested(string chapterId, string chapterLabel)
    signal continueChapterRequested()
    signal openExtensionsRequested()

    function requestExtensions() { root.openExtensionsRequested() }
    function setPageMenuOpen(open) {
        if (open && root.pageCount > 0) pagePopup.open()
        else pagePopup.close()
    }
    function togglePageMenu() { root.setPageMenuOpen(!pagePopup.opened) }
    function selectPage(index) {
        var i = Math.max(0, Math.min(Number(index), root.pageCount - 1))
        if (!isFinite(i)) i = 0
        root.currentPageIndex = i
        pagePopup.close()
    }
    onPageCountChanged: {
        if (root.pageCount <= 0) {
            root.currentPageIndex = 0
            pagePopup.close()
        } else if (root.currentPageIndex >= root.pageCount) {
            root.currentPageIndex = root.pageCount - 1
        }
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#050608" }

    // At phone width the masthead and chapter list scroll as a single column.
    // Desktop keeps its pinned masthead and independent list unchanged.
    Flickable {
        id: phoneScroll
        visible: root.phoneNarrow
        anchors.fill: parent
        contentWidth: width
        contentHeight: phoneStack.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Item {
            id: phoneStack
            width: phoneScroll.width
            height: sharedHeader.height + scroll.height
        }
    }

    MangaSeriesSharedHeader {
        id: sharedHeader
        parent: root.phoneNarrow ? phoneStack : root
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        tankobanMode: false
        allowTankobanMode: root.allowTankobanMode
        publicationLabel: root.publicationLabel
        seriesTitle: root.seriesTitle
        banner: root.banner
        cover: root.cover
        author: root.author
        status: root.status
        year: root.year
        score: root.score
        synopsis: root.synopsis
        collectionEntry: root.collectionEntry
        chapterLanguages: root.chapterLanguages
        selectedChapterLanguage: root.selectedChapterLanguage
        hasSeriesLanguageOverride: root.hasSeriesLanguageOverride
        onChapterDefaultLanguageRequested: root.chapterDefaultLanguageRequested()
        onLanguageSettingsRequested: root.languageSettingsRequested()
        onBackRequested: root.backRequested()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onCloseRequested: root.closeRequested()
        onTankobanRequested: root.tankobanRequested()
        onChapterLanguageRequested: (code) => root.chapterLanguageRequested(code)
    }

    Flickable {
        id: scroll
        parent: root.phoneNarrow ? phoneStack : root
        anchors.top: sharedHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: root.phoneNarrow ? undefined : parent.bottom
        height: root.phoneNarrow ? content.height + 54 : 0
        interactive: !root.phoneNarrow
        contentWidth: width
        contentHeight: content.height + 54
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: scroll.width
            spacing: 0

            Item {
                width: parent.width
                height: root.phoneNarrow && root.continueChapterId.length ? 156
                    : (root.phoneNarrow ? 100 : 58)
                Text {
                    anchors.left: parent.left; anchors.leftMargin: theme.margin
                    anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                    anchors.top: root.phoneNarrow ? parent.top : undefined
                    anchors.topMargin: root.phoneNarrow ? 16 : 0
                    text: "CHAPTERS  " + root.chapters.length
                    color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 12; font.letterSpacing: 2.2
                }

                Item {
                    id: pageSelector
                    objectName: "mangaChapterPageSelector"
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                    anchors.top: root.phoneNarrow ? parent.top : undefined
                    anchors.topMargin: root.phoneNarrow ? 54 : 0
                    width: 116; height: root.phoneNarrow ? 44 : 34; z: 20
                    activeFocusOnTab: true
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: "Chapter page selector"
                    Rectangle { anchors.fill: parent; radius: 17; color: theme.glassTint; border.width: 1; border.color: root.pageMenuOpen ? theme.gold : theme.edge }
                    Text { anchors.centerIn: parent; text: root.pageSelectorLabel + "  ▾"; color: root.pageMenuOpen ? theme.gold : theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.togglePageMenu() }
                    Keys.onReturnPressed: root.togglePageMenu()
                    Keys.onEnterPressed: root.togglePageMenu()
                    Keys.onEscapePressed: pagePopup.close()
                }
                // Resume the exact saved/downloaded chapter. Missing downloads remain
                // visible as a recovery explanation without replacing Progress.
                Rectangle {
                    id: continueChapterButton
                    objectName: "mangaChapterContinueAction"
                    visible: root.continueChapterReady
                    width: Math.min(root.phoneNarrow ? parent.width - 32 : 300,
                                    continueChapterLabelText.implicitWidth + 32)
                    height: root.phoneNarrow ? 40 : 36
                    radius: 10
                    anchors.right: root.phoneNarrow ? undefined : pageSelector.left
                    anchors.rightMargin: root.phoneNarrow ? 0 : 12
                    anchors.horizontalCenter: root.phoneNarrow ? parent.horizontalCenter : undefined
                    anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                    anchors.top: root.phoneNarrow ? parent.top : undefined
                    anchors.topMargin: root.phoneNarrow ? 105 : 0
                    color: theme.gold
                    Text {
                        id: continueChapterLabelText
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: root.continueChapterLabel
                        color: "#211806"; font.family: theme.ui
                        font.pixelSize: 12; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.continueChapterRequested() }
                    KeyboardAction {
                        anchors.fill: parent; pointerEnabled: false
                        objectName: "mangaChapterContinueKeyboardAction"
                        focusEnabled: root.continueChapterReady
                        accessibleName: root.continueChapterLabel
                        focusRadius: continueChapterButton.radius
                        onTriggered: root.continueChapterRequested()
                    }
                }
                Text {
                    objectName: "mangaChapterSavedUnavailable"
                    visible: root.continueChapterId.length > 0 && !root.continueChapterReady
                    width: Math.min(root.phoneNarrow ? parent.width - 32 : 360, 320)
                    anchors.right: root.phoneNarrow ? undefined : pageSelector.left
                    anchors.rightMargin: 12
                    anchors.horizontalCenter: root.phoneNarrow ? parent.horizontalCenter : undefined
                    anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                    anchors.top: root.phoneNarrow ? parent.top : undefined
                    anchors.topMargin: root.phoneNarrow ? 105 : 0
                    text: "Saved chapter unavailable on this device · choose a source below"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            Item {
                width: parent.width
                height: ledger.height + 54
                Rectangle {
                    id: ledger
                    anchors.left: parent.left; anchors.leftMargin: root.phoneNarrow ? 12 : theme.margin
                    anchors.right: parent.right; anchors.rightMargin: root.phoneNarrow ? 12 : theme.margin
                    anchors.top: parent.top
                    height: ledgerHeader.height + Math.max(root.phoneNarrow && !root.activeChapters.length ? 156 : 0,
                                                           chapterRows.implicitHeight)
                    radius: 18
                    color: Qt.rgba(0.07, 0.08, 0.11, 0.88)
                    border.width: 1; border.color: theme.edge
                    clip: true

                    Item {
                        id: ledgerHeader
                        width: parent.width; height: root.phoneNarrow ? 136 : 64
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: root.phoneNarrow ? 16 : 24
                            anchors.right: root.phoneNarrow ? parent.right : undefined
                            anchors.rightMargin: root.phoneNarrow ? 16 : 0
                            anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                            anchors.top: root.phoneNarrow ? parent.top : undefined
                            anchors.topMargin: root.phoneNarrow ? 12 : 0
                            text: root.activeRangeLabel
                            color: theme.ink; font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: root.phoneNarrow ? 16 : 190
                            anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                            anchors.top: root.phoneNarrow ? parent.top : undefined
                            anchors.topMargin: root.phoneNarrow ? 44 : 0
                            text: root.activeChapterCount + (root.activeChapterCount === 1 ? " chapter" : " chapters")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                        }
                        Rectangle {
                            anchors.right: parent.right; anchors.rightMargin: root.phoneNarrow ? 16 : 22
                            anchors.verticalCenter: root.phoneNarrow ? undefined : parent.verticalCenter
                            anchors.top: root.phoneNarrow ? parent.top : undefined
                            anchors.topMargin: root.phoneNarrow ? 76 : 0
                            width: batchLabel.implicitWidth + 28
                            height: root.phoneNarrow ? 44 : 30; radius: 8
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
                        id: chapterRows
                        anchors.top: ledgerHeader.bottom
                        width: parent.width
                        Repeater {
                            id: chapterRepeater
                            model: root.activeChapters
                            delegate: Item {
                                id: row
                                required property var modelData
                                width: ledger.width
                                height: root.phoneNarrow
                                    ? Math.max(112, chapterTitle.implicitHeight + chapterStatus.implicitHeight + 26)
                                    : 156
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
                                    anchors.left: parent.left; anchors.leftMargin: root.phoneNarrow ? 12 : 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: root.phoneNarrow ? 32 : 52
                                    horizontalAlignment: Text.AlignHCenter
                                    text: String(modelData.number !== undefined && modelData.number !== null ? modelData.number : "?")
                                    color: theme.ink; font.family: theme.display
                                    font.pixelSize: root.phoneNarrow ? 20 : 24; font.weight: Font.DemiBold
                                }
                                Item {
                                    id: thumb
                                    anchors.left: parent.left; anchors.leftMargin: root.phoneNarrow ? 52 : 94
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: root.phoneNarrow ? 52 : 100
                                    height: root.phoneNarrow ? 76 : 140
                                    Rectangle { anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1; border.color: row.dlState === "done" ? Qt.rgba(.94,.77,.29,.5) : theme.edge }
                                    Text { anchors.centerIn: parent; visible: thumbImage.status !== Image.Ready; text: String(modelData.number || "?"); color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 28 }
                                    Image { id: thumbImage; anchors.fill: parent; anchors.margins: 1; source: row.thumbUrl; visible: status === Image.Ready; fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true; sourceSize.width: 280 }
                                }
                                Column {
                                    anchors.left: thumb.right; anchors.leftMargin: root.phoneNarrow ? 8 : 18
                                    anchors.right: trailing.left; anchors.rightMargin: root.phoneNarrow ? 6 : 18
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 6
                                    Text {
                                        id: chapterTitle
                                        width: parent.width
                                        text: row.chapterLabel
                                        color: rowMouse.containsMouse ? theme.gold : theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: root.phoneNarrow ? 14 : 17
                                        font.weight: Font.DemiBold
                                        wrapMode: root.phoneNarrow ? Text.Wrap : Text.NoWrap
                                        elide: root.phoneNarrow ? Text.ElideNone : Text.ElideRight
                                    }
                                    Text {
                                        id: chapterStatus
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
                                    anchors.right: parent.right; anchors.rightMargin: root.phoneNarrow ? 12 : 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: root.phoneNarrow ? 44 : 38
                                    height: root.phoneNarrow ? 44 : 38
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
                                    id: rowMouse
                                    anchors.fill: parent
                                    anchors.rightMargin: root.phoneNarrow ? 60 : 0
                                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.readChapterRequested(row.chapterId, row.chapterLabel)
                                }
                            }
                        }
                    }

                    Column {
                        visible: root.loading || root.errorText.length > 0 || (!root.loading && !root.chapters.length)
                        anchors.centerIn: root.phoneNarrow ? undefined : parent
                        anchors.top: root.phoneNarrow ? ledgerHeader.bottom : undefined
                        anchors.topMargin: root.phoneNarrow ? 24 : 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.phoneNarrow ? parent.width - 32 : implicitWidth
                        spacing: 12
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: root.phoneNarrow ? parent.width : implicitWidth
                            horizontalAlignment: root.phoneNarrow ? Text.AlignHCenter : Text.AlignLeft
                            wrapMode: root.phoneNarrow ? Text.WordWrap : Text.NoWrap
                            text: root.loading ? "Loading chapters..." : (root.errorText.length ? root.errorText : "No chapters available")
                            color: root.errorText.length ? "#e6a3a3" : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 14
                        }
                        Rectangle {
                            visible: root.extensionGateVisible
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: extensionsLabel.implicitWidth + 32; height: 34; radius: 17
                            color: extensionsMouse.containsMouse ? theme.glassHi : theme.glassTint
                            border.width: 1; border.color: extensionsMouse.containsMouse ? theme.gold : theme.edge
                            Text {
                                id: extensionsLabel
                                anchors.centerIn: parent
                                text: "Open Extensions"
                                color: extensionsMouse.containsMouse ? theme.gold : theme.ink
                                font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: extensionsMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.requestExtensions()
                            }
                        }
                    }
                }
            }
        }
    }
    Popup {
        id: pagePopup
        parent: pageSelector
        objectName: "mangaChapterPagePopup"
        x: pageSelector.width - width
        y: pageSelector.height + 6
        width: 150
        height: Math.min(root.pageCount * 36 + 12, 300)
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onOpened: {
            pageList.currentIndex = root.currentPageIndex
            pageList.positionViewAtIndex(pageList.currentIndex, ListView.Contain)
            pageList.forceActiveFocus(Qt.PopupFocusReason)
        }
        onClosed: if (pageSelector.visible) pageSelector.forceActiveFocus(Qt.PopupFocusReason)

        background: Rectangle {
            radius: 12
            color: Qt.rgba(.035,.04,.06,.98)
            border.width: 1
            border.color: theme.edge
        }

        contentItem: ListView {
            id: pageList
            clip: true
            model: root.pageCount
            currentIndex: root.currentPageIndex
            boundsBehavior: Flickable.StopAtBounds
            Keys.onReturnPressed: root.selectPage(currentIndex)
            Keys.onEnterPressed: root.selectPage(currentIndex)
            Keys.onEscapePressed: pagePopup.close()
            delegate: Rectangle {
                required property int index
                width: pageList.width
                height: 36
                radius: 7
                color: index === root.currentPageIndex
                    ? theme.glassHi
                    : (pageOptionMouse.containsMouse ? theme.glassTint : "transparent")
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Page " + (index + 1)
                    color: index === root.currentPageIndex ? theme.gold : theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                MouseArea {
                    id: pageOptionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectPage(index)
                }
            }
        }
    }
}
