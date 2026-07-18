// MagazineUniversePage — THE LONG RUN. Weekly Shonen Jump is velocity: a magazine that has
// shipped every Monday since July 1968, printed loud on cheap paper. The page speaks that
// language — night ink, bright print-red, halftone dots, a speedline burst behind a tilted
// press-block lockup — and its signature is the run chart: the magazine's whole history,
// 1968 to today, with every landmark series drawn as an ink stroke across its real
// serialization years. Publishing runs fire off the right edge with an arrowhead. Hover a
// stroke to read its line; click to open the manga. Below: Running Now (the live
// publishing registry), Most Collected (MAL's member ranking), and Every Series (the full
// searchable registry, filed progressively).
// (A5, 2026-07-18 — fresh design on Hemanth's order; the prior archive concept is dead.)
//
// Data: MAL magazine id 83 via Jikan is the registry spine — the only database with a
// serialization axis. AniList carries the art: the curated flagships in Universes.js are
// AniList id-pinned with baked covers and AniList's own dates, so the chart and roster
// stand whole when Jikan is down. MAL members are always labeled MAL members. Nothing is
// invented offline: no ranks, no totals, no dates beyond the verified pins.
// Every entry routes into A1's MangaSeries by title (the manga lane's own door).
import QtQuick
import QtQuick.Controls
import "MagazineApi.js" as Mag
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors UniversePage — read-only verbs)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(string title)    // any entry → MangaSeries

    Theme { id: theme }

    // the press inks — night black, print red, newsprint gray
    readonly property color inkNight:  "#0b0a0c"
    readonly property color jumpRed:   "#d2232a"
    readonly property color newsprint: "#b9b3a6"
    readonly property color strokeInk: "#8a8478"

    property var uni: ({ name: "", blurb: "", banner: "", milestones: [], flagships: [],
                         currentLineup: [], readQueries: [] })
    readonly property int magId: uni.malMagazineId || 0
    readonly property int nowYear: new Date().getFullYear()
    readonly property var flagships: (uni.flagships || []).map(Mag.mapFlagship)

    // the current lineup is CURATED, not fetched — membership from Wikipedia's
    // current-series table, art from the verified AniList pins (Hemanth 2026-07-18)
    readonly property var runningNow: (uni.currentLineup || []).map(Mag.mapLineup)

    // ── FAST lane: hero total, chart strokes, Most Collected ──
    property var summary: null
    readonly property var champions: summary ? summary.all.slice(0, 10) : []
    readonly property int registryTotal: summary ? summary.total : 0

    // the chart rides the live registry when it answers, the verified flagships when not
    readonly property var chartRuns: Mag.buildRuns(summary ? summary.all : flagships, 28, nowYear)

    // ── ARCHIVE lane: the full registry, one page per tick, resumable ──
    property var archive: []
    property int archTotal: 0
    property int archNext: 1
    property bool archWalking: false
    property bool archComplete: false
    property bool archFailed: false
    property bool archRetried: false

    // registry wall state
    property string ixQuery: ""
    property string ixSort: "members"      // members | year | alpha
    property string ixDecade: "all"
    readonly property var ixItems: {
        var out = root.archive
        if (root.ixDecade !== "all")
            out = out.filter(function(m) {
                return root.ixDecade === "undated" ? !m.fromYear
                                                   : Mag.decadeOf(m.fromYear) === root.ixDecade })
        var q = root.ixQuery.trim().toLowerCase()
        if (q.length) out = out.filter(function(m) {
            return m.title.toLowerCase().indexOf(q) !== -1 })
        return Mag.sortBy(out, root.ixSort)
    }
    readonly property var ixDecades: {
        var seen = ({}), out = []
        for (var i = 0; i < root.archive.length; i++) {
            var d = Mag.decadeOf(root.archive[i].fromYear)
            if (d.length && !seen[d]) { seen[d] = true; out.push(d) }
        }
        out.sort()
        return out
    }

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        pump.stop(); pumpRetry.stop()
        root.uni = UDB.configFor(root.universeName)
        root.summary = null
        root.archive = []
        root.archTotal = 0; root.archNext = 1
        root.archWalking = false; root.archComplete = false
        root.archFailed = false; root.archRetried = false
        if (root.magId > 0)
            Mag.loadSummary(root.magId, function(s) {
                if (s) root.summary = s
                root.startArchive()
            })
    }
    Component.onCompleted: { reload(); reveal.start() }
    onUniverseNameChanged: reload()

    function startArchive() {
        if (root.magId <= 0 || root.archWalking || root.archComplete) return
        root.archFailed = false
        root.archRetried = false
        root.archWalking = true
        root.pumpArchive()
    }
    function pumpArchive() {
        if (Mag.hasPage(root.magId, root.archNext)) root.fetchArchivePage()
        else pump.restart()
    }
    Timer { id: pump; interval: 460; repeat: false; onTriggered: root.fetchArchivePage() }
    Timer { id: pumpRetry; interval: 2600; repeat: false; onTriggered: root.fetchArchivePage() }
    function fetchArchivePage() {
        var wanted = root.archNext
        Mag.fetchArchivePage(root.magId, wanted, function(res) {
            if (wanted !== root.archNext || !root.archWalking) return
            if (!res) {
                if (!root.archRetried) { root.archRetried = true; pumpRetry.restart(); return }
                root.archWalking = false
                root.archFailed = true
                return
            }
            root.archRetried = false
            root.archive = Mag.mergeDedup(root.archive, res.entries)
            if (res.total > 0) root.archTotal = res.total
            if (res.hasNext) { root.archNext += 1; root.pumpArchive() }
            else { root.archWalking = false; root.archComplete = true }
        })
    }

    function spanLine(e) {
        if (!e.fromYear) return ""
        return e.publishing ? e.fromYear + "–" : e.fromYear + "–" + (e.toYear || "")
    }
    function runLine(r) {
        var s = r.title + "  ·  " + r.fromYear + "–" + (r.publishing ? "" : (r.toYear || ""))
        if (r.author) s += "  ·  " + r.author
        if (r.members > 0) s += "  ·  " + Mag.fmtMembers(r.members) + " MAL members"
        return s
    }

    // ---- the wall: night ink, nothing else ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.043, 0.039, 0.047, 0.96) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#141013" }
                GradientStop { position: 0.4; color: root.inkNight }
                GradientStop { position: 1.0; color: "#080709" }
            }
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: contentColumn
            width: page.width
            opacity: 0
            spacing: 0

            // ═══════════ THE PRESS — the hero ═══════════
            Item {
                id: hero
                width: parent.width
                height: Math.max(500, root.height * 0.72)
                clip: true

                // the speedline burst — drawn rays converging behind the lockup
                Canvas {
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var cx = width * 0.60, cy = height * 0.40
                        var maxR = Math.max(width, height)
                        for (var i = 0; i < 96; i++) {
                            var a = (i / 96) * Math.PI * 2 + 0.13
                            var jitter = ((i * 37) % 11) / 11
                            var r0 = 150 + jitter * 90
                            var red = (i % 9 === 0)
                            ctx.strokeStyle = red ? Qt.rgba(0.82, 0.14, 0.16, 0.20)
                                                  : Qt.rgba(1, 1, 1, 0.035 + jitter * 0.05)
                            ctx.lineWidth = red ? 2 : 1
                            ctx.beginPath()
                            ctx.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0)
                            ctx.lineTo(cx + Math.cos(a) * maxR, cy + Math.sin(a) * maxR)
                            ctx.stroke()
                        }
                        // halftone field, upper right, thinning toward the text
                        ctx.fillStyle = Qt.rgba(1, 1, 1, 0.05)
                        for (var gx = Math.round(width * 0.55); gx < width; gx += 16)
                            for (var gy = 20; gy < height * 0.9; gy += 16) {
                                var t = (gx - width * 0.55) / (width * 0.45)
                                ctx.beginPath()
                                ctx.arc(gx + ((gy / 16) % 2) * 8, gy, 0.8 + t * 1.4, 0, Math.PI * 2)
                                ctx.fill()
                            }
                    }
                }

                // the lockup — a press block, set at a tilt
                Column {
                    x: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -8
                    spacing: 14
                    rotation: -2
                    Text {
                        text: "UNIVERSE  ·  SHUEISHA  ·  TOKYO  ·  EVERY MONDAY"
                        color: root.newsprint
                        font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 4; font.bold: true
                    }
                    Text {
                        text: "WEEKLY"
                        color: Qt.rgba(0.97, 0.97, 0.96, 0.55)
                        font.family: theme.ui; font.pixelSize: 17
                        font.letterSpacing: 22; font.bold: true
                    }
                    Row {
                        spacing: 20
                        Text {
                            text: "SHONEN"
                            color: theme.ink
                            font.family: theme.display; font.pixelSize: 84
                            lineHeight: 0.9
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: jumpT.implicitWidth + 40; height: jumpT.implicitHeight + 8
                            color: root.jumpRed
                            rotation: 1.6
                            Text { id: jumpT; anchors.centerIn: parent
                                   text: "JUMP"; color: theme.ink
                                   font.family: theme.display; font.pixelSize: 84 }
                        }
                    }
                    Text {
                        text: "友情 · 努力 · 勝利      friendship · effort · victory"
                        color: root.newsprint
                        font.family: theme.ui; font.pixelSize: 14; font.letterSpacing: 2
                    }
                    Item { width: 1; height: 2 }
                    Text {
                        width: Math.min(760, hero.width - theme.margin * 2)
                        text: {
                            var ms = root.uni.milestones || []
                            var parts = []
                            for (var i = 0; i < ms.length; i++) parts.push(ms[i].year + " " + ms[i].fact)
                            return parts.join("   ·   ")
                        }
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: root.registryTotal > 0
                        text: root.registryTotal + " series in MyAnimeList's Weekly Shonen Jump registry"
                        color: theme.gold
                        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
                    }
                }

                // the press rule — hard red edge at the fold
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3; color: root.jumpRed
                }
            }

            // ═══════════ THE LONG RUN — the serialization chart ═══════════
            Item {
                width: parent.width; height: 108
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "THE LONG RUN"; color: root.jumpRed
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 4; font.bold: true }
                    Text { text: "1968 → today"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 30 }
                    Text { text: "Landmark serializations, drawn to scale. An arrowhead means the run is still going — click any stroke to read."
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
            Item {
                id: chart
                x: theme.margin
                width: parent.width - theme.margin * 2
                height: root.chartRuns.lanes * 30 + 58
                property int hot: -1
                onHotChanged: chartCanvas.requestPaint()

                readonly property int y0: 1966
                function xFor(year) {
                    return (year - chart.y0) / (root.nowYear + 1 - chart.y0) * chart.width
                }

                Canvas {
                    id: chartCanvas
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Connections {
                        target: root
                        function onChartRunsChanged() { chartCanvas.requestPaint() }
                    }
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var runs = root.chartRuns.runs
                        var bottom = height - 30
                        // decade grid
                        ctx.textAlign = "left"
                        for (var y = 1970; y <= root.nowYear; y += 10) {
                            var gx = chart.xFor(y)
                            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.07)
                            ctx.lineWidth = 1
                            ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, bottom); ctx.stroke()
                            ctx.fillStyle = Qt.rgba(0.73, 0.70, 0.65, 0.55)
                            ctx.font = "10px 'Segoe UI'"
                            ctx.fillText(String(y), gx + 4, height - 14)
                        }
                        // the strokes
                        for (var i = 0; i < runs.length; i++) {
                            var r = runs[i]
                            var hot = (chart.hot === i)
                            var x1 = chart.xFor(r.fromYear)
                            var x2 = chart.xFor(r.publishing ? root.nowYear + 1 : (r.toYear || r.fromYear) + 1)
                            var w = Math.max(10, x2 - x1)
                            var ty = 8 + r.lane * 30
                            // label — elided to its run's reach so lanes stay readable
                            ctx.font = (hot ? "bold " : "") + "11px 'Segoe UI'"
                            ctx.fillStyle = hot ? Qt.rgba(0.97, 0.97, 0.96, 1)
                                                : Qt.rgba(0.97, 0.97, 0.96, 0.72)
                            var label = r.title
                            var maxW = Math.max(w + 46, 60)
                            while (label.length > 4 && ctx.measureText(label).width > maxW)
                                label = label.substring(0, label.length - 2)
                            if (label !== r.title) label += "…"
                            ctx.fillText(label, x1 + 1, ty + 8)
                            // stroke bar
                            ctx.fillStyle = hot ? root.jumpRed
                                                : (r.publishing ? Qt.rgba(0.85, 0.82, 0.76, 0.92)
                                                                : Qt.rgba(0.54, 0.52, 0.47, 0.9))
                            ctx.fillRect(x1, ty + 12, w - (r.publishing ? 8 : 0), hot ? 9 : 7)
                            if (r.publishing) {          // the arrowhead — still running
                                ctx.beginPath()
                                ctx.moveTo(x1 + w - 9, ty + 8)
                                ctx.lineTo(x1 + w + 2, ty + 15.5)
                                ctx.lineTo(x1 + w - 9, ty + 23)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }
                    }
                }
                // hit zones ride the same geometry as the drawn strokes
                Repeater {
                    model: root.chartRuns.runs
                    delegate: Item {
                        id: hit
                        required property var modelData
                        required property int index
                        x: chart.xFor(modelData.fromYear)
                        y: 8 + modelData.lane * 30
                        width: Math.max(26, chart.xFor(modelData.publishing ? root.nowYear + 1
                                                                            : (modelData.toYear || modelData.fromYear) + 1) - x)
                        height: 26
                        activeFocusOnTab: true
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onEntered: chart.hot = hit.index
                            onExited: if (chart.hot === hit.index) chart.hot = -1
                            onClicked: root.seriesRequested(hit.modelData.title)
                        }
                        onActiveFocusChanged: if (activeFocus) chart.hot = hit.index
                        Keys.onReturnPressed: root.seriesRequested(hit.modelData.title)
                        Keys.onEnterPressed: root.seriesRequested(hit.modelData.title)
                        Keys.onSpacePressed: root.seriesRequested(hit.modelData.title)
                    }
                }
            }
            Item {
                width: parent.width; height: 34
                Text {
                    x: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    text: chart.hot >= 0 ? root.runLine(root.chartRuns.runs[chart.hot]) : ""
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                }
            }

            // ═══════════ RUNNING NOW — the live publishing registry ═══════════
            Item {
                width: parent.width; height: 104
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "RUNNING NOW"; color: root.jumpRed
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 4; font.bold: true }
                    Text { text: "In serialization"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28 }
                    Text { text: root.runningNow.length + " series in the weekly lineup"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
            Flickable {
                width: parent.width; height: 296
                visible: root.runningNow.length > 0
                contentWidth: nowRow.implicitWidth + theme.margin * 2
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: nowRow
                    x: theme.margin
                    spacing: 18
                    Repeater {
                        model: root.runningNow
                        delegate: NowTile {
                            required property var modelData
                            entry: modelData
                        }
                    }
                }
            }

            // ═══════════ MOST COLLECTED — MAL's member ranking ═══════════
            Column {
                x: theme.margin; width: parent.width - theme.margin * 2
                topPadding: 40
                spacing: 18
                Column {
                    spacing: 6
                    Text { text: "MOST COLLECTED"; color: root.jumpRed
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.champions.length > 0 ? "The top ten, by MAL members" : "The flagships"
                           color: theme.ink
                           font.family: theme.display; font.pixelSize: 28 }
                    Text { text: root.champions.length > 0
                                 ? "MyAnimeList library counts — not sales"
                                 : "the curated lineup, 1976 to today"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
                Column {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: root.champions.length > 0 ? root.champions : root.flagships
                        delegate: Rectangle {
                            id: rankRow
                            required property var modelData
                            required property int index
                            readonly property bool live: root.champions.length > 0
                            width: parent.width; height: 88
                            radius: 12
                            color: rankMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.045)
                            border.width: 1
                            border.color: rankMa.containsMouse ? Qt.rgba(0.82, 0.14, 0.16, 0.7)
                                                               : Qt.rgba(0.97, 0.97, 0.96, 0.08)
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 22
                                Text {
                                    visible: rankRow.live
                                    width: 58
                                    text: (rankRow.index + 1 < 10 ? "0" : "") + (rankRow.index + 1)
                                    color: rankRow.index === 0 ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.45)
                                    font.family: theme.display; font.italic: true; font.pixelSize: 40
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    width: 48; height: 68; radius: 4; clip: true
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "#1c191b"
                                    Image {
                                        anchors.fill: parent
                                        source: rankRow.modelData.cover || ""
                                        asynchronous: true; cache: true
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 220 } }
                                    }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5
                                    Text { text: rankRow.modelData.title
                                           color: theme.ink; font.family: theme.display; font.pixelSize: 19 }
                                    Text { text: {
                                               var bits = []
                                               var span = root.spanLine(rankRow.modelData)
                                               if (span.length) bits.push(span)
                                               if (rankRow.modelData.chapters) bits.push(rankRow.modelData.chapters + " chapters")
                                               if (rankRow.modelData.author) bits.push(rankRow.modelData.author)
                                               return bits.join("  ·  ")
                                           }
                                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                                }
                            }
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 18
                                Text {
                                    visible: rankRow.live && rankRow.modelData.members > 0
                                    text: Mag.fmtMembers(rankRow.modelData.members) + " MAL members"
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Row {
                                    spacing: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    opacity: rankMa.containsMouse ? 1 : 0.55
                                    Behavior on opacity { NumberAnimation { duration: 140 } }
                                    Text { text: "Read"; color: theme.ink
                                           font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                                    Text { text: "→"; color: root.jumpRed; font.pixelSize: 15 }
                                }
                            }
                            MouseArea {
                                id: rankMa
                                anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.seriesRequested(rankRow.modelData.title)
                            }
                        }
                    }
                }
            }

            // ═══════════ EVERY SERIES — the full registry ═══════════
            Column {
                x: theme.margin; width: parent.width - theme.margin * 2
                topPadding: 48
                spacing: 16
                Column {
                    spacing: 6
                    Text { text: "EVERY SERIES"; color: root.jumpRed
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 4; font.bold: true }
                    Text { text: "The full registry"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28 }
                    Row {
                        spacing: 14
                        Text {
                            text: {
                                if (root.archComplete) return root.archive.length + " series, 1968 to today"
                                if (root.archWalking)
                                    return "loading — " + root.archive.length
                                           + (root.archTotal > 0 ? " of " + root.archTotal : "") + " so far"
                                if (root.archFailed && root.archive.length > 0)
                                    return root.archive.length + " received — the rest returns with MyAnimeList"
                                if (root.archFailed) return "MyAnimeList's registry isn't responding right now"
                                return ""
                            }
                            color: root.archFailed ? root.newsprint : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            visible: root.archFailed
                            radius: 6; height: 28; width: resumeT.implicitWidth + 26
                            color: resumeMa.containsMouse ? Qt.rgba(0.82, 0.14, 0.16, 0.25) : "transparent"
                            border.width: 1; border.color: Qt.rgba(0.82, 0.14, 0.16, 0.7)
                            anchors.verticalCenter: parent.verticalCenter
                            Text { id: resumeT; anchors.centerIn: parent
                                   text: "RETRY"; color: theme.ink
                                   font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2 }
                            MouseArea { id: resumeMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.startArchive() }
                        }
                    }
                }

                Row {
                    spacing: 12
                    visible: root.archive.length > 0
                    Rectangle {
                        width: 300; height: 38; radius: 10
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        border.color: ixInput.activeFocus ? root.jumpRed : Qt.rgba(1, 1, 1, 0.16)
                        TextInput {
                            id: ixInput
                            anchors.fill: parent
                            anchors.leftMargin: 14; anchors.rightMargin: 14
                            verticalAlignment: TextInput.AlignVCenter
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 13
                            selectionColor: Qt.rgba(0.82, 0.14, 0.16, 0.5)
                            clip: true
                            onTextChanged: root.ixQuery = text
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !ixInput.text.length && !ixInput.activeFocus
                            text: "Search"
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: [ { m: "members", label: "MOST COLLECTED" },
                                     { m: "year",    label: "NEWEST" },
                                     { m: "alpha",   label: "A–Z" } ]
                            delegate: FilterPill {
                                required property var modelData
                                label: modelData.label
                                on: root.ixSort === modelData.m
                                onPicked: root.ixSort = modelData.m
                            }
                        }
                    }
                    Rectangle { width: 1; height: 26; color: Qt.rgba(1, 1, 1, 0.14)
                                anchors.verticalCenter: parent.verticalCenter }
                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: {
                                var pills = [ { key: "all", label: "ALL" } ]
                                for (var i = 0; i < root.ixDecades.length; i++)
                                    pills.push({ key: root.ixDecades[i], label: root.ixDecades[i].toUpperCase() })
                                return pills
                            }
                            delegate: FilterPill {
                                required property var modelData
                                label: modelData.label
                                on: root.ixDecade === modelData.key
                                onPicked: root.ixDecade = modelData.key
                            }
                        }
                    }
                }

                Text {
                    visible: root.archive.length > 0 && root.ixItems.length === 0
                    text: "No matches"
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    visible: root.ixItems.length > 0
                    Repeater {
                        model: root.ixItems
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            radius: 6; height: 30
                            width: chipRow.implicitWidth + 22
                            color: chipMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.04)
                            border.width: 1
                            border.color: chipMa.containsMouse ? Qt.rgba(0.82, 0.14, 0.16, 0.65)
                                                               : Qt.rgba(1, 1, 1, 0.10)
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 7
                                Text { text: chip.modelData.title
                                       color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                                Text { visible: chip.modelData.fromYear > 0
                                       text: String(chip.modelData.fromYear)
                                       color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
                                       anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.seriesRequested(chip.modelData.title) }
                        }
                    }
                }
            }

            Item { width: 1; height: 76 }
        }

        NumberAnimation {
            id: reveal
            target: contentColumn
            property: "opacity"
            from: 0; to: 1
            duration: 460
            easing.type: Easing.OutCubic
        }
    }

    ChromeScrim { z: 16 }
    BackAction { x: theme.margin; y: 28; z: 20; onTriggered: root.backRequested() }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: powerMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: powerMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
        }
    }

    // ═══ NowTile — one running series ═══
    component NowTile: FocusScope {
        id: nt
        property var entry: ({})
        readonly property bool hot: ntMa.containsMouse || nt.activeFocus
        width: 150; height: 288
        activeFocusOnTab: true
        Rectangle {
            width: parent.width; height: 212
            y: nt.hot ? -4 : 0
            radius: 8; clip: true
            color: "#1c191b"
            border.width: 1
            border.color: nt.hot ? Qt.rgba(0.82, 0.14, 0.16, 0.85) : Qt.rgba(0.97, 0.97, 0.96, 0.12)
            Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Behavior on border.color { ColorAnimation { duration: 140 } }
            Image {
                anchors.fill: parent
                source: nt.entry.cover || ""
                asynchronous: true; cache: true
                fillMode: Image.PreserveAspectCrop
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }
        }
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 2
            Text {
                width: parent.width
                text: nt.entry.title || ""
                color: theme.ink; font.family: theme.ui
                font.pixelSize: 13; font.weight: Font.DemiBold
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: (nt.entry.since ? "since " + nt.entry.since
                                      : (nt.entry.fromYear > 0 ? "since " + nt.entry.fromYear : ""))
                      + (nt.entry.author ? "  ·  " + nt.entry.author : "")
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
            }
        }
        MouseArea {
            id: ntMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.seriesRequested(nt.entry.title)
        }
        Keys.onReturnPressed: root.seriesRequested(nt.entry.title)
        Keys.onEnterPressed: root.seriesRequested(nt.entry.title)
        Keys.onSpacePressed: root.seriesRequested(nt.entry.title)
    }

    // ═══ FilterPill — one registry-wall filter ═══
    component FilterPill: Rectangle {
        id: fp
        property string label: ""
        property bool on: false
        signal picked()
        radius: 6; height: 28
        width: fpT.implicitWidth + 22
        color: fp.on ? Qt.rgba(0.82, 0.14, 0.16, 0.22)
                     : (fpMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
        border.width: 1
        border.color: fp.on ? root.jumpRed : Qt.rgba(1, 1, 1, 0.16)
        Text { id: fpT; anchors.centerIn: parent
               text: fp.label
               color: fp.on ? theme.ink : theme.inkDim
               font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2 }
        MouseArea { id: fpMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fp.picked() }
    }
}
