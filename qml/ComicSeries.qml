// ComicSeries — the western-comics series page (Tankoban mode). A GetComics tag IS
// the series (ratified 2026-07-04: GetComics for both catalog and download, iTunes
// posters on top, no metadata brain in v1). The shelf is the tag's release posts,
// newest-first, lightly grouped: collections (TPB/omnibus/treasury) lead, single
// issues follow. Each release = ONE volume unit (TB2-ratified: TPB is king) — a
// single archive download, extracted by `Comics` into a page dir MangaReader eats.
// Covers: each release's own og_image (exact by construction); iTunes art is the
// series-level hero only. Same glass-over-wallpaper language as MangaSeries.

import QtQuick
import QtQuick.Controls
import "ComicsApi.js" as Api
import "ComicResolve.js" as Resolve

Item {
    id: page
    property Item backdrop
    property string seriesTitle: ""
    property string tagSlug: ""            // GetComics tag slug — the series identity
    property int    tagId: 0               // WP tag id (0 = resolve from the slug)
    property string poster: ""             // iTunes series art (fetched here if empty)
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    // reader chrome verbs (Windows-window vocabulary, same as MangaSeries)
    signal readerMinimizeRequested()
    signal readerCloseRequested()

    // --- resolved state ---
    property var releases: []              // [{id,url,name,cover,year,sizeMB,synopsis,collection}]
    property int totalReleases: 0          // GetComics' own count (> releases.length while loading / past cap)
    property bool loading: true
    property string errorMsg: ""
    readonly property string seriesId: "gc:" + tagSlug   // the app-wide western series id

    // --- shelf navigation: filter + sort (big series run to hundreds of releases).
    // View-only — the reader's chapter list stays date-newest-first so its
    // next/previous crossing keeps meaning regardless of how the shelf is sorted. ---
    property string filter: ""
    property string sortMode: "new"        // new | old | az
    // natural compare: digit runs compare as numbers, so "#2" sorts before "#10"
    function natCmp(a, b) {
        var ax = String(a).toLowerCase().match(/(\d+)|(\D+)/g) || []
        var bx = String(b).toLowerCase().match(/(\d+)|(\D+)/g) || []
        for (var i = 0; i < Math.max(ax.length, bx.length); i++) {
            var av = ax[i], bv = bx[i]
            if (av === undefined) return -1
            if (bv === undefined) return 1
            if (/^\d/.test(av) && /^\d/.test(bv)) {
                var d = Number(av) - Number(bv)
                if (d) return d
            } else if (av !== bv) return av < bv ? -1 : 1
        }
        return 0
    }
    readonly property var shown: {
        var list = releases
        var f = filter.trim().toLowerCase()
        if (f.length) list = list.filter(function(r) { return r.name.toLowerCase().indexOf(f) >= 0 })
        if (sortMode === "old") {
            // publication order, not site-upload order: the parsed Year is the
            // comic's own year (upload date would put a 2015 re-up before #1).
            list = list.slice()
            list.sort(function(a, b) {
                var ay = a.year || 9999, by = b.year || 9999
                return (ay - by) || page.natCmp(a.name, b.name)
            })
        } else if (sortMode === "az") {
            list = list.slice()
            list.sort(function(a, b) { return page.natCmp(a.name, b.name) })
        }
        return list
    }
    readonly property var collections: shown.filter(function(r) { return r.collection })
    readonly property var issues: shown.filter(function(r) { return !r.collection })
    // the reader's chapter list: every release, newest-first (crossing = next issue)
    readonly property var chaptersModel: releases.map(function(r) {
        return { id: r.id, name: r.name, url: r.url, cover: r.cover, sizeMB: r.sizeMB }
    })

    Theme { id: theme }

    onTagSlugChanged: resolve()
    Component.onCompleted: if (tagSlug.length || seriesTitle.length) resolve()

    function resolve() {
        loading = true; errorMsg = ""; releases = []
        revealGuard.restart()
        if (!poster.length && seriesTitle.length)
            Api.posterFor(seriesTitle + " comic", function(art) { if (art) page.poster = art })
        if (tagId > 0) { loadReleases(); return }
        if (!tagSlug.length) {
            // title-only open (a Top-10 tile, a genre-page tile): SLUG-FIRST (2026-07-12).
            // WP's tag search is token-OR + count-ordered — popular titles get flooded out
            // of their own results ("Absolute Batman" ranks under "Batman", 1417 releases),
            // so blind tags[0] opened the WRONG franchise shelf or nothing. The exact slug
            // derived from the title has no ambiguity; the ranked search stays as fallback,
            // preferring an exact normalized-title hit over tags[0] when it must guess.
            if (!seriesTitle.length) { errorMsg = "No series tag."; loading = false; return }
            var wantNorm = String(seriesTitle).toLowerCase()
                .replace(/\(\d{4}\)/g, "").replace(/\[[^\]]*\]/g, "")
                .replace(/[^a-z0-9]+/g, " ").replace(/\s+/g, " ").trim()
            Api.tagBySlug(wantNorm.replace(/\s+/g, "-"), function(exact) {
                if (exact) {
                    page.tagId = exact.tagId
                    page.tagSlug = exact.tag    // change signal re-enters resolve() on the tagId path
                    return
                }
                Api.searchSeries(page.seriesTitle, function(tags) {
                    if (!tags || !tags.length) {
                        page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on GetComics."
                        page.loading = false
                        return
                    }
                    var pick = tags[0]
                    for (var i = 0; i < tags.length; i++) {
                        var tn = String(tags[i].title).toLowerCase()
                            .replace(/[^a-z0-9]+/g, " ").replace(/\s+/g, " ").trim()
                        if (tn === wantNorm) { pick = tags[i]; break }
                    }
                    page.tagId = pick.tagId
                    page.tagSlug = pick.tag
                })
            })
            return
        }
        Api.tagBySlug(tagSlug, function(t) {
            if (!t) { page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on GetComics."; page.loading = false; return }
            page.tagId = t.tagId
            if (!page.seriesTitle.length) page.seriesTitle = t.title
            loadReleases()
        })
    }
    function loadReleases() {
        Api.releases(tagId, function(rs, total) {   // fires twice on multi-page series: page 1, then all
            page.releases = rs || []
            page.totalReleases = total || page.releases.length
            page.loading = false
            revealGuard.stop()
            if (!page.releases.length) page.errorMsg = "No releases under this series tag."
        })
    }
    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    function fmtMB(mb) {
        if (!mb) return ""
        return mb >= 1024 ? (mb / 1024).toFixed(1) + " GB" : mb + " MB"
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                      // absorb clicks from the world below

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

    // ---- the page: banner hero → release shelf (glass table, collections then issues) ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.loading ? 0.0 : 1.0
        ScrollBar.vertical: HouseScrollBar { flick: flick }
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            // ── BANNER HERO — iTunes series art washed into the page ──
            Item {
                width: parent.width
                height: 320

                Image {
                    anchors.fill: parent
                    source: page.poster
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                    opacity: status === Image.Ready ? 0.55 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.035, 0.055, 0.25) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.03, 0.035, 0.05, 0.55) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.92) }
                    }
                }

                Row {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.bottomMargin: 28
                    spacing: 26

                    // the series poster as a real cover, not just a wash
                    Rectangle {
                        width: 128; height: 190; visible: page.poster.length > 0
                        color: "#1a1c24"; border.width: 1; border.color: Qt.rgba(1,1,1,0.14)
                        anchors.bottom: parent.bottom
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            source: page.poster
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true; cache: true
                        }
                    }
                    Column {
                        spacing: 12
                        anchors.bottom: parent.bottom
                        Text {
                            text: "Western Comics · Tankoban"
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                            font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                        }
                        Text {
                            width: flick.width - 2 * theme.margin - 180
                            text: page.seriesTitle
                            color: theme.ink; font.family: theme.display; font.pixelSize: 54
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                        }
                        // INLINE metadata — bright count + dim medium (the transparent-tablet law)
                        Row {
                            spacing: 11
                            Text { text: page.releases.length
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "releases" + (page.totalReleases > page.releases.length
                                                       ? " of " + page.totalReleases : "")
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { visible: page.collections.length > 0; text: "·"
                                color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                            Text { visible: page.collections.length > 0
                                text: page.collections.length + " collected editions"
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "GetComics"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }

            // ── SHELF CONTROLS — filter + sort; view-only, the reader is untouched ──
            Item {
                width: parent.width; height: 64
                visible: page.releases.length > 0

                // search-within-the-shelf (left): underlined inline field, no chrome
                Item {
                    id: filterBox
                    x: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(380, parent.width * 0.4); height: 36
                    TextInput {
                        id: filterInput
                        anchors.left: parent.left; anchors.right: clearBtn.visible ? clearBtn.left : parent.right
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        verticalAlignment: TextInput.AlignVCenter
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 15
                        clip: true; selectByMouse: true
                        onTextChanged: page.filter = text
                    }
                    Text {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        visible: filterInput.text.length === 0 && !filterInput.activeFocus
                        text: "Search releases…"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 15
                    }
                    Item {
                        id: clearBtn
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: 24; height: 24
                        visible: filterInput.text.length > 0
                        Text { anchors.centerIn: parent; text: "✕"
                            color: clearMa.containsMouse ? theme.gold : theme.inkDim; font.pixelSize: 13 }
                        MouseArea { id: clearMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: filterInput.text = "" }
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1
                        color: filterInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.7) : theme.edge
                    }
                }
                // live match count while filtering (inline, dim)
                Text {
                    anchors.left: filterBox.right; anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.filter.trim().length > 0
                    text: page.shown.length + (page.shown.length === 1 ? " match" : " matches")
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                }
                // sort (right): Newest · Oldest · A–Z — A–Z is numeric-aware (#2 before #10)
                Row {
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 22
                    Repeater {
                        model: [{ k: "new", l: "Newest" }, { k: "old", l: "Oldest" }, { k: "az", l: "A–Z" }]
                        delegate: Item {
                            id: sopt
                            required property var modelData
                            width: soptText.implicitWidth; height: 26
                            readonly property bool on: page.sortMode === sopt.modelData.k
                            Text { id: soptText; text: sopt.modelData.l
                                color: sopt.on ? theme.gold : (soptMa.containsMouse ? theme.ink : theme.inkDim)
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: sopt.on ? Font.DemiBold : Font.Normal }
                            Rectangle { visible: sopt.on; anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 18; height: 2; radius: 2; color: theme.gold }
                            MouseArea { id: soptMa; anchors.fill: parent; anchors.margins: -4
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: page.sortMode = sopt.modelData.k }
                        }
                    }
                }
            }

            // ── RELEASE TABLE — collections lead, issues follow, one glass widget ──
            Item {
                width: parent.width
                height: relTable.height + 24
                visible: page.shown.length > 0

                Glass {
                    id: relTable
                    x: theme.margin
                    width: parent.width - 2 * theme.margin
                    height: tableInner.height
                    radius: 18
                    backdrop: page.backdrop
                    track: flick.contentY

                    Column {
                        id: tableInner
                        width: parent.width

                        Repeater {
                            model: [
                                { label: "COLLECTED EDITIONS", items: page.collections },
                                { label: "ISSUES", items: page.issues }
                            ]
                            delegate: Column {
                                id: section
                                required property var modelData
                                width: tableInner.width
                                visible: section.modelData.items.length > 0

                                // section header
                                Item {
                                    width: parent.width; height: 52
                                    Row {
                                        anchors.left: parent.left; anchors.leftMargin: 24
                                        anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                        Text { text: section.modelData.label; color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 13; font.letterSpacing: 3
                                            anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: section.modelData.items.length; color: theme.ink
                                            font.family: theme.display; font.pixelSize: 15; font.weight: Font.Bold
                                            anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                                }

                                Repeater {
                                    model: section.modelData.items
                                    delegate: Item {
                                        id: row
                                        required property var modelData
                                        width: tableInner.width; height: 96

                                        property string relId: String(row.modelData.id || "")
                                        property string dlState: "none"   // none|resolving|queued|downloading|extracting|done|error
                                        property real dlDone: 0
                                        property real dlTotal: 0
                                        readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                                                       || dlState === "resolving" || dlState === "extracting"
                                        function statusLine() {
                                            if (dlState === "done") return "● Downloaded"
                                            if (dlState === "resolving") return "Resolving link…"
                                            if (dlState === "queued") return "Queued…"
                                            if (dlState === "extracting") return "Extracting pages…"
                                            if (dlState === "downloading")
                                                return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%")
                                                                   : "Downloading…"
                                            if (dlState === "dead") return "Not available from this source"
                                            if (dlState === "error") return "⚠ Failed — tap to retry"
                                            var bits = []
                                            if (row.modelData.year) bits.push(row.modelData.year)
                                            if (row.modelData.sizeMB) bits.push(page.fmtMB(row.modelData.sizeMB))
                                            return bits.join(" · ")
                                        }
                                        function openReader() {
                                            page.openChapterId = row.relId
                                            page.openChapterLabel = row.modelData.name
                                        }
                                        function startDownload() {
                                            if (typeof Comics === "undefined" || !row.relId.length) return
                                            row.dlState = "queued"
                                            Comics.downloadIssue(row.relId, row.modelData.url, page.seriesId,
                                                                 page.seriesTitle, row.modelData.name,
                                                                 (row.modelData.sizeMB || 0) * 1024 * 1024)
                                        }
                                        // download-fed: tap reads what's on disk, else downloads it
                                        function primary() {
                                            if (row.dlState === "dead") return   // no usable source — retry can't win
                                            if (row.dlState === "done") row.openReader()
                                            else if (!row.inFlight) row.startDownload()
                                        }
                                        function refreshDl() {
                                            if (typeof Comics === "undefined") return
                                            var st = Comics.statusOf(row.relId)
                                            row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                                        }
                                        Component.onCompleted: refreshDl()
                                        Connections {
                                            target: typeof Comics !== "undefined" ? Comics : null
                                            function onProgress(cid, done, total) {
                                                if (cid !== row.relId) return
                                                row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total
                                            }
                                            function onFinished(cid) { if (cid === row.relId) row.dlState = "done" }
                                            // terminal "no-source" (all mirrors CF-blocked/offline) → dead, not retryable
                                            function onFailed(cid, reason) {
                                                if (cid === row.relId) row.dlState = Resolve.failureIsTerminal(reason) ? "dead" : "error"
                                            }
                                            function onRemoved(cid) { if (cid === row.relId) row.dlState = "none" }
                                        }
                                        // extraction has no byte progress — poll state while in flight
                                        Timer {
                                            interval: 700; repeat: true
                                            running: row.inFlight
                                            onTriggered: row.refreshDl()
                                        }

                                        Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }

                                        // release cover (GetComics og_image — exact per release)
                                        Item {
                                            id: thumb
                                            anchors.left: parent.left; anchors.leftMargin: 22
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 58; height: 82
                                            Rectangle {
                                                anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1
                                                border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                                Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                                    text: "#"; color: theme.inkDimmer
                                                    font.family: theme.display; font.pixelSize: 22 }
                                            }
                                            Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                                source: row.modelData.cover || ""; visible: status === Image.Ready
                                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                                sourceSize.width: 170
                                                // wp.com 429-throttles a 28-cover burst — staggered re-request lands them
                                                property int retries: 0
                                                onStatusChanged: if (status === Image.Error && retries < 2) coverRetry.restart()
                                                Timer { id: coverRetry; interval: 1200 + Math.random() * 2400
                                                    onTriggered: { thumbImg.retries += 1
                                                        var s = row.modelData.cover || ""
                                                        thumbImg.source = ""; thumbImg.source = s } }
                                            }
                                        }

                                        Column {
                                            anchors.left: thumb.right; anchors.leftMargin: 16
                                            anchors.right: trailing.left; anchors.rightMargin: 14
                                            anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                            Text { width: parent.width; text: row.modelData.name
                                                color: rowMa.containsMouse ? theme.gold : theme.ink
                                                font.family: theme.ui; font.pixelSize: 16; elide: Text.ElideRight }
                                            Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                                color: row.dlState === "done" ? theme.gold
                                                     : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                                font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                                        }

                                        // trailing control: ✓→✕ delete · ✕ cancel · ↓/↻ download/retry
                                        Item {
                                            id: trailing
                                            anchors.right: parent.right; anchors.rightMargin: 22
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 36; height: 36
                                            Rectangle { anchors.fill: parent; radius: 18
                                                color: trMa.containsMouse ? theme.glassHi : "transparent" }
                                            Text {
                                                anchors.centerIn: parent
                                                text: row.dlState === "dead" ? ""
                                                    : row.dlState === "done" ? (trMa.containsMouse ? "✕" : "✓")
                                                    : row.inFlight ? "✕"
                                                    : row.dlState === "error" ? "↻" : "↓"
                                                color: (row.dlState === "done" && trMa.containsMouse) ? "#e6a3a3"
                                                     : row.dlState === "done" ? theme.gold
                                                     : trMa.containsMouse ? theme.gold : theme.inkDim
                                                font.pixelSize: 16
                                                font.weight: (row.dlState === "done" && !trMa.containsMouse) ? Font.Bold : Font.Normal
                                            }
                                            MouseArea {
                                                id: trMa; anchors.fill: parent; hoverEnabled: true; z: 5
                                                enabled: row.dlState !== "dead"   // no usable source — no verb
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (typeof Comics === "undefined") return
                                                    if (row.dlState === "done") Comics.deleteIssue(row.relId)
                                                    else if (row.inFlight) Comics.cancelDownload(row.relId)
                                                    else row.startDownload()
                                                }
                                            }
                                        }

                                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                            color: Qt.rgba(1,1,1,0.05) }
                                        MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                            cursorShape: row.dlState === "dead" ? Qt.ArrowCursor : Qt.PointingHandCursor
                                            onClicked: row.primary() }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // filter came up empty (the shelf itself isn't)
            Text {
                visible: !page.loading && page.releases.length > 0 && page.shown.length === 0
                x: theme.margin
                text: "No releases match “" + page.filter.trim() + "”."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                topPadding: 10
            }

            // post-reveal error (inset)
            Text {
                visible: !page.loading && page.errorMsg.length > 0
                x: theme.margin
                text: page.errorMsg
                color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }
        }
    }

    ScrollGlide { flick: flick }

    // ---- scroll bar: always visible when the page overflows, draggable, click-to-jump ----
    Item {
        id: sbar
        z: 26
        visible: flick.contentHeight > flick.height + 8 && !page.loading
        anchors.right: parent.right; anchors.rightMargin: 4
        y: 76
        width: 12
        height: parent.height - 96
        Rectangle {   // track
            anchors.horizontalCenter: parent.horizontalCenter
            width: 3; height: parent.height; radius: 1.5
            color: Qt.rgba(1, 1, 1, 0.07)
        }
        Rectangle {
            id: sHandle
            anchors.horizontalCenter: parent.horizontalCenter
            width: (sMa.containsMouse || sMa.drag.active) ? 7 : 5
            radius: width / 2
            height: Math.max(40, sbar.height * flick.height / Math.max(1, flick.contentHeight))
            color: sMa.drag.active ? theme.gold
                 : (sMa.containsMouse ? Qt.rgba(1, 1, 1, 0.55) : Qt.rgba(1, 1, 1, 0.28))
            Behavior on width { NumberAnimation { duration: 100 } }
        }
        // content drives the handle EXCEPT while the handle itself is being dragged
        Binding {
            target: sHandle; property: "y"
            value: (sbar.height - sHandle.height)
                   * (flick.contentY / Math.max(1, flick.contentHeight - flick.height))
            when: !sMa.drag.active
        }
        MouseArea {
            id: sMa
            anchors.fill: parent; hoverEnabled: true
            drag.target: sHandle; drag.axis: Drag.YAxis
            drag.minimumY: 0; drag.maximumY: Math.max(0, sbar.height - sHandle.height)
            onPositionChanged: {
                if (!drag.active) return
                flick.contentY = sHandle.y / Math.max(1, sbar.height - sHandle.height)
                                 * (flick.contentHeight - flick.height)
            }
            onClicked: function(m) {
                var frac = (m.y - sHandle.height / 2) / Math.max(1, sbar.height - sHandle.height)
                flick.contentY = Math.max(0, Math.min(1, frac)) * (flick.contentHeight - flick.height)
            }
        }
    }

    // ---- clean loading state ----
    Column {
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.seriesTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading…"
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }
    }

    // ---- reader overlay: the SAME recreated reader, fed by the Comics store ----
    // Direct child (not a Loader+Component) for the same id-resolution reason as
    // MangaSeries' reader. `western: true` flips its page/download source to Comics.
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        western: true
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.seriesId
        seriesCover: page.poster
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.readerMinimizeRequested()
        onCloseRequested: page.readerCloseRequested()
    }
}
