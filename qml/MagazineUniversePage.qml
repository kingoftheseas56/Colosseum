// MagazineUniversePage — THE EDITORIAL ARCHIVE. Weekly Shonen Jump is not a list of famous
// manga and not a simulation of this week's issue: it is a sixty-year editorial institution,
// so the page is drawn as its archive room. The visitor enters past the masthead — a deep
// Jump-red wall where the FIRST ISSUE hangs framed and four monumental bound volumes stand
// on a shelf (the signature; each spine opens an era) — then works the CURRENT DESK (the
// live serialization registry), passes the HALL OF CHAMPIONS (most collected on MAL), and
// reads the four ARCHIVE VOLUMES themselves: open ivory paper spreads on the dark room, ink
// on paper — the one aesthetic risk, spent where a print institution earns it. The COMPLETE
// REGISTRY closes the room: every title MAL has ever filed under the magazine, searchable.
// (A5, 2026-07-18 free-reign commission; spec: docs/superpowers/specs/
// 2026-07-16-weekly-shonen-jump-editorial-archive-design.md.)
//
// Data honesty (spec §2): the canonical gate is MAL magazine id 83 via Jikan's exact
// registry — live data IS the canon. `members` is ALWAYS labeled a MAL member/library
// count, never print numbers; print history (105k launch, the 6.53M 1995 peak) is sourced
// from Wikipedia and kept visually separate as THE PRINT RECORD. Offline, the page stands
// whole on the curated fallback in Universes.js and invents nothing — no counts, no ranks.
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
    signal seriesRequested(string title)    // any filed entry → MangaSeries

    Theme { id: theme }

    // The archive's inks — aged Jump red, ink black, ivory stock, the editor's red pencil.
    readonly property color jumpRed:    "#8e1418"
    readonly property color jumpRedDeep:"#3d080b"
    readonly property color inkWall:    "#0e0b0c"
    readonly property color paper:      "#ece2cc"
    readonly property color paperEdge:  "#cfc1a2"
    readonly property color paperInk:   "#241a12"
    readonly property color paperFaint: "#6b5b45"
    readonly property color redPencil:  "#c0392b"

    property var uni: ({ name: "", blurb: "", banner: "", heroLine: "", milestones: [],
                         eraNotes: ({}), fallbackEras: ({}), readQueries: [] })
    readonly property int magId: uni.malMagazineId || 0

    // ── the FAST lane: hero total, Current Desk, Hall of Champions ──
    property var summary: null
    readonly property var champions: summary ? summary.all.slice(0, 10) : []
    readonly property var currentDesk: summary ? summary.publishing : []
    readonly property int registryTotal: summary ? summary.total : 0

    // ── the ARCHIVE lane: the page drives the walk, one registry page per tick ──
    //    (Jikan allows ~3/sec — the Timer is the throttle; cached pages skip the wait)
    property var archive: []            // every filed entry, id-deduped
    property int archTotal: 0           // MAL's own registry total (live only)
    property int archNext: 1
    property int archLast: 0
    property bool archWalking: false
    property bool archComplete: false
    property bool archFailed: false
    property bool archRetried: false    // one silent retry per page, then surface honestly

    // the four volumes — fixed order, always present; archive first, summary as the seed
    readonly property var eraSource: archive.length ? archive : (summary ? summary.all : [])
    readonly property var eras: Mag.bucketByEra(eraSource)
    readonly property var undated: Mag.undatedOf(archive)

    // hero: which spine the hand is on (−1 = none) — feeds the shelf's reveal strip
    property int hoveredSpine: -1

    // complete-registry index state
    property string ixQuery: ""
    property string ixEra: "all"
    readonly property var ixItems: {
        var out = Mag.alphaSort(root.archive)
        if (root.ixEra !== "all") {
            if (root.ixEra === "undated")
                out = out.filter(function(m) { return !m.fromYear })
            else {
                var def = null
                for (var i = 0; i < Mag.ERAS.length; i++)
                    if (Mag.ERAS[i].key === root.ixEra) def = Mag.ERAS[i]
                if (def) out = out.filter(function(m) {
                    return m.fromYear >= def.from && m.fromYear <= def.to })
            }
        }
        var q = root.ixQuery.trim().toLowerCase()
        if (q.length) out = out.filter(function(m) {
            return m.title.toLowerCase().indexOf(q) !== -1 })
        return out
    }

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        pump.stop(); pumpRetry.stop()              // a reload orphans any pending walk tick
        root.uni = UDB.configFor(root.universeName)
        root.summary = null
        root.archive = []
        root.archTotal = 0; root.archNext = 1; root.archLast = 0
        root.archWalking = false; root.archComplete = false
        root.archFailed = false; root.archRetried = false
        if (root.magId > 0)
            Mag.loadSummary(root.magId, function(s) {
                if (s) root.summary = s
                root.startArchive()                // walk regardless — resume covers a bad day
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
        // cached pages continue immediately; live pages wait out the throttle
        if (Mag.hasPage(root.magId, root.archNext)) root.fetchArchivePage()
        else pump.restart()
    }
    Timer { id: pump; interval: 460; repeat: false; onTriggered: root.fetchArchivePage() }
    Timer { id: pumpRetry; interval: 2600; repeat: false; onTriggered: root.fetchArchivePage() }
    function fetchArchivePage() {
        var wanted = root.archNext
        Mag.fetchArchivePage(root.magId, wanted, function(res) {
            if (wanted !== root.archNext || !root.archWalking) return   // a reload superseded us
            if (!res) {
                if (!root.archRetried) { root.archRetried = true; pumpRetry.restart(); return }
                root.archWalking = false
                root.archFailed = true             // honest stop — everything received stands
                return
            }
            root.archRetried = false
            root.archive = Mag.mergeDedup(root.archive, res.entries)
            if (res.total > 0) root.archTotal = res.total
            if (res.lastPage > 0) root.archLast = res.lastPage
            if (res.hasNext) { root.archNext += 1; root.pumpArchive() }
            else { root.archWalking = false; root.archComplete = true }
        })
    }

    function fallbackFor(key) {
        return (root.uni.fallbackEras && root.uni.fallbackEras[key]) || []
    }
    function eraNoteFor(key) {
        return (root.uni.eraNotes && root.uni.eraNotes[key]) || ""
    }
    function spanLine(e) {
        if (!e.fromYear) return "serialized in Jump"
        return e.publishing ? "since " + e.fromYear
                            : e.fromYear + "–" + (e.toYear || "")
    }
    function scrollToVolume(i) {
        var it = volRep.itemAt(i)
        if (!it) return
        scrollAnim.stop()
        scrollAnim.from = page.contentY
        scrollAnim.to = Math.max(0, Math.min(volumesCol.y + it.y - 14,
                                             page.contentHeight - page.height))
        scrollAnim.start()
    }
    NumberAnimation { id: scrollAnim; target: page; property: "contentY"
                      duration: 560; easing.type: Easing.InOutCubic }

    // ---- the archive room's wall: ink black with the faintest red memory ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.055, 0.043, 0.047, 0.94) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1c0709" }
                GradientStop { position: 0.34; color: root.inkWall }
                GradientStop { position: 1.0; color: "#0a0809" }
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

            // ═══════════ THE MASTHEAD — the archive entrance ═══════════
            Item {
                id: hero
                width: parent.width
                height: Math.max(600, root.height * 0.82)
                clip: true

                // the red wall, aged toward the floor
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.jumpRed }
                        GradientStop { position: 0.55; color: "#5a0d10" }
                        GradientStop { position: 1.0; color: root.jumpRedDeep }
                    }
                }
                // wall sheen — a diagonal light falling from the high left
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.05) }
                        GradientStop { position: 0.5; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.22) }
                    }
                }
                // the floor line the shelf stands on
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 84
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.5) }
                    }
                }

                // ── left: the masthead lockup ──
                Column {
                    id: heroCopy
                    anchors.left: parent.left
                    anchors.leftMargin: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -14
                    width: Math.min(620, parent.width * 0.46)
                    spacing: 15
                    Text {
                        text: "UNIVERSE  ·  THE MAGAZINE  ·  SHUEISHA, SINCE JULY 11, 1968"
                        color: theme.gold
                        font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 4; font.bold: true
                    }
                    Text {
                        text: root.uni.name || "Weekly Shonen Jump"
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 54
                        lineHeight: 0.95
                    }
                    // the sourced hero line — Wikipedia's own characterization, not our blurb
                    Text {
                        visible: (root.uni.heroLine || "").length > 0
                        text: "“" + root.uni.heroLine + "”"
                        color: Qt.rgba(0.97, 0.93, 0.86, 0.92)
                        font.family: theme.display; font.italic: true; font.pixelSize: 21
                    }
                    Text {
                        width: parent.width
                        text: root.uni.blurb || ""
                        color: Qt.rgba(0.97, 0.95, 0.93, 0.78)
                        font.family: theme.ui; font.pixelSize: 14
                        lineHeight: 1.45; wrapMode: Text.WordWrap
                        maximumLineCount: 3; elide: Text.ElideRight
                    }
                    Item { width: 1; height: 6 }
                    // THE PRINT RECORD — verified print history, kept apart from MAL numbers
                    Column {
                        spacing: 8
                        visible: (root.uni.milestones || []).length > 0
                        Text { text: "THE PRINT RECORD"
                               color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                               font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 3 }
                        Row {
                            spacing: 26
                            Repeater {
                                model: root.uni.milestones || []
                                delegate: Column {
                                    required property var modelData
                                    spacing: 2
                                    Text { text: modelData.year; color: theme.gold
                                           font.family: theme.display; font.italic: true
                                           font.pixelSize: 22 }
                                    Text { width: 150; text: modelData.fact
                                           color: Qt.rgba(0.97, 0.95, 0.93, 0.62)
                                           font.family: theme.ui; font.pixelSize: 11
                                           lineHeight: 1.25; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }
                    // the live registry line — appears only once MAL answers, never invented
                    Text {
                        visible: root.registryTotal > 0
                        text: root.registryTotal + " titles in MAL's serialization registry"
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.85)
                        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 1
                    }
                }

                // ── right: the first issue, framed — and the four bound volumes ──
                Row {
                    id: shelfRow
                    anchors.right: parent.right
                    anchors.rightMargin: theme.margin + 8
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 148
                    spacing: 30

                    // the framed archival object (never the page's throne)
                    Item {
                        width: 178; height: 268
                        anchors.bottom: parent.bottom
                        Rectangle {                       // the frame
                            anchors.fill: parent
                            color: "#221410"
                            border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.4)
                            Rectangle {                   // the ivory matte
                                anchors.fill: parent; anchors.margins: 9
                                color: "#e8dfc8"
                                Image {
                                    anchors.fill: parent; anchors.margins: 8
                                    source: root.uni.banner || ""
                                    asynchronous: true; cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: status === Image.Ready ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 300 } }
                                }
                            }
                        }
                        Rectangle {                       // the brass plate
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom; anchors.topMargin: 8
                            width: plateT.implicitWidth + 18; height: plateT.implicitHeight + 8
                            radius: 2; color: "#2a1c10"
                            border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
                            Text { id: plateT; anchors.centerIn: parent
                                   text: "ISSUE No. 1  ·  JULY 1968"
                                   color: theme.gold; font.family: theme.ui
                                   font.pixelSize: 9; font.letterSpacing: 2 }
                        }
                    }

                    // THE FOUR VOLUMES — the signature: bound spines, one era each
                    Row {
                        anchors.bottom: parent.bottom
                        spacing: 12
                        Repeater {
                            model: root.eras
                            delegate: ArchiveSpine {
                                required property var modelData
                                required property int index
                                bucket: modelData
                                ord: index
                            }
                        }
                    }
                }
                // the shelf they stand on
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 142
                    width: shelfRow.width + 26; height: 5
                    color: "#1c0f08"
                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.28)
                }
                // the reveal strip — the hovered spine speaks here
                Item {
                    anchors.right: parent.right; anchors.rightMargin: theme.margin
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 78
                    width: shelfRow.width + 26; height: 56
                    Column {
                        anchors.right: parent.right
                        spacing: 4
                        opacity: root.hoveredSpine >= 0 ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Text {
                            anchors.right: parent.right
                            text: root.hoveredSpine >= 0
                                  ? "VOLUME " + root.eras[root.hoveredSpine].volume + "  ·  "
                                    + root.eras[root.hoveredSpine].era.toUpperCase() + "  ·  "
                                    + root.eras[root.hoveredSpine].span
                                  : ""
                            color: theme.gold
                            font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2
                        }
                        Text {
                            anchors.right: parent.right
                            text: {
                                if (root.hoveredSpine < 0) return ""
                                var b = root.eras[root.hoveredSpine]
                                var names = b.items.length
                                    ? b.items.slice(0, 3).map(function(m) { return m.title })
                                    : root.fallbackFor(b.key).slice(0, 3).map(function(f) { return f.t })
                                return names.length ? names.join("  ·  ") : "the volume is still being filed"
                            }
                            color: Qt.rgba(0.97, 0.95, 0.93, 0.7)
                            font.family: theme.ui; font.pixelSize: 12
                        }
                        Text {
                            anchors.right: parent.right
                            text: root.hoveredSpine >= 0 ? "OPEN THE VOLUME  →" : ""
                            color: Qt.rgba(0.94, 0.77, 0.29, 0.7)
                            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2
                        }
                    }
                }
            }

            // ═══════════ THE CURRENT DESK — the live serialization registry ═══════════
            Item {
                width: parent.width; height: 118
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "THE CURRENT DESK"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "On the editor's desk"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 30 }
                    Text { text: root.currentDesk.length > 0
                                 ? root.currentDesk.length + " manga in MAL's current serialization registry — the running lineup, not the literal contents of this week's printed issue"
                                 : "Current registry unavailable — the desk stands empty rather than invented"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
            Flickable {
                width: parent.width; height: 314
                visible: root.currentDesk.length > 0
                contentWidth: deskRow.implicitWidth + theme.margin * 2
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: deskRow
                    x: theme.margin
                    spacing: 20
                    Repeater {
                        model: root.currentDesk
                        delegate: DeskSlip {
                            required property var modelData
                            required property int index
                            entry: modelData
                            ord: index
                        }
                    }
                }
            }

            // ═══════════ THE HALL OF CHAMPIONS — most collected on MAL ═══════════
            Column {
                x: theme.margin; width: parent.width - theme.margin * 2
                topPadding: 44
                spacing: 18
                Column {
                    spacing: 6
                    Text { text: "THE HALL OF CHAMPIONS"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: root.champions.length > 0 ? "Most collected on MAL" : "The flagships"
                           color: theme.ink
                           font.family: theme.display; font.pixelSize: 30 }
                    Text { text: root.champions.length > 0
                                 ? "cultural reach, not print sales — every number is a MyAnimeList library count"
                                 : "registry unreachable — the curated lineup stands in, unranked"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
                Column {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: root.champions.length > 0
                               ? root.champions
                               : (root.uni.readQueries || []).map(function(t) { return { title: t } })
                        delegate: Rectangle {
                            id: champ
                            required property var modelData
                            required property int index
                            readonly property bool live: root.champions.length > 0
                            width: parent.width; height: 92
                            radius: 12
                            color: champMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.045)
                            border.width: 1
                            border.color: champMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6)
                                                                : Qt.rgba(0.97, 0.97, 0.96, 0.08)
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 26
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 24
                                Text {
                                    width: 62
                                    visible: champ.live
                                    text: (champ.index + 1 < 10 ? "0" : "") + (champ.index + 1)
                                    color: champ.index === 0 ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.45)
                                    font.family: theme.display; font.italic: true; font.pixelSize: 42
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    visible: champ.live
                                    width: 50; height: 72; radius: 4; clip: true
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "#2a1a14"
                                    Image {
                                        anchors.fill: parent
                                        source: champ.modelData.cover || ""
                                        asynchronous: true; cache: true
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 220 } }
                                    }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5
                                    Text { text: champ.modelData.title
                                           color: theme.ink; font.family: theme.display; font.pixelSize: 20 }
                                    Text { visible: champ.live
                                           text: root.spanLine(champ.modelData)
                                                 + (champ.modelData.chapters ? "  ·  " + champ.modelData.chapters + " chapters" : "")
                                                 + (champ.modelData.author ? "  ·  " + champ.modelData.author : "")
                                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                                }
                            }
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 26
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 18
                                Text {
                                    visible: champ.live && champ.modelData.members > 0
                                    text: Mag.fmtMembers(champ.modelData.members) + " MAL members"
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Row {
                                    spacing: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    opacity: champMa.containsMouse ? 1 : 0.55
                                    Behavior on opacity { NumberAnimation { duration: 140 } }
                                    Text { text: "Read"; color: theme.ink
                                           font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                                    Text { text: "→"; color: theme.gold; font.pixelSize: 15 }
                                }
                            }
                            MouseArea {
                                id: champMa
                                anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.seriesRequested(champ.modelData.title)
                            }
                        }
                    }
                }
            }

            // ═══════════ THE FOUR ARCHIVE VOLUMES — the reading rooms ═══════════
            Item {
                width: parent.width; height: 128
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "THE ARCHIVE"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "Four volumes, one institution"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 30 }
                    Text {
                        text: {
                            var base = "every manga is filed under the era its Jump run began"
                            if (root.archWalking)
                                return base + "  —  filing the registry now: " + root.archive.length
                                       + (root.archTotal > 0 ? " of " + root.archTotal : "") + " titles"
                            if (root.archComplete) return base + "  —  " + root.archive.length + " titles filed"
                            return base
                        }
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    }
                }
            }
            Column {
                id: volumesCol
                width: parent.width
                spacing: 46
                Repeater {
                    id: volRep
                    model: root.eras
                    delegate: ArchiveVolume {
                        required property var modelData
                        required property int index
                        bucket: modelData
                        ord: index
                        width: contentColumn.width
                    }
                }
            }

            // ═══════════ THE COMPLETE REGISTRY — every title ever filed ═══════════
            Column {
                x: theme.margin; width: parent.width - theme.margin * 2
                topPadding: 54
                spacing: 18
                Column {
                    spacing: 6
                    Text { text: "THE COMPLETE REGISTRY"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "Every title ever filed"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 30 }
                    Row {
                        spacing: 14
                        Text {
                            text: {
                                if (root.archComplete)
                                    return "the complete MAL registry — " + root.archive.length + " titles, alphabetical"
                                if (root.archWalking)
                                    return "filing — " + root.archive.length
                                           + (root.archTotal > 0 ? " of " + root.archTotal : "") + " titles so far"
                                if (root.archFailed && root.archive.length > 0)
                                    return "archive partially filed — " + root.archive.length + " titles received"
                                if (root.archFailed) return "the registry is unreachable — nothing filed, nothing invented"
                                return ""
                            }
                            color: root.archFailed ? root.redPencil : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            visible: root.archFailed
                            radius: 6; height: 28; width: resumeT.implicitWidth + 26
                            color: resumeMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.2) : "transparent"
                            border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.6)
                            anchors.verticalCenter: parent.verticalCenter
                            Text { id: resumeT; anchors.centerIn: parent
                                   text: "RESUME FILING"; color: theme.gold
                                   font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2 }
                            MouseArea { id: resumeMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.startArchive() }
                        }
                    }
                }

                // the index tools — a search slip and the era tabs
                Row {
                    spacing: 12
                    visible: root.archive.length > 0
                    Rectangle {
                        width: 300; height: 38; radius: 10
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        border.color: ixInput.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.16)
                        TextInput {
                            id: ixInput
                            anchors.fill: parent
                            anchors.leftMargin: 14; anchors.rightMargin: 14
                            verticalAlignment: TextInput.AlignVCenter
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 13
                            selectionColor: Qt.rgba(0.94, 0.77, 0.29, 0.45)
                            clip: true
                            onTextChanged: root.ixQuery = text
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !ixInput.text.length && !ixInput.activeFocus
                            text: "Search the registry"
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: {
                                var pills = [ { key: "all", label: "ALL" } ]
                                for (var i = 0; i < root.eras.length; i++)
                                    pills.push({ key: root.eras[i].key, label: "VOL " + root.eras[i].volume })
                                if (root.undated.length > 0) pills.push({ key: "undated", label: "UNDATED" })
                                return pills
                            }
                            delegate: Rectangle {
                                id: pill
                                required property var modelData
                                readonly property bool on: root.ixEra === modelData.key
                                radius: 6; height: 28
                                width: pillT.implicitWidth + 22
                                color: pill.on ? Qt.rgba(0.94, 0.77, 0.29, 0.18)
                                               : (pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                                border.width: 1
                                border.color: pill.on ? theme.gold : Qt.rgba(1, 1, 1, 0.16)
                                Text { id: pillT; anchors.centerIn: parent
                                       text: pill.modelData.label
                                       color: pill.on ? theme.gold : theme.inkDim
                                       font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2 }
                                MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.ixEra = pill.modelData.key }
                            }
                        }
                    }
                }

                Text {
                    visible: root.archive.length > 0 && root.ixItems.length === 0
                    text: "nothing in the registry matches — clear the search or switch volumes"
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
                            border.color: chipMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.55)
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

    // ═══ ArchiveSpine — one bound volume standing on the masthead shelf ═══
    component ArchiveSpine: FocusScope {
        id: spine
        property var bucket: ({})
        property int ord: 0
        readonly property bool hot: spineMa.containsMouse || spine.activeFocus
        width: 66
        height: [252, 282, 264, 292][spine.ord % 4]
        anchors.bottom: parent.bottom
        activeFocusOnTab: true

        Rectangle {
            id: cloth
            anchors.fill: parent
            anchors.topMargin: spine.hot ? -10 : 0      // pulled up out of the shelf
            radius: 3
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: spine.hot ? "#6b1216" : "#520e12" }
                GradientStop { position: 0.14; color: spine.hot ? "#872024" : "#6b171b" }
                GradientStop { position: 0.86; color: spine.hot ? "#5c1013" : "#480c0f" }
                GradientStop { position: 1.0; color: "#2e0708" }
            }
            border.width: spine.activeFocus ? 2 : 1
            border.color: spine.activeFocus ? theme.gold : Qt.rgba(0, 0, 0, 0.5)
            Behavior on anchors.topMargin { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

            // gold stamped bands, top and foot
            Rectangle { x: 7; y: 14; width: parent.width - 14; height: 2
                        color: Qt.rgba(0.94, 0.77, 0.29, spine.hot ? 0.95 : 0.6) }
            Rectangle { x: 7; y: 20; width: parent.width - 14; height: 1
                        color: Qt.rgba(0.94, 0.77, 0.29, spine.hot ? 0.7 : 0.4) }
            Rectangle { x: 7; y: parent.height - 22; width: parent.width - 14; height: 2
                        color: Qt.rgba(0.94, 0.77, 0.29, spine.hot ? 0.95 : 0.6) }

            // the volume numeral, stamped across the head
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 30
                text: spine.bucket.volume || ""
                color: spine.hot ? theme.gold : Qt.rgba(0.94, 0.77, 0.29, 0.8)
                font.family: theme.display; font.pixelSize: 21
            }

            // the era name, running down the spine
            Text {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 16
                rotation: 90
                width: parent.height - 108
                text: (spine.bucket.era || "").toUpperCase()
                color: spine.hot ? Qt.rgba(0.97, 0.93, 0.86, 0.95) : Qt.rgba(0.97, 0.93, 0.86, 0.62)
                font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            // the filed count — a small archive label near the foot, live only
            Rectangle {
                visible: spine.bucket.items && spine.bucket.items.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - 46
                width: countT.implicitWidth + 10; height: countT.implicitHeight + 4
                radius: 2; color: "#e8dfc8"
                Text { id: countT; anchors.centerIn: parent
                       text: (spine.bucket.items ? spine.bucket.items.length : 0)
                       color: root.paperInk; font.family: theme.ui
                       font.pixelSize: 9; font.weight: Font.DemiBold }
            }
        }
        MouseArea {
            id: spineMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onEntered: root.hoveredSpine = spine.ord
            onExited: if (root.hoveredSpine === spine.ord) root.hoveredSpine = -1
            onClicked: root.scrollToVolume(spine.ord)
        }
        onActiveFocusChanged: if (activeFocus) root.hoveredSpine = spine.ord
        Keys.onReturnPressed: root.scrollToVolume(spine.ord)
        Keys.onEnterPressed: root.scrollToVolume(spine.ord)
        Keys.onSpacePressed: root.scrollToVolume(spine.ord)
    }

    // ═══ DeskSlip — one running manga as a manuscript slip on the editor's desk ═══
    component DeskSlip: FocusScope {
        id: slip
        property var entry: ({})
        property int ord: 0
        readonly property bool hot: slipMa.containsMouse || slip.activeFocus
        width: 168; height: 300
        activeFocusOnTab: true

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: slip.hot ? 0 : 8
            radius: 3
            rotation: slip.hot ? 0 : (slip.ord % 2 === 0 ? -1.1 : 1.2)
            color: "#f0e8d4"
            border.width: slip.activeFocus ? 2 : 1
            border.color: slip.activeFocus ? theme.gold : "#b8a988"
            Behavior on anchors.topMargin { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top; anchors.margins: 11
                spacing: 8
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 140; height: 198
                    color: "#ddd2b6"
                    border.width: 1; border.color: Qt.rgba(0.42, 0.32, 0.16, 0.5)
                    Image {
                        anchors.fill: parent; anchors.margins: 3
                        source: slip.entry.cover || ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                }
                Text {
                    width: parent.width
                    text: slip.entry.title || ""
                    color: root.paperInk
                    font.family: theme.display; font.pixelSize: 14
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                }
                Text {
                    text: (slip.entry.fromYear > 0 ? "since " + slip.entry.fromYear : "")
                          + (slip.entry.members > 0
                                ? "  ·  " + Mag.fmtMembers(slip.entry.members) + " MAL members" : "")
                    color: root.paperFaint
                    font.family: theme.ui; font.pixelSize: 10
                }
            }
            // the red RUNNING stamp — the editor's mark that the serialization lives
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: -6
                anchors.top: parent.top; anchors.topMargin: 10
                rotation: 8
                width: stampT.implicitWidth + 14; height: stampT.implicitHeight + 8
                color: "transparent"
                border.width: 2; border.color: Qt.rgba(0.75, 0.22, 0.17, 0.8)
                radius: 3
                Text { id: stampT; anchors.centerIn: parent
                       text: "RUNNING"
                       color: Qt.rgba(0.75, 0.22, 0.17, 0.9)
                       font.family: theme.ui; font.pixelSize: 9
                       font.letterSpacing: 2; font.bold: true }
            }
        }
        MouseArea {
            id: slipMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.seriesRequested(slip.entry.title)
        }
        Keys.onReturnPressed: root.seriesRequested(slip.entry.title)
        Keys.onEnterPressed: root.seriesRequested(slip.entry.title)
        Keys.onSpacePressed: root.seriesRequested(slip.entry.title)
    }

    // ═══ ArchiveVolume — one era as an open bound volume: ivory spread, ink on paper ═══
    component ArchiveVolume: Item {
        id: vol
        property var bucket: ({})
        property int ord: 0
        property string sortMode: "members"     // "members" | "year"
        property bool open: false               // the full-grid expander
        readonly property var fallback: root.fallbackFor(vol.bucket.key || "")
        readonly property bool live: (vol.bucket.items || []).length > 0
        readonly property var sorted: vol.live ? Mag.sortEra(vol.bucket.items, vol.sortMode) : []
        readonly property var shown: vol.open ? vol.sorted : vol.sorted.slice(0, 18)
        readonly property var anchorEntry: vol.live ? vol.bucket.items[0] : null
        implicitHeight: spread.height + 26
        height: implicitHeight                  // Column positions by height, not implicit

        // the closed stack beneath — page block edges
        Rectangle { x: spread.x + 7; y: spread.y + 8; width: spread.width; height: spread.height
                    radius: 3; color: root.paperEdge }
        Rectangle { x: spread.x + 3; y: spread.y + 4; width: spread.width; height: spread.height
                    radius: 3; color: "#ddd1b2" }

        Rectangle {
            id: spread
            x: theme.margin
            width: vol.width - theme.margin * 2
            height: Math.max(leftPage.implicitHeight, rightPage.implicitHeight) + 72
            radius: 3
            color: root.paper

            // the gutter — the fold between the two pages
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.horizontalCenterOffset: -spread.width * 0.16
                width: 52; height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Qt.rgba(0.25, 0.18, 0.1, 0.16) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            // ── LEFT PAGE — the era's card ──
            Column {
                id: leftPage
                x: 40; y: 36
                width: spread.width * 0.34 - 66
                spacing: 13
                Row {
                    spacing: 10
                    Text { text: "VOLUME " + (vol.bucket.volume || "")
                           color: root.redPencil
                           font.family: theme.ui; font.pixelSize: 11
                           font.letterSpacing: 4; font.bold: true }
                    Text { text: vol.bucket.span || ""
                           color: root.paperFaint
                           font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2
                           anchors.verticalCenter: parent.verticalCenter }
                }
                Text {
                    width: parent.width
                    text: vol.bucket.era || ""
                    color: root.paperInk
                    font.family: theme.display; font.pixelSize: 34
                    lineHeight: 1.0
                    wrapMode: Text.WordWrap
                }
                // the editor's red underline
                Rectangle { width: 64; height: 3; color: root.redPencil }
                Text {
                    visible: vol.live
                    text: vol.bucket.items.length + " titles filed"
                          + (root.archWalking ? "  ·  still filing" : "")
                    color: root.paperFaint
                    font.family: theme.ui; font.pixelSize: 12
                }
                Text {
                    width: parent.width
                    visible: root.eraNoteFor(vol.bucket.key || "").length > 0
                    text: root.eraNoteFor(vol.bucket.key || "")
                    color: Qt.rgba(0.25, 0.19, 0.12, 0.85)
                    font.family: theme.display; font.italic: true; font.pixelSize: 15
                    lineHeight: 1.35; wrapMode: Text.WordWrap
                }
                Item { width: 1; height: 4 }
                // the era's anchor — its most collected entry, live only
                Column {
                    visible: !!vol.anchorEntry
                    spacing: 8
                    Text { text: "THE ERA'S MOST COLLECTED"
                           color: root.paperFaint
                           font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 3 }
                    Rectangle {
                        width: 150; height: 214
                        color: "#ddd2b6"
                        border.width: anchorMa.containsMouse ? 2 : 1
                        border.color: anchorMa.containsMouse ? root.redPencil
                                                             : Qt.rgba(0.42, 0.32, 0.16, 0.5)
                        Image {
                            anchors.fill: parent; anchors.margins: 4
                            source: (vol.anchorEntry && vol.anchorEntry.cover) || ""
                            asynchronous: true; cache: true
                            fillMode: Image.PreserveAspectCrop
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                        }
                        MouseArea { id: anchorMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (vol.anchorEntry) root.seriesRequested(vol.anchorEntry.title) }
                    }
                    Text { width: leftPage.width
                           text: vol.anchorEntry
                                 ? vol.anchorEntry.title
                                   + (vol.anchorEntry.members > 0
                                        ? "  ·  " + Mag.fmtMembers(vol.anchorEntry.members) + " MAL members" : "")
                                 : ""
                           color: root.paperInk
                           font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                           wrapMode: Text.WordWrap }
                }
            }

            // ── RIGHT PAGE — the era's titles ──
            Column {
                id: rightPage
                x: spread.width * 0.34 + 24
                y: 36
                width: spread.width - x - 40
                spacing: 16

                // the page header: the sort switch (live) or the fallback note
                Row {
                    spacing: 10
                    visible: vol.live
                    Repeater {
                        model: [ { m: "members", label: "MOST COLLECTED" },
                                 { m: "year",    label: "CHRONOLOGICAL" } ]
                        delegate: Rectangle {
                            id: sortPill
                            required property var modelData
                            readonly property bool on: vol.sortMode === modelData.m
                            radius: 4; height: 26
                            width: sortT.implicitWidth + 22
                            color: sortPill.on ? root.paperInk : "transparent"
                            border.width: 1
                            border.color: sortPill.on ? root.paperInk : Qt.rgba(0.25, 0.19, 0.12, 0.4)
                            Text { id: sortT; anchors.centerIn: parent
                                   text: sortPill.modelData.label
                                   color: sortPill.on ? root.paper : root.paperFaint
                                   font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2 }
                            MouseArea { anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: vol.sortMode = sortPill.modelData.m }
                        }
                    }
                }
                Text {
                    visible: !vol.live
                    width: parent.width
                    text: "The archive can't reach MAL's registry right now — the era's curated flagships hold the page."
                    color: root.paperFaint
                    font.family: theme.ui; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                // live: the cover grid
                Flow {
                    width: parent.width
                    spacing: 16
                    visible: vol.live
                    Repeater {
                        model: vol.shown
                        delegate: PaperTile {
                            required property var modelData
                            entry: modelData
                        }
                    }
                }
                // the expander — open the full volume
                Rectangle {
                    visible: vol.live && vol.sorted.length > 18
                    width: parent.width; height: 40
                    radius: 4
                    color: expMa.containsMouse ? Qt.rgba(0.25, 0.19, 0.12, 0.1) : "transparent"
                    border.width: 1; border.color: Qt.rgba(0.25, 0.19, 0.12, 0.35)
                    Text {
                        anchors.centerIn: parent
                        text: vol.open ? "CLOSE THE VOLUME"
                                       : "OPEN THE FULL VOLUME  —  ALL " + vol.sorted.length + " TITLES"
                        color: root.paperInk
                        font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3
                    }
                    MouseArea { id: expMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: vol.open = !vol.open }
                }

                // fallback: the printed flagship slips
                Column {
                    width: parent.width
                    spacing: 0
                    visible: !vol.live
                    Repeater {
                        model: vol.fallback
                        delegate: Item {
                            id: fbRow
                            required property var modelData
                            width: parent.width; height: 46
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width
                                        height: 1; color: Qt.rgba(0.25, 0.19, 0.12, 0.25) }
                            // the red pencil tick
                            Text { text: "✓"; color: root.redPencil
                                   font.pixelSize: 13
                                   anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                x: 26
                                anchors.verticalCenter: parent.verticalCenter
                                text: fbRow.modelData.t
                                color: fbMa.containsMouse ? root.redPencil : root.paperInk
                                font.family: theme.display; font.pixelSize: 17
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: fbRow.modelData.a + "  ·  " + fbRow.modelData.y
                                color: root.paperFaint
                                font.family: theme.ui; font.pixelSize: 12
                            }
                            MouseArea { id: fbMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.seriesRequested(fbRow.modelData.t) }
                        }
                    }
                }
            }
        }
    }

    // ═══ PaperTile — one filed manga printed on the volume's right page ═══
    component PaperTile: FocusScope {
        id: pt
        property var entry: ({})
        readonly property bool hot: ptMa.containsMouse || pt.activeFocus
        width: 108; height: 202
        activeFocusOnTab: true
        Rectangle {
            width: parent.width; height: 152
            y: pt.hot ? -3 : 0
            color: "#ddd2b6"
            border.width: pt.hot ? 2 : 1
            border.color: pt.hot ? root.redPencil : Qt.rgba(0.42, 0.32, 0.16, 0.5)
            Behavior on y { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
            Image {
                anchors.fill: parent; anchors.margins: 3
                source: pt.entry.cover || ""
                asynchronous: true; cache: true
                fillMode: Image.PreserveAspectCrop
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }
        }
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 1
            Text {
                width: parent.width
                text: pt.entry.title || ""
                color: root.paperInk
                font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            Text {
                text: root.spanLine(pt.entry)
                color: root.redPencil
                font.family: theme.ui; font.pixelSize: 9
            }
        }
        MouseArea {
            id: ptMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.seriesRequested(pt.entry.title)
        }
        Keys.onReturnPressed: root.seriesRequested(pt.entry.title)
        Keys.onEnterPressed: root.seriesRequested(pt.entry.title)
        Keys.onSpacePressed: root.seriesRequested(pt.entry.title)
    }
}
