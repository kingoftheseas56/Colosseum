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
import QtQuick.Controls
import "XoxoApi.js" as Xoxo
import "ComicResolve.js" as Resolve
import "LocgApi.js" as Locg
import "ComicsApi.js" as GcApi

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

    // GC mode rows: LOCG issues ascending (#1 first, house rule), each carrying its
    // matched GC post (or matched:false → honest dim, no verb). Same field names as
    // xoxo rows so the ONE delegate serves both modes.
    readonly property var gcIssueRows: locgIssuesRaw.slice().reverse().map(function(iss) {
        var post = (gcMatch && gcMatch.byIssue) ? gcMatch.byIssue[iss.id] : null
        return { issueId: post ? post.id : "", label: iss.title, date: iss.date || "",
                 postUrl: post ? post.url : "", sizeMB: post ? (post.sizeMB || 0) : 0,
                 matched: !!post }
    })

    // reader order = NEWEST-first (MangaReader contract: index-1 = newer, next-issue
    // walks toward 0). The DISPLAY table uses gcIssueRows (ascending, #1-first) — the
    // reader must NOT: it reads locgIssuesRaw in LOCG's native date-desc order. Chapter
    // objects carry url + sizeMB + cover so the in-reader "Download chapter" button works
    // (the western reader contract — ComicSeries.qml feeds the same {id,name,url,cover,sizeMB}).
    readonly property var chaptersModel: gcMode
        ? locgIssuesRaw.map(function(iss) {
                  var post = (gcMatch && gcMatch.byIssue) ? gcMatch.byIssue[iss.id] : null
                  return post ? { id: post.id, name: iss.title, url: post.url, cover: post.cover || "",
                                  sizeMB: post.sizeMB || 0,
                                  number: parseInt(String(iss.title || "").replace(/[^0-9]/g, ""), 10) || 0 } : null
              }).filter(function(c) { return c !== null })
              .concat(((gcMatch && gcMatch.collections) || []).map(function(p) {
                  return { id: p.id, name: p.name, url: p.url, cover: p.cover || "",
                           sizeMB: p.sizeMB || 0, number: 0 } }))
        : issuesRaw.map(function(iss) {
              return { id: iss.issueId, name: iss.label,
                       number: parseInt(String(iss.label || "").replace(/[^0-9]/g, ""), 10) || 0 }
          })

    // resume affordance (Progress records xoxo issues under kind "comic")
    readonly property var resumeRec: (typeof Progress !== "undefined")
        ? (gcMode ? (gcTag.length ? Progress.get("comic", "gc:" + gcTag) : null)
                  : (seriesId.length ? Progress.get("comic", seriesId) : null))
        : null

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

    // --- LOCG catalogue entry: the catalogue is the brain. Rows are LOCG's own issue
    //     list (never dark); GetComics attaches ONTO them via Resolve.matchIssues. ---
    property string locgId: ""              // "locg:<id>" — set INSTEAD of seriesId by catalogue opens
    property var locgMeta: ({})             // {publisher, rating, startYear…} from LOCG
    property bool notAvailable: false       // catalogued, but GetComics carries nothing for it

    // GC mode (catalogue opens): LOCG issue rows + GetComics content verbs.
    readonly property bool gcMode: gcTag.length > 0
    property string gcTag: ""               // GC tag slug — "gc:"+gcTag is the reader/progress seriesId
    property string gcTagId: ""             // numeric tag id — feeds GcApi.releases()
    property var locgIssuesRaw: []          // LOCG issue list, date-desc as served
    property var gcMatch: ({ byIssue: {}, collections: [] })

    onLocgIdChanged: attach()
    function attach() {
        if (!locgId.length) return
        notAvailable = false
        loading = true
        // clear any prior series' attach so a no-match (or a reused layer) can't leave
        // stale gcMode rows behind the "not available" overlay
        page.gcTag = ""; page.gcTagId = ""
        page.locgIssuesRaw = []
        page.gcMatch = ({ byIssue: {}, collections: [] })
        Resolve.resolve({ id: locgId, title: seriesTitle, startYear: (locgMeta.startYear || 0) },
            function(res) {
                if (!res.attached) { page.loading = false; page.notAvailable = true; return }
                var parts = String(res.sourceId).split("|")
                page.gcTag = parts[0]
                page.gcTagId = parts.length > 1 ? parts[1] : ""
                page.loadGc()
            })
    }
    function loadGc() {
        // rows are LOCG's issue list (catalogue-first); GC attaches onto them
        Locg.series(locgId, function(det, meta) {
            page.locgIssuesRaw = (det && det.issues) ? det.issues : []
            if (!page.locgIssuesRaw.length) {
                page.loading = false
                page.errorMsg = (meta && meta.ok) ? "No issues catalogued for this series."
                                                  : "Catalogue unavailable right now."
                return
            }
            GcApi.releases(Number(page.gcTagId), function(posts) {
                // fires twice on big archives (page 1, then full) — reassignment is the refresh
                page.gcMatch = Resolve.matchIssues(page.locgIssuesRaw, posts || [])
                page.loading = false
            })
        })
    }

    // metadata line: status · released · N issues · views (empties omitted)
    function metaLine() {
        var m = (page.locgMeta && page.locgMeta.publisher) ? page.locgMeta : (page.seriesMeta || ({}))
        var bits = []
        if (m.status && m.status.length) bits.push(m.status)
        if (m.released && m.released.length) bits.push(m.released)
        var nRows = page.gcMode ? page.gcIssueRows.length : page.issueRows.length
        bits.push(nRows + (nRows === 1 ? " issue" : " issues"))
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
        ScrollBar.vertical: HouseScrollBar { flick: flick }   // gold sliver, same as every page
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
                            Text { text: (page.gcMode ? page.gcIssueRows.length : page.issueRows.length) + " issues · #1 first"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                    }

                    // rows
                    Repeater {
                        model: page.gcMode ? page.gcIssueRows : page.issueRows
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            width: tableCol.width; height: 90

                            property string chId: String(row.modelData.issueId || "")
                            property string dlState: "none"   // none|resolving|queued|downloading|extracting|done|error
                            // real, not int: gc-mode progress is BYTES (TPBs run to 1GB;
                            // int caps at 2.1GB — ComicSeries.qml is the precedent)
                            property real dlDone: 0
                            property real dlTotal: 0
                            readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                                          || dlState === "resolving"   || dlState === "extracting"
                            readonly property bool gcUnmatched: page.gcMode && !row.modelData.matched
                            readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : ""
                            readonly property var dlStore: page.gcMode
                                ? (typeof Comics !== "undefined" ? Comics : null)
                                : (typeof Downloads !== "undefined" ? Downloads : null)

                            function firstLocalUrl() {
                                if (!row.dlStore || !row.chId.length) return ""
                                var lp = row.dlStore.localPages(row.chId)
                                return (lp && lp.length) ? lp[0].url : ""
                            }
                            function statusLine() {
                                if (row.gcUnmatched) return "Not on GetComics yet"
                                if (dlState === "done") return "● Downloaded"
                                if (dlState === "resolving") return "Resolving…"
                                if (dlState === "queued") return "Queued…"
                                if (dlState === "extracting") return "Extracting…"
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
                                if (!row.dlStore || !row.chId.length) return
                                row.dlState = "queued"
                                if (page.gcMode) {
                                    // one archive post = the volume unit; C++ does resolve→stream→extract
                                    Comics.downloadIssue(row.chId, row.modelData.postUrl, "gc:" + page.gcTag,
                                                         page.seriesTitle, String(row.modelData.label || ""),
                                                         (row.modelData.sizeMB || 0) * 1024 * 1024)
                                } else {
                                    Xoxo.pages(row.chId, function(urls) {
                                        if (!urls || urls.length === 0) { row.dlState = "error"; return }
                                        Downloads.downloadPages(row.chId, page.seriesId, page.seriesTitle,
                                                                String(row.modelData.label || ""), urls)
                                    })
                                }
                            }
                            function primary() {
                                if (row.gcUnmatched) return           // honest dim — no fake verb
                                if (row.dlState === "done") row.openReader()
                                else if (!row.inFlight) row.startDownload()
                            }
                            function refreshDl() {
                                if (!row.dlStore || !row.chId.length) return
                                var st = row.dlStore.statusOf(row.chId)
                                row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                            }
                            Component.onCompleted: refreshDl()
                            Connections {
                                target: row.dlStore
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
                                    color: rowMa.containsMouse && !row.gcUnmatched ? theme.gold : theme.ink
                                    opacity: row.gcUnmatched ? 0.45 : 1.0
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
                                    text: row.gcUnmatched ? "" : (row.dlState === "done" ? "▸" : (row.inFlight ? "…" : "↓"))
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
                                cursorShape: row.gcUnmatched ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: row.primary()
                            }
                        }
                    }
                }
            }

            // ===== Collected editions (GC mode): TPB/Omnibus/Epic posts — the way in
            //       for old series with no per-issue posts. Same verbs, own rows. =====
            Rectangle {
                visible: page.gcMode && ((page.gcMatch && page.gcMatch.collections) || []).length > 0
                width: parent.width
                height: collCol.height
                radius: 14
                color: theme.glassTint
                border.width: 1; border.color: theme.edge
                clip: true
                Column {
                    id: collCol
                    width: parent.width
                    Item {
                        width: parent.width; height: 56
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 24
                            anchors.verticalCenter: parent.verticalCenter; spacing: 14
                            Text { text: "Collected editions"; color: theme.ink; font.family: theme.display; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: ((page.gcMatch && page.gcMatch.collections) || []).length + " on GetComics"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                    }
                    Repeater {
                        model: (page.gcMatch && page.gcMatch.collections) || []
                        delegate: Item {
                            id: crow
                            required property var modelData
                            width: collCol.width; height: 74
                            property string chId: String(crow.modelData.id || "")
                            property string dlState: "none"
                            property real dlDone: 0     // bytes — real, not int (the ComicSeries precedent)
                            property real dlTotal: 0
                            readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                                          || dlState === "resolving"   || dlState === "extracting"
                            function statusLine() {
                                if (dlState === "done") return "● Downloaded"
                                if (dlState === "resolving") return "Resolving…"
                                if (dlState === "queued") return "Queued…"
                                if (dlState === "extracting") return "Extracting…"
                                if (dlState === "downloading")
                                    return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                                if (dlState === "error") return "⚠ Failed — tap to retry"
                                var bits = []
                                if (crow.modelData.year) bits.push(String(crow.modelData.year))
                                if (crow.modelData.sizeMB) bits.push(crow.modelData.sizeMB >= 1024
                                    ? (crow.modelData.sizeMB / 1024).toFixed(1) + " GB" : crow.modelData.sizeMB + " MB")
                                return bits.join("   ·   ")
                            }
                            function primary() {
                                if (typeof Comics === "undefined" || !crow.chId.length) return
                                if (dlState === "done") {
                                    page.openChapterId = crow.chId
                                    page.openChapterLabel = String(crow.modelData.name || "")
                                } else if (!crow.inFlight) {
                                    crow.dlState = "queued"
                                    Comics.downloadIssue(crow.chId, crow.modelData.url, "gc:" + page.gcTag,
                                                         page.seriesTitle, String(crow.modelData.name || ""),
                                                         (crow.modelData.sizeMB || 0) * 1024 * 1024)
                                }
                            }
                            function refreshDl() {
                                if (typeof Comics === "undefined" || !crow.chId.length) return
                                var st = Comics.statusOf(crow.chId)
                                crow.dlState = st.state; crow.dlDone = st.done; crow.dlTotal = st.total
                            }
                            Component.onCompleted: refreshDl()
                            Connections {
                                target: typeof Comics !== "undefined" ? Comics : null
                                function onProgress(cid, done, total) {
                                    if (cid !== crow.chId) return
                                    crow.dlState = "downloading"; crow.dlDone = done; crow.dlTotal = total
                                }
                                function onFinished(cid) { if (cid === crow.chId) crow.dlState = "done" }
                                function onFailed(cid, reason) { if (cid === crow.chId) crow.dlState = "error" }
                                function onRemoved(cid) { if (cid === crow.chId) crow.dlState = "none" }
                            }
                            Rectangle { anchors.fill: parent; color: crowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.05) }
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.right: ctrail.left; anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                Text { width: parent.width; text: crow.modelData.name || ""
                                    color: crowMa.containsMouse ? theme.gold : theme.ink
                                    font.family: theme.ui; font.pixelSize: 16; elide: Text.ElideRight }
                                Text { width: parent.width; text: crow.statusLine(); visible: text.length > 0
                                    color: crow.dlState === "done" ? theme.gold
                                         : (crow.dlState === "error" ? "#e6a3a3" : theme.inkDim)
                                    font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                            }
                            Item {
                                id: ctrail
                                anchors.right: parent.right; anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                width: 34; height: 34
                                Text { anchors.centerIn: parent
                                    text: crow.dlState === "done" ? "▸" : (crow.inFlight ? "…" : "↓")
                                    color: theme.inkDim; font.pixelSize: 18 }
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom; anchors.left: parent.left
                                height: 2; color: theme.gold
                                visible: crow.dlState === "downloading" && crow.dlTotal > 0
                                width: parent.width * (crow.dlTotal > 0 ? crow.dlDone / crow.dlTotal : 0)
                            }
                            MouseArea {
                                id: crowMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: crow.primary()
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
        western: page.gcMode                    // flips its page/download store to Comics
        comicKind: !page.gcMode                 // keeps store=Downloads for legacy xoxo issues
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.gcMode ? ("gc:" + page.gcTag) : page.seriesId
        seriesCover: page.cover
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.readerMinimizeRequested()
        onCloseRequested: page.readerCloseRequested()
    }
}
