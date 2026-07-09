// XoxoSeries — an XOXO comic's issue list (Tankoban mode). Peer of ComicSeries
// (GetComics archives); this page is the "issues" kind's home. Download-fed, manga-
// style: an xoxo issue's page URLs come from XoxoApi.pages(), download through the
// manga pipeline (Downloads.downloadPages), then the reader reads the loose pages
// offline. No scraper, no archive, no volume shelf — xoxo issues are a flat numbered
// run. Same glass-over-wallpaper language as ComicSeries/MangaSeries.

import QtQuick
import "XoxoApi.js" as Xoxo

Item {
    id: page
    property Item backdrop
    property string seriesId: ""            // "xoxo:<slug>"
    property string seriesTitle: ""
    property string cover: ""
    property string bannerBackdrop: ""

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    // reader chrome verbs (Windows-window vocabulary, same as ComicSeries)
    signal readerMinimizeRequested()
    signal readerCloseRequested()

    // --- resolved state ---
    property var issueRows: []              // [{issueId, label, date}]
    property bool loading: true
    property string errorMsg: ""

    // the reader's chapter model needs .id (crossing) + .name + .number (placeholder)
    readonly property var chaptersModel: issueRows.map(function(iss, idx) {
        return { id: iss.issueId, name: iss.label, number: (issueRows.length - idx) }
    })

    Theme { id: theme }

    Component.onCompleted: reload()
    onSeriesIdChanged: reload()
    function reload() {
        if (!seriesId.length) return
        page.loading = true
        page.errorMsg = ""
        Xoxo.issues(seriesId, function(list) {
            page.loading = false
            if (!list || list.length === 0) { page.errorMsg = "No issues found — the source may be down."; return }
            page.issueRows = list
        })
    }

    // ---- wallpaper wash + scrim (same as ComicSeries) ----
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.82) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.9) }
        }
    }
    ChromeScrim { z: 16 }

    // ---- ‹ Back ----
    BackAction {
        id: backBtn
        x: theme.margin; y: 28; z: 20
        onTriggered: page.backRequested()
    }

    // ---- window controls (minimize / power) ----
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested() }
        }
    }

    // ---- the page: hero header → flat issue list ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Column {
            id: pageCol
            x: theme.margin; y: 96
            width: parent.width - theme.margin * 2
            spacing: 18

            // hero: cover + title + issue count
            Row {
                spacing: 22
                Rectangle {
                    width: 132; height: 196; radius: 8
                    color: "#15171f"; border.width: 1; border.color: theme.edge
                    clip: true
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: page.cover; fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true; sourceSize.width: 300
                        visible: status === Image.Ready
                    }
                    Text {
                        anchors.centerIn: parent; visible: page.cover.length === 0
                        text: "XOXO"; color: theme.inkDimmer
                        font.family: theme.display; font.pixelSize: 22
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    width: pageCol.width - 132 - 22
                    Text {
                        width: parent.width
                        text: page.seriesTitle
                        color: theme.ink; font.family: theme.display; font.pixelSize: 34
                        wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                    }
                    Text {
                        text: "XOXO   ·   " + page.issueRows.length + (page.issueRows.length === 1 ? " issue" : " issues")
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15
                    }
                    Text {
                        visible: page.errorMsg.length > 0
                        text: page.errorMsg
                        color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // issue rows (bounded by the run length — a Repeater is fine, matches MangaSeries)
            Column {
                width: parent.width
                Repeater {
                    model: page.issueRows
                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        width: pageCol.width; height: 92

                        property string chId: String(row.modelData.issueId || "")
                        property string dlState: "none"   // none | queued | downloading | done | error
                        property int dlDone: 0
                        property int dlTotal: 0
                        readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                        readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : ""

                        function firstLocalUrl() {
                            if (typeof Downloads === "undefined") return ""
                            var lp = Downloads.localPages(row.chId)
                            return (lp && lp.length) ? lp[0].url : ""
                        }
                        function statusLine() {
                            if (dlState === "done") return "● Downloaded"
                            if (dlState === "queued") return "Queued…"
                            if (dlState === "downloading")
                                return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                            if (dlState === "error") return "⚠ Failed — tap to retry"
                            return ""
                        }
                        function openReader() {
                            page.openChapterId = row.chId
                            page.openChapterLabel = String(row.modelData.label || "")
                        }
                        // xoxo download = resolve page URLs (from /all) THEN queue them into the
                        // manga pipeline. Two-step: XoxoApi.pages() → Downloads.downloadPages().
                        function startDownload() {
                            if (typeof Downloads === "undefined" || !row.chId.length) return
                            row.dlState = "queued"
                            Xoxo.pages(row.chId, function(urls) {
                                if (!urls || urls.length === 0) { row.dlState = "error"; return }
                                Downloads.downloadPages(row.chId, page.seriesId, page.seriesTitle,
                                                        String(row.modelData.label || ""), urls)
                            })
                        }
                        // download-fed: tap reads a downloaded issue, else downloads it
                        function primary() {
                            if (row.dlState === "done") row.openReader()
                            else if (!row.inFlight) row.startDownload()
                        }
                        function refreshDl() {
                            if (typeof Downloads === "undefined") return
                            var st = Downloads.statusOf(row.chId)
                            row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                        }
                        Component.onCompleted: refreshDl()
                        Connections {
                            target: typeof Downloads !== "undefined" ? Downloads : null
                            function onProgress(cid, done, total) {
                                if (cid !== row.chId) return
                                row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total
                            }
                            function onFinished(cid) { if (cid === row.chId) row.dlState = "done" }
                            function onFailed(cid, reason) { if (cid === row.chId) row.dlState = "error" }
                            function onRemoved(cid) { if (cid === row.chId) row.dlState = "none" }
                        }

                        Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }

                        // thumbnail: first page once downloaded, numbered placeholder otherwise
                        Item {
                            id: thumb
                            anchors.left: parent.left; anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 56; height: 78
                            Rectangle {
                                anchors.fill: parent; radius: 5; color: "#15171f"; border.width: 1
                                border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                    text: (row.modelData.label || "").replace(/[^0-9]/g, "") || "#"
                                    color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 18 }
                            }
                            Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                source: row.thumbUrl; visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                sourceSize.width: 160 }
                        }

                        // label + status
                        Column {
                            anchors.left: thumb.right; anchors.leftMargin: 16
                            anchors.right: trailing.left; anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter; spacing: 4
                            Text { width: parent.width; text: row.modelData.label || ""
                                color: rowMa.containsMouse ? theme.gold : theme.ink
                                font.family: theme.ui; font.pixelSize: 16; elide: Text.ElideRight }
                            Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                color: row.dlState === "done" ? theme.gold
                                     : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                            Text { width: parent.width; text: row.modelData.date || ""; visible: text.length > 0 && row.dlState === "none"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                        }

                        // trailing control: download / open glyph
                        Item {
                            id: trailing
                            anchors.right: parent.right; anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34; height: 34
                            Text {
                                anchors.centerIn: parent
                                text: row.dlState === "done" ? "▸" : (row.inFlight ? "…" : "↓")
                                color: theme.inkDim; font.pixelSize: 18
                            }
                        }

                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: row.primary()
                        }
                    }
                }
            }
        }
    }

    // loading spinner
    Text {
        anchors.centerIn: parent
        visible: page.loading
        text: "Loading…"
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
    }

    // ---- reader overlay: the SAME recreated reader, Downloads store + comic progress ----
    // Direct child (not a Loader+Component) for the id-resolution reason (MangaSeries note).
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        comicKind: true                     // Downloads store + "comic" progress kind
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.seriesId
        seriesCover: page.cover
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.readerMinimizeRequested()
        onCloseRequested: page.readerCloseRequested()
    }
}
