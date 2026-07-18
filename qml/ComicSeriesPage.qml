// >>> PARKED 2026-07-12 (Hemanth: GetComics = brain AND content). No door routes here —
// >>> the live comic page is ComicSeries.qml (the GetComics shelf). Revive with LOCG
// >>> when an RCO/Batcave-class source restores the catalogue split. Do not delete.
// ComicSeriesPage — the comic series detail page (Tankoban mode): LOCG catalogue issue rows
// with GetComics content attached. The LOCG catalogue is the brain (issue list, never
// dark); GetComics attaches downloadable archives onto those rows via ComicResolve.
// Download-fed, manga-style: one GetComics archive per issue/collection downloads through
// the Comics pipeline, then the reader reads the extracted pages offline. Manga-grade
// layout (mock-ratified 2026-07-09): hero (cover-wash backdrop, Fraunces title, parsed
// metadata + Continue) + glass issue table + a Collected-editions shelf for TPB/Omnibus
// posts. No volume shelf — comics runs are flat.
//
// ORDER: the DISPLAY table is ascending (#1 first — Hemanth's reading-order redline), but
// the READER's chapter model stays newest-first (LOCG's native date-desc), because its
// crossing advances toward index 0 (so finishing #1 goes to #2).

import QtQuick
import QtQuick.Controls
import "ComicResolve.js" as Resolve
import "LocgApi.js" as Locg
import "ComicsApi.js" as GcApi
import "ComicsDb.js" as ComicsDb

