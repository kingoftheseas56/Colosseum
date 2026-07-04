// ComicSeries — the western-comics series page (Tankoban mode). A GetComics tag IS
// the series (ratified 2026-07-04: GetComics for both catalog and download, iTunes
// posters on top, no metadata brain in v1). The shelf is the tag's release posts,
// newest-first, lightly grouped: collections (TPB/omnibus/treasury) lead, single
// issues follow. Each release = ONE volume unit (TB2-ratified: TPB is king) — a
// single archive download, extracted by `Comics` into a page dir MangaReader eats.
// Covers: each release's own og_image (exact by construction); iTunes art is the
// series-level hero only. Same glass-over-wallpaper language as MangaSeries.

import QtQuick
import "ComicsApi.js" as Api

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
    property bool loading: true
    property string errorMsg: ""
    readonly property string seriesId: "gc:" + tagSlug   // the app-wide western series id
    readonly property var collections: releases.filter(function(r) { return r.collection })
    readonly property var issues: releases.filter(function(r) { return !r.collection })
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
        if (!tagSlug.length) { errorMsg = "No series tag."; loading = false; return }
        Api.tagBySlug(tagSlug, function(t) {
            if (!t) { page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on GetComics."; page.loading = false; return }
            page.tagId = t.tagId
            if (!page.seriesTitle.length) page.seriesTitle = t.title
            loadReleases()
        })
    }
    function loadReleases() {
        Api.releases(tagId, function(rs) {
            page.releases = rs || []
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

    Rectangle { anchors.fill: parent; color: "#07080c" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.42) }
            GradientStop { position: 0.42; color: Qt.rgba(0.03, 0.035, 0.05, 0.72) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.9) }
        }
    }

    ChromeScrim { z: 16 }

    // ---- ‹ Back ----
    Item {
        id: backBtn
        x: theme.margin; y: 28; width: backRow.implicitWidth + 16; height: 34; z: 20
        Row {
            id: backRow; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } } }
        }
        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: page.backRequested() }
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
                            Text { text: "releases"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
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

            // ── RELEASE TABLE — collections lead, issues follow, one glass widget ──
            Item {
                width: parent.width
                height: relTable.height + 24
                visible: page.releases.length > 0

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
                                            function onFailed(cid, reason) { if (cid === row.relId) row.dlState = "error" }
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
                                                text: row.dlState === "done" ? (trMa.containsMouse ? "✕" : "✓")
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
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: row.primary() }
                                    }
                                }
                            }
                        }
                    }
                }
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
