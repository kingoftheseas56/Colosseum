// XoxoSeries — an XOXO comic's detail page (Tankoban mode). Peer of ComicSeries
// (GetComics archives); this page is the "issues" kind's home. Download-fed, manga-
// style: an xoxo issue's page URLs come from XoxoApi.pages(), download through the
// manga pipeline (Downloads.downloadPages), then the reader reads the loose pages
// offline. Manga-grade layout (mock-ratified 2026-07-09): hero (cover-wash backdrop,
// Fraunces title, parsed metadata + genre chips, Continue) + glass issue table. No
// volume shelf — comics runs are flat.
//
// ORDER: the site serves issues newest-first. The DISPLAY table is reversed to ascending
// (#1 first — Hemanth's reading-order redline), but the READER's chapter model stays
// newest-first, because its crossing advances toward index 0 (so finishing #1 goes to #2).

import QtQuick
import "XoxoApi.js" as Xoxo
import "ComicResolve.js" as Resolve

Item {
    id: page
    property Item backdrop
    property string seriesId: ""            // "xoxo:<slug>"
    property string seriesTitle: ""
    property string cover: ""

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal readerMinimizeRequested()
    signal readerCloseRequested()

    // --- resolved state ---
    property var issuesRaw: []              // newest-first, as the site serves (reader order)
    property var seriesMeta: ({})           // {status, author, genres[], released, views}
    property bool loading: true
    property string errorMsg: ""
    property int cooldownMs: 0

    // display list = ascending (#1 first); reader model = newest-first (crossing order)
    readonly property var issueRows: issuesRaw.slice().reverse()
    readonly property var chaptersModel: issuesRaw.map(function(iss) {
        return { id: iss.issueId, name: iss.label,
                 number: parseInt(String(iss.label || "").replace(/[^0-9]/g, ""), 10) || 0 }
    })

    // resume affordance (Progress records xoxo issues under kind "comic")
    readonly property var resumeRec: (typeof Progress !== "undefined" && seriesId.length)
                                     ? Progress.get("comic", seriesId) : null

    Theme { id: theme }

    Component.onCompleted: reload()
    onSeriesIdChanged: reload()
    function reload() {
        if (!seriesId.length) return
        page.loading = true
        page.errorMsg = ""
        Xoxo.issues(seriesId, function(list, meta, sMeta) {
            page.loading = false
            page.cooldownMs = (meta && meta.blocked) ? meta.retryInMs : 0
            if (sMeta) page.seriesMeta = sMeta
            if (list && list.length > 0) { page.issuesRaw = list; return }   // cached/partial keeps rendering
            if (meta && meta.blocked) return                                  // banner explains the quiet
            page.errorMsg = "No issues found for this series."
        })
    }

    // --- LOCG catalogue entry: resolve the locg:<id> to an xoxo slug, then fall through
    //     to the existing xoxo reading flow. If nothing carries it, show honest empty state. ---
    property string locgId: ""              // "locg:<id>" — set INSTEAD of seriesId by catalogue opens
    property var locgMeta: ({})             // {publisher, rating, startYear…} from LOCG
    property bool notAvailable: false       // catalogued, but no reading source carries it yet

    onLocgIdChanged: attach()
    function attach() {
        if (!locgId.length) return
        notAvailable = false
        loading = true
        Resolve.resolve({ id: locgId, title: seriesTitle, startYear: (locgMeta.startYear || 0) },
            function(res) {
                if (res.attached) page.seriesId = res.sourceId      // triggers the existing reload()
                else { page.loading = false; page.notAvailable = true }
            })
    }

    // metadata line: status · released · N issues · views (empties omitted)
    function metaLine() {
        var m = (page.locgMeta && page.locgMeta.publisher) ? page.locgMeta : (page.seriesMeta || ({}))
        var bits = []
        if (m.status && m.status.length) bits.push(m.status)
        if (m.released && m.released.length) bits.push(m.released)
        bits.push(page.issueRows.length + (page.issueRows.length === 1 ? " issue" : " issues"))
        if (m.views && m.views.length) bits.push(fmtViews(m.views) + " views")
        return bits.join("   ·   ")
    }
    // "1.704.194" (dot-grouped) → "1.7M"
    function fmtViews(v) {
        var n = parseInt(String(v).replace(/[^0-9]/g, ""), 10)
        if (!n) return String(v)
        if (n >= 1000000) return (n / 1000000).toFixed(1).replace(/\.0$/, "") + "M"
        if (n >= 1000) return Math.round(n / 1000) + "K"
        return String(n)
    }

    // ---- backdrop: the cover washed dark, bleeding from the right (house language) ----
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.82) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.94) }
        }
    }
    Image {
        anchors.right: parent.right; anchors.top: parent.top
        width: parent.width * 0.5; height: parent.height
        source: page.cover
        fillMode: Image.PreserveAspectCrop
        opacity: 0.14
        visible: status === Image.Ready
        asynchronous: true; cache: true
        layer.enabled: true
        // fade the left edge so it bleeds into the glass, not a hard seam
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(0.02, 0.025, 0.04, 1) }
                GradientStop { position: 0.9; color: Qt.rgba(0.02, 0.025, 0.04, 0) }
            }
        }
    }
    ChromeScrim { z: 16 }

    // ---- ‹ Back ----
    BackAction { id: backBtn; x: theme.margin; y: 28; z: 20; onTriggered: page.backRequested() }

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

    // ---- source cooldown banner ----
    SourceCooldownBanner {
        z: 40
        anchors.top: parent.top; anchors.topMargin: 84
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - theme.margin * 2, 460)
        retryInMs: page.cooldownMs
        onRetry: page.reload()
    }

    // ---- the page: hero → glass issue table ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height + 96
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Column {
            id: pageCol
            x: theme.margin; y: 96
            width: parent.width - theme.margin * 2
            spacing: 26

            // ===== hero =====
            Row {
                spacing: 34
                Rectangle {
                    width: 196; height: 294; radius: 10
                    color: "#15171f"; border.width: 1; border.color: theme.edge
                    clip: true
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: page.cover; fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true; sourceSize.width: 400
                        visible: status === Image.Ready
                    }
                    Text {
                        anchors.centerIn: parent; visible: page.cover.length === 0
                        text: "XOXO"; color: theme.inkDimmer
                        font.family: theme.display; font.pixelSize: 26
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    width: pageCol.width - 196 - 34
                    Text {
                        text: "XOXO · TANKOBAN"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.seriesTitle
                        color: theme.ink; font.family: theme.display; font.pixelSize: 48
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                    }
                    Text {
                        text: page.metaLine()
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15
                    }
                    // genre chips (the first is the publisher tag — the source mixes them)
                    Flow {
                        width: parent.width; spacing: 8
                        visible: (page.seriesMeta.genres || []).length > 0
                        Repeater {
                            model: page.seriesMeta.genres || []
                            delegate: Rectangle {
                                required property var modelData
                                height: 24; radius: 12
                                width: chipText.implicitWidth + 22
                                color: theme.glassTint; border.width: 1; border.color: theme.edge
                                Text { id: chipText; anchors.centerIn: parent; text: modelData
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                            }
                        }
                    }
                    // Continue affordance
                    Rectangle {
                        visible: !!page.resumeRec && !!page.resumeRec.resume
                        width: contRow.implicitWidth + 36; height: 40; radius: 9
                        color: theme.gold
                        Row {
                            id: contRow; anchors.centerIn: parent; spacing: 10
                            Text { text: "▸"; color: "#1a1306"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "Continue"; color: "#1a1306"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                visible: !!page.resumeRec && !!page.resumeRec.resume
                                text: {
                                    var r = page.resumeRec && page.resumeRec.resume ? page.resumeRec.resume : ({})
                                    return r.chapterLabel ? r.chapterLabel : "Resume"
                                }
                                color: Qt.rgba(0.1, 0.075, 0.02, 0.66); font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var r = page.resumeRec && page.resumeRec.resume ? page.resumeRec.resume : null
                                if (r && r.chapterId) { page.openChapterId = r.chapterId; page.openChapterLabel = r.chapterLabel || "" }
                            }
                        }
                    }
                    Text {
                        visible: page.errorMsg.length > 0
                        text: page.errorMsg
                        color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // ===== glass issue table (ascending #1 first) =====
            Rectangle {
                width: parent.width
                height: tableCol.height
                radius: 14
                color: theme.glassTint
                border.width: 1; border.color: theme.edge
                clip: true
                Column {
                    id: tableCol
                    width: parent.width

                    // header
                    Item {
                        width: parent.width; height: 56
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 24
                            anchors.verticalCenter: parent.verticalCenter; spacing: 14
                            Text { text: "Issues"; color: theme.ink; font.family: theme.display; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: page.issueRows.length + " issues · #1 first"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                    }

                    // rows
                    Repeater {
                        model: page.issueRows
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            width: tableCol.width; height: 90

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
                            function startDownload() {
                                if (typeof Downloads === "undefined" || !row.chId.length) return
                                row.dlState = "queued"
                                Xoxo.pages(row.chId, function(urls) {
                                    if (!urls || urls.length === 0) { row.dlState = "error"; return }
                                    Downloads.downloadPages(row.chId, page.seriesId, page.seriesTitle,
                                                            String(row.modelData.label || ""), urls)
                                })
                            }
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
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.05) }

                            // numbered thumbnail
                            Rectangle {
                                id: thumb
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                width: 52; height: 74; radius: 5
                                color: "#15171f"; border.width: 1
                                border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                clip: true
                                Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                    text: (row.modelData.label || "").replace(/[^0-9]/g, "") || "#"
                                    color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 17 }
                                Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                    source: row.thumbUrl; visible: status === Image.Ready
                                    fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                    sourceSize.width: 160 }
                            }

                            // label + status
                            Column {
                                anchors.left: thumb.right; anchors.leftMargin: 16
                                anchors.right: rdate.left; anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                Text { width: parent.width; text: row.modelData.label || ""
                                    color: rowMa.containsMouse ? theme.gold : theme.ink
                                    font.family: theme.ui; font.pixelSize: 16; elide: Text.ElideRight }
                                Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                    color: row.dlState === "done" ? theme.gold
                                         : (row.dlState === "error" ? "#e6a3a3" : theme.inkDim)
                                    font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                            }

                            // date (right-aligned, before the trailing action)
                            Text {
                                id: rdate
                                anchors.right: trailing.left; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.dlState === "none" ? (row.modelData.date || "") : ""
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }

                            // trailing action glyph
                            Item {
                                id: trailing
                                anchors.right: parent.right; anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                width: 34; height: 34
                                Text { anchors.centerIn: parent
                                    text: row.dlState === "done" ? "▸" : (row.inFlight ? "…" : "↓")
                                    color: theme.inkDim; font.pixelSize: 18 }
                            }

                            // gold progress line while downloading
                            Rectangle {
                                anchors.bottom: parent.bottom; anchors.left: parent.left
                                height: 2; color: theme.gold
                                visible: row.dlState === "downloading" && row.dlTotal > 0
                                width: parent.width * (row.dlTotal > 0 ? row.dlDone / row.dlTotal : 0)
                            }

                            MouseArea {
                                id: rowMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: row.primary()
                            }
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

    // honest empty state: catalogued, but no reading source carries it yet
    Column {
        visible: page.notAvailable
        anchors.centerIn: parent
        spacing: 8
        Text { text: "Not available from sources yet"; color: "#e8e8e8"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: "This series is catalogued, but no reading source carries it."; color: "#9a9a9a"; font.pixelSize: 13; anchors.horizontalCenter: parent.horizontalCenter }
    }

    // ---- reader overlay: Downloads store + comic progress; chapters = newest-first ----
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        comicKind: true
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