Item {
    id: page
    property Item backdrop
    property string seriesTitle: ""
    property string cover: ""

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal readerMinimizeRequested()
    signal readerFullscreenRequested()
    signal readerCloseRequested()

    // --- resolved state ---
    property bool loading: true
    property string errorMsg: ""

    // issue rows: LOCG issues ascending (#1 first, house rule), each carrying its matched
    // GC post (or matched:false → honest dim, no verb).
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
    readonly property var chaptersModel: locgIssuesRaw.map(function(iss) {
                  var post = (gcMatch && gcMatch.byIssue) ? gcMatch.byIssue[iss.id] : null
                  return post ? { id: post.id, name: iss.title, url: post.url, cover: post.cover || "",
                                  sizeMB: post.sizeMB || 0,
                                  number: parseInt(String(iss.title || "").replace(/[^0-9]/g, ""), 10) || 0 } : null
              }).filter(function(c) { return c !== null })
              .concat(((gcMatch && gcMatch.collections) || []).map(function(p) {
                  return { id: p.id, name: p.name, url: p.url, cover: p.cover || "",
                           sizeMB: p.sizeMB || 0, number: 0 } }))

    // resume affordance (Progress records comic issues under kind "comic", id "gc:"+gcTag)
    readonly property var resumeRec: (typeof Progress !== "undefined")
        ? (gcTag.length ? Progress.get("comic", "gc:" + gcTag) : null)
        : null

    Theme { id: theme }

    // --- LOCG catalogue entry: the catalogue is the brain. Rows are LOCG's own issue
    //     list (never dark); GetComics attaches ONTO them via Resolve.matchIssues. ---
    property string locgId: ""              // "locg:<id>" — the ONLY entry
    property var locgMeta: ({})             // {publisher, rating, startYear…} from LOCG
    property bool notAvailable: false       // catalogued, but GetComics carries nothing for it

    // GC mode: LOCG issue rows + GetComics content verbs. gcMode flips true once a series
    // resolves to a GetComics tag (false while resolving or when nothing carries it).
    readonly property bool gcMode: gcTag.length > 0
    property string gcTag: ""               // GC tag slug — "gc:"+gcTag is the reader/progress seriesId
    property string gcTagId: ""             // numeric tag id — feeds GcApi.releases()
    property var locgIssuesRaw: []          // LOCG issue list, date-desc as served
    property var gcMatch: ({ byIssue: {}, collections: [] })

    // DB-first (comics-brain step 3): if the weekly comics_db.json carries this series, render the
    // ComicDbLedger straight from it — no live LOCG/GetComics resolution. Otherwise fall through to
    // the old live attach() flow below. ComicsDb.ready() falls back gracefully when the sidecar
    // isn't loaded.
    readonly property var dbSeries: (ComicsDb.ready() && page.locgId.length)
                                    ? ComicsDb.series(page.locgId) : null

    // Generation guard: this page is REUSED across series opens (Main.openComicSeries reassigns
    // locgId without unloading), and the live attach() path fires async callbacks. Every attach
    // bumps attachGen; each async callback below bails unless its captured gen is still current —
    // otherwise a slow no-match callback for a PREVIOUS series clobbers page state (notAvailable,
    // rows) on the series now on screen. Bumped on EVERY attach, incl. the DB path, so a pending
    // live callback for a prior series can't paint over a DB-backed series' ledger.
    property int attachGen: 0
    onLocgIdChanged: attach()
    function attach() {
        var gen = ++page.attachGen
        if (!locgId.length) return
        // Read ComicsDb FRESH for the branch decision — do NOT trust the page.dbSeries binding
        // here. Inside onLocgIdChanged, that binding (which also depends on locgId) may not have
        // re-evaluated yet, so it can return the PREVIOUS series' cached value: a DB-backed series
        // would then wrongly take the live path, fire Resolve.resolve, and its own no-match set
        // notAvailable=true over a fully-catalogued series. The binding stays for rendering.
        var dbHit = (ComicsDb.ready() && locgId.length) ? ComicsDb.series(locgId) : null
        if (dbHit) {                         // DB has it → the ledger renders; skip live resolution
            page.gcTag = String(locgId).replace(/^locg:/, "")   // download/reader namespace
            page.loading = false
            page.notAvailable = false
            return
        }
        notAvailable = false
        loading = true
        // clear any prior series' attach so a no-match (or a reused layer) can't leave
        // stale gcMode rows behind the "not available" overlay
        page.gcTag = ""; page.gcTagId = ""
        page.locgIssuesRaw = []
        page.gcMatch = ({ byIssue: {}, collections: [] })
        page.errorMsg = ""
        Resolve.resolve({ id: locgId, title: seriesTitle, startYear: (locgMeta.startYear || 0) },
            function(res) {
                if (gen !== page.attachGen) return           // stale: we've navigated on
                if (!res.attached) { page.loading = false; page.notAvailable = true; return }
                var parts = String(res.sourceId).split("|")
                page.gcTag = parts[0]
                page.gcTagId = parts.length > 1 ? parts[1] : ""
                page.loadGc(gen)
            })
    }
    function loadGc(gen) {
        // rows are LOCG's issue list (catalogue-first); GC attaches onto them
        Locg.series(locgId, function(det, meta) {
            if (gen !== page.attachGen) return               // stale: a newer series is on screen
            page.locgIssuesRaw = (det && det.issues) ? det.issues : []
            if (!page.locgIssuesRaw.length) {
                page.loading = false
                page.errorMsg = (meta && meta.ok) ? "No issues catalogued for this series."
                                                  : "Catalogue unavailable right now."
                return
            }
            GcApi.releases(Number(page.gcTagId), function(posts) {
                if (gen !== page.attachGen) return           // stale: drop late release data
                // fires twice on big archives (page 1, then full) — reassignment is the refresh
                page.gcMatch = Resolve.matchIssues(page.locgIssuesRaw, posts || [])
                page.loading = false
            })
        })
    }

    // metadata line: status · released · N issues (empties omitted)
    function metaLine() {
        var m = page.locgMeta || ({})
        var bits = []
        if (m.status && m.status.length) bits.push(m.status)
        if (m.released && m.released.length) bits.push(m.released)
        var nRows = page.gcIssueRows.length
        bits.push(nRows + (nRows === 1 ? " issue" : " issues"))
        return bits.join("   ·   ")
    }

    // ---- pitch-black Theatre stack: opaque base, world art, then a heavy black wash ----
    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
        opacity: 0.5
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
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

    // ---- the page: hero → glass issue table ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height + 96
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: flick }   // gold sliver, same as every page
        ScrollGlide { flick: flick }
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Column {
            id: pageCol
            x: theme.margin; y: 96
            width: parent.width - theme.margin * 2
            spacing: 26

            // ===== DB-driven ledger (comics_db.json): hero + format-grouped editions =====
            ComicDbLedger {
                id: ledger
                visible: !!page.dbSeries
                width: parent.width
                theme: theme
                dbSeries: page.dbSeries
                seriesTitle: page.seriesTitle
                gcTag: "gc:" + page.gcTag
                onReadRequested: (chId, label) => { page.openChapterId = chId; page.openChapterLabel = label }
                onAlternateSourcesRequested: (edition, chId) => torrentSources.show({
                    issueId: chId,
                    seriesId: ledger.gcTag,          // already "gc:<tag>" — do not double-prefix
                    seriesTitle: page.seriesTitle,
                    editionTitle: String(edition.display_title || edition.title || ""),
                    isbn: String(edition.isbn || ""),
                    collects: String(edition.collects || ""),
                    format: String(edition.format || ""),   // catalog format -> format-scoped safety

                    year: String(edition.published || ""),
                    cover: String(edition.cover || page.cover || "")
                })
            }

            // ===== hero (live-flow fallback when the series isn't in the DB) =====
            Row {
                visible: !page.dbSeries
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
                        text: "Comics"; color: theme.inkDimmer
                        font.family: theme.display; font.pixelSize: 26
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    width: pageCol.width - 196 - 34
                    Text {
                        text: "Comics · Tankoban"
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

            // ===== glass issue table (ascending #1 first) — live-flow fallback =====
            Rectangle {
                visible: !page.dbSeries
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
                            Text { text: page.gcIssueRows.length + " issues · #1 first"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                    }

                    // rows
                    Repeater {
                        model: page.gcIssueRows
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
                            readonly property bool gcUnmatched: !row.modelData.matched
                            readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : ""
                            readonly property var dlStore: (typeof Comics !== "undefined" ? Comics : null)

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
                                if (dlState === "dead") return "Not available from this source"
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
                                // one archive post = the volume unit; C++ does resolve→stream→extract
                                Comics.downloadIssue(row.chId, row.modelData.postUrl, "gc:" + page.gcTag,
                                                     page.seriesTitle, String(row.modelData.label || ""),
                                                     (row.modelData.sizeMB || 0) * 1024 * 1024)
                            }
                            function primary() {
                                if (row.gcUnmatched) return           // honest dim — no fake verb
                                if (row.dlState === "dead") return    // no usable source — retry can't win
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
                                // terminal "no-source" (all mirrors CF-blocked/offline) → dead, not a
                                // retryable error; everything else stays "error — tap to retry"
                                function onFailed(cid, reason) {
                                    if (cid === row.chId) row.dlState = Resolve.failureIsTerminal(reason) ? "dead" : "error"
                                }
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
                                         : (row.dlState === "error" ? "#e6a3a3" : theme.inkDim)   // dead → dim, informational
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
                                    text: (row.gcUnmatched || row.dlState === "dead") ? ""
                                        : (row.dlState === "done" ? "▸" : (row.inFlight ? "…" : "↓"))
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
                                cursorShape: (row.gcUnmatched || row.dlState === "dead") ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: row.primary()
                            }
                        }
                    }
                }
            }

            // ===== Collected editions (GC mode): TPB/Omnibus/Epic posts — the way in
            //       for old series with no per-issue posts. Same verbs, own rows. =====
            Rectangle {
                visible: !page.dbSeries && page.gcMode && ((page.gcMatch && page.gcMatch.collections) || []).length > 0
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
                                if (dlState === "dead") return "Not available from this source"
                                if (dlState === "error") return "⚠ Failed — tap to retry"
                                var bits = []
                                if (crow.modelData.year) bits.push(String(crow.modelData.year))
                                if (crow.modelData.sizeMB) bits.push(crow.modelData.sizeMB >= 1024
                                    ? (crow.modelData.sizeMB / 1024).toFixed(1) + " GB" : crow.modelData.sizeMB + " MB")
                                return bits.join("   ·   ")
                            }
                            function primary() {
                                if (typeof Comics === "undefined" || !crow.chId.length) return
                                if (dlState === "dead") return        // no usable source — retry can't win
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
                                function onFailed(cid, reason) {
                                    if (cid === crow.chId) crow.dlState = Resolve.failureIsTerminal(reason) ? "dead" : "error"
                                }
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
                                    text: crow.dlState === "dead" ? ""
                                        : (crow.dlState === "done" ? "▸" : (crow.inFlight ? "…" : "↓"))
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
                                cursorShape: crow.dlState === "dead" ? Qt.ArrowCursor : Qt.PointingHandCursor
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

    // honest empty state: catalogued, but no reading source carries it yet. This is a LIVE-path-
    // only state — a DB-backed series always has a ledger, so the banner must never show over one
    // (guards any path that leaves notAvailable set while dbSeries is present).
    Column {
        visible: page.notAvailable && !page.dbSeries
        anchors.centerIn: parent
        spacing: 8
        Text { text: "Not available from sources yet"; color: "#e8e8e8"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: "This series is catalogued, but no reading source carries it."; color: "#9a9a9a"; font.pixelSize: 13; anchors.horizontalCenter: parent.horizontalCenter }
    }

    // ---- reader overlay: Comics store + comic progress; chapters = newest-first ----
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        western: true                           // GetComics content: page/download store is Comics
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: "gc:" + page.gcTag
        seriesCover: page.cover
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.readerMinimizeRequested()
        onFullscreenRequested: page.readerFullscreenRequested()
        onCloseRequested: page.readerCloseRequested()
    }

    // ---- alternate torrent sources: full-screen picker opened from a ledger row.
    //      A sibling of the reader (they're mutually-exclusive overlays); acquisition
    //      rides the global Comics object under the same edition chId. ----
    ComicTorrentSourcesPage {
        id: torrentSources
        anchors.fill: parent
        z: 70
        backdrop: page.backdrop
    }
}
