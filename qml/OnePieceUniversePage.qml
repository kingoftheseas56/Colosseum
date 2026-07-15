// OnePieceUniversePage — THE CAPTAIN'S CHART. One Piece is ONE unbroken voyage, so the
// page is drawn as a sea chart: a dotted treasure-map course undulating across the Grand
// Line, saga islands staggered above and below the route, and a red X at the end — because
// X marks the One Piece. The manga shelf is a row of WANTED posters (parchment, letterhead,
// DEAD OR ALIVE) — the one aesthetic risk, spent where the subject earns it. (Agent 5,
// 2026-07-15 free-reign commission, rebuilt to the Cosmere authored-artifact bar same day.)
//
// Data = the pinned curation in Universes.js (anime / sagas / adaptations / filmEras /
// manga) — untouched by this rebuild. The one anime + every film dress DIRECTLY by
// verified Cinemeta id via live.metahub.space (IPv4-pinned); manga carry AniList covers
// (s4.anilist.co, IPv4-pinned). Anime/films → A4's TheatreSeries (watchRequested); manga →
// the manga reader (seriesRequested). Every saga waypoint opens the one anime.
import QtQuick
import QtQuick.Controls
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    // shell contract
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)         // anime / film → A4's TheatreSeries.qml
    signal seriesRequested(string title)    // manga → the manga reader

    Theme { id: theme }

    // The chart's inks — deep-sea night, chart gold, sunset buoys, treasure red, parchment.
    readonly property color seaDeep:   "#04121c"
    readonly property color chartInk:  "#1b4258"   // wave ripples / faint chart linework
    readonly property color seaGold:   theme.gold
    readonly property color sunset:    "#f2a04a"
    readonly property color treasureX: "#d84a33"
    readonly property color parchment: "#e6d2a4"
    readonly property color parchInk:  "#3a2a16"

    property var uni: ({ name: "", blurb: "", banner: "", anime: null, firstRead: null,
                         sagas: [], adaptations: [], filmEras: [], manga: [] })
    function reload() {
        if (!root.universeName.length) return
        var arr = UDB.universes
        for (var i = 0; i < arr.length; i++)
            if (arr[i].name === root.universeName) { root.uni = arr[i]; return }
    }
    Component.onCompleted: { reload(); reveal.start() }
    onUniverseNameChanged: reload()

    function poster(id) { return id ? "https://live.metahub.space/poster/medium/" + id + "/img" : "" }

    // chart geometry — one slot per saga; nodes ride the course, staggered high/low
    readonly property int slotW: 252
    readonly property int courseY: 212
    readonly property int swing: 34
    function nodeX(i) { return 150 + i * slotW }
    function nodeY(i) { return courseY + (i % 2 === 0 ? -swing : swing) }

    // ---- the wall: deep sea, the app wallpaper only a faint memory beneath ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.016, 0.055, 0.09, 0.94) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#07202f" }
                GradientStop { position: 0.5; color: root.seaDeep }
                GradientStop { position: 1.0; color: "#020a10" }
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

            // ═══════════ THE OPEN SEA — the invitation ═══════════
            Item {
                id: hero
                width: parent.width
                height: Math.max(620, root.height * 0.86)
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.uni.banner || ""
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 0.16 : 0
                    Behavior on opacity { NumberAnimation { duration: 500 } }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.016,0.055,0.09,0.42) }
                        GradientStop { position: 0.6; color: Qt.rgba(0.016,0.055,0.09,0.78) }
                        GradientStop { position: 1.0; color: root.seaDeep }
                    }
                }

                // the log pose — a drawn compass, the hero's quiet right-side instrument
                Canvas {
                    id: compass
                    width: 300; height: 300
                    anchors.right: parent.right
                    anchors.rightMargin: theme.margin + 30
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.55
                    onWidthChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var cx = width / 2, cy = height / 2
                        ctx.strokeStyle = "#2a5a76"; ctx.lineWidth = 1
                        ctx.beginPath(); ctx.arc(cx, cy, 128, 0, Math.PI * 2); ctx.stroke()
                        ctx.beginPath(); ctx.arc(cx, cy, 104, 0, Math.PI * 2); ctx.stroke()
                        ctx.strokeStyle = "#3f7a99"
                        for (var k = 0; k < 16; k++) {          // degree ticks
                            var a = k * Math.PI / 8
                            var r1 = k % 4 === 0 ? 88 : 97
                            ctx.beginPath()
                            ctx.moveTo(cx + Math.cos(a) * r1,  cy + Math.sin(a) * r1)
                            ctx.lineTo(cx + Math.cos(a) * 104, cy + Math.sin(a) * 104)
                            ctx.stroke()
                        }
                        // the needle — pointing up-sea, gold north / dim south
                        ctx.fillStyle = "#f0c44a"
                        ctx.beginPath()
                        ctx.moveTo(cx, cy - 78); ctx.lineTo(cx - 11, cy); ctx.lineTo(cx + 11, cy)
                        ctx.closePath(); ctx.fill()
                        ctx.fillStyle = "#33607a"
                        ctx.beginPath()
                        ctx.moveTo(cx, cy + 78); ctx.lineTo(cx - 11, cy); ctx.lineTo(cx + 11, cy)
                        ctx.closePath(); ctx.fill()
                        ctx.fillStyle = "#dfeaf0"
                        ctx.beginPath(); ctx.arc(cx, cy, 5, 0, Math.PI * 2); ctx.fill()
                    }
                }
                Text {
                    anchors.horizontalCenter: compass.horizontalCenter
                    anchors.top: compass.bottom
                    anchors.topMargin: 2
                    text: "THE LOG POSE HOLDS THE COURSE"
                    color: Qt.rgba(0.47, 0.72, 0.85, 0.55)
                    font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 3
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: theme.margin
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(640, parent.width * 0.52)
                    spacing: 15
                    Text {
                        text: "ONE PIECE  /  THE GREAT PIRATE ERA"
                        color: root.sunset
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 3
                    }
                    Text {
                        text: "One crew, one promise,\nand the whole sea between."
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 52
                        lineHeight: 0.95
                    }
                    Text {
                        text: root.uni.blurb || ""
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 15
                        lineHeight: 1.4
                        width: parent.width - 30
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "1,100+ episodes  ·  seventeen films  ·  the manga since 1997  —  Eiichiro Oda"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                    Item { width: 1; height: 8 }
                    Row {
                        spacing: 14
                        // Set sail → the one anime
                        Rectangle {
                            radius: 12; height: 52; width: sailRow.implicitWidth + 48
                            visible: !!root.uni.anime
                            gradient: Gradient {
                                GradientStop { position: 0; color: sailMa.containsMouse ? Qt.rgba(0.95,0.63,0.29,0.42) : Qt.rgba(0.95,0.63,0.29,0.22) }
                                GradientStop { position: 1; color: sailMa.containsMouse ? Qt.rgba(0.82,0.45,0.14,0.30) : Qt.rgba(0.82,0.45,0.14,0.13) }
                            }
                            border.width: 1
                            border.color: sailMa.containsMouse ? root.seaGold : Qt.rgba(0.95,0.63,0.29,0.55)
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Row {
                                id: sailRow; anchors.centerIn: parent; spacing: 10
                                Text { text: "Set sail"; color: theme.ink
                                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "→"; color: root.seaGold; font.pixelSize: 17
                                    anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: sailMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.uni.anime) root.watchRequested(root.uni.anime) }
                        }
                        // Read from Chapter 1 → the manga
                        Rectangle {
                            radius: 12; height: 52; width: readRow.implicitWidth + 48
                            visible: !!root.uni.firstRead
                            color: "transparent"
                            border.width: 1
                            border.color: readMa.containsMouse ? theme.ink : Qt.rgba(1,1,1,0.26)
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Row {
                                id: readRow; anchors.centerIn: parent; spacing: 10
                                Text { text: "Read from Chapter 1"; color: readMa.containsMouse ? theme.ink : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: readMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.uni.firstRead) root.seriesRequested(root.uni.firstRead.t) }
                        }
                    }
                }
            }

            // ═══════════ THE CHART — the Grand Line, saga by saga ═══════════
            Item {
                width: parent.width; height: 108
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "THE CAPTAIN'S CHART"; color: root.seaGold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "The whole voyage, island by island"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 31 }
                    Text { text: "Eleven sagas, one course — every landing opens the anime. The X is where the treasure waits."
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }

            Flickable {
                id: chartFlick
                width: parent.width
                height: 470
                contentWidth: chart.width
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                Item {
                    id: chart
                    width: root.nodeX(Math.max(0, (root.uni.sagas || []).length - 1)) + 320
                    height: 470

                    // the sea + the course, drawn: wave ripples, then the dotted route
                    Canvas {
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var n = (root.uni.sagas || []).length
                            if (n < 2) return
                            // ---- faint wave ripples (three drifting sine lines) ----
                            ctx.lineWidth = 1
                            var waves = [ { y: 92,  amp: 5, ph: 0.0,  op: 0.22 },
                                          { y: 236, amp: 7, ph: 1.7,  op: 0.30 },
                                          { y: 388, amp: 5, ph: 3.4,  op: 0.22 } ]
                            for (var w = 0; w < waves.length; w++) {
                                ctx.strokeStyle = Qt.rgba(0.19, 0.43, 0.56, waves[w].op)
                                ctx.beginPath()
                                for (var x = 0; x <= width; x += 12) {
                                    var wy = waves[w].y + Math.sin(x / 68 + waves[w].ph) * waves[w].amp
                                    if (x === 0) ctx.moveTo(x, wy); else ctx.lineTo(x, wy)
                                }
                                ctx.stroke()
                            }
                            // ---- the dotted course: quadratics through the staggered nodes,
                            //      sampled into treasure-map dots (no setLineDash dependency) ----
                            ctx.fillStyle = Qt.rgba(0.94, 0.77, 0.29, 0.75)
                            function dotSeg(x0, y0, cx, cy, x1, y1) {
                                var len = Math.abs(x1 - x0) + Math.abs(y1 - y0)
                                var steps = Math.max(6, Math.round(len / 16))
                                for (var s = 0; s <= steps; s++) {
                                    var t = s / steps
                                    var mt = 1 - t
                                    var px = mt*mt*x0 + 2*mt*t*cx + t*t*x1
                                    var py = mt*mt*y0 + 2*mt*t*cy + t*t*y1
                                    ctx.beginPath(); ctx.arc(px, py, 2, 0, Math.PI * 2); ctx.fill()
                                }
                            }
                            // course begins off-chart (the ship comes from East Blue's edge)
                            var p0x = root.nodeX(0), p0y = root.nodeY(0)
                            dotSeg(28, root.courseY, (28 + p0x) / 2, p0y, p0x, p0y)
                            for (var i = 1; i < n; i++) {
                                var ax = root.nodeX(i - 1), ay = root.nodeY(i - 1)
                                var bx = root.nodeX(i),     by = root.nodeY(i)
                                dotSeg(ax, ay, (ax + bx) / 2, ay, (ax + bx) / 2, (ay + by) / 2)
                                dotSeg((ax + bx) / 2, (ay + by) / 2, (ax + bx) / 2, by, bx, by)
                            }
                        }
                    }

                    Repeater {
                        model: root.uni.sagas
                        delegate: SagaIsland {
                            required property var modelData
                            required property int index
                            saga: modelData
                            ord: index
                            x: root.nodeX(index) - width / 2
                        }
                    }
                }
            }

            // ═══════════ ANOTHER SEA, SAME DREAM — the adaptations ═══════════
            Item {
                width: parent.width; height: 96
                visible: (root.uni.adaptations || []).length > 0
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "ANOTHER SEA, SAME DREAM"; color: root.sunset
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "The voyage retold"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 27 }
                }
            }
            Row {
                x: theme.margin
                spacing: 18
                visible: (root.uni.adaptations || []).length > 0
                Repeater {
                    model: root.uni.adaptations
                    delegate: Item {
                        id: adTile
                        required property var modelData
                        width: 348; height: 150
                        Rectangle {
                            anchors.fill: parent
                            radius: 12; clip: true
                            color: "#071824"
                            border.width: 1
                            border.color: adMa.containsMouse ? root.seaGold : Qt.rgba(0.55,0.75,0.88,0.16)
                            Behavior on border.color { ColorAnimation { duration: 140 } }
                            Image {
                                anchors.fill: parent
                                source: root.poster(adTile.modelData.id)
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectCrop
                                opacity: status === Image.Ready ? (adMa.containsMouse ? 0.55 : 0.34) : 0
                                Behavior on opacity { NumberAnimation { duration: 220 } }
                            }
                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Qt.rgba(0.016,0.055,0.09,0.92) }
                                    GradientStop { position: 1; color: Qt.rgba(0.016,0.055,0.09,0.30) }
                                }
                            }
                            Column {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 20
                                spacing: 6
                                Row {
                                    spacing: 9
                                    Text { text: adTile.modelData.t; color: theme.ink
                                           font.family: theme.display; font.pixelSize: 20 }
                                    Rectangle {
                                        visible: adTile.modelData.upcoming === true
                                        anchors.verticalCenter: parent.verticalCenter
                                        radius: 4; color: Qt.rgba(0,0,0,0.5)
                                        border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.5)
                                        width: upTag.implicitWidth + 12; height: upTag.implicitHeight + 6
                                        Text { id: upTag; anchors.centerIn: parent; text: "UPCOMING"
                                               color: root.seaGold; font.family: theme.ui
                                               font.pixelSize: 9; font.letterSpacing: 2 }
                                    }
                                }
                                Text { text: adTile.modelData.year + "  ·  " + adTile.modelData.note
                                       color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                            }
                            MouseArea {
                                id: adMa; anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.watchRequested(adTile.modelData)
                            }
                        }
                    }
                }
            }

            // ═══════════ THE SHIP'S LOG — every film, in sailing order ═══════════
            Repeater {
                model: root.uni.filmEras
                delegate: Column {
                    id: filmEra
                    required property var modelData
                    required property int index
                    width: contentColumn.width
                    spacing: 0
                    Item {
                        width: parent.width; height: index === 0 ? 118 : 92
                        Column {
                            x: theme.margin; anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                            spacing: 6
                            Text { visible: filmEra.index === 0
                                   text: "THE SHIP'S LOG"; color: root.seaGold
                                   font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                            Row {
                                spacing: 12
                                Text { text: filmEra.modelData.era; color: theme.ink
                                       font.family: theme.display; font.pixelSize: 26 }
                                Text { text: filmEra.modelData.films.length + " films  ·  sailing order"
                                       color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                       anchors.baseline: parent.children[0].baseline }
                            }
                        }
                    }
                    Flickable {
                        width: parent.width; height: 246
                        contentWidth: filmRow.implicitWidth + theme.margin * 2
                        contentHeight: height
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        Row {
                            id: filmRow
                            x: theme.margin
                            spacing: 16
                            Repeater {
                                model: filmEra.modelData.films
                                delegate: Item {
                                    id: fTile
                                    required property var modelData
                                    width: 150; height: 232
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8; clip: true
                                        color: "#071520"
                                        border.width: 1
                                        border.color: fMa.containsMouse ? root.seaGold : Qt.rgba(1,1,1,0.12)
                                        Behavior on border.color { ColorAnimation { duration: 140 } }
                                        Image {
                                            anchors.fill: parent
                                            source: root.poster(fTile.modelData.id)
                                            asynchronous: true; cache: true
                                            fillMode: Image.PreserveAspectCrop
                                            opacity: status === Image.Ready ? 1 : 0
                                            Behavior on opacity { NumberAnimation { duration: 220 } }
                                        }
                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 60
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: "transparent" }
                                                GradientStop { position: 1; color: Qt.rgba(0,0,0,0.9) }
                                            }
                                        }
                                        Column {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 9
                                            spacing: 1
                                            Text {
                                                width: parent.width
                                                text: fTile.modelData.t
                                                color: theme.ink; font.family: theme.ui
                                                font.pixelSize: 12; font.weight: Font.DemiBold
                                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                            }
                                            Text {
                                                text: fTile.modelData.year
                                                color: root.sunset; font.family: theme.ui; font.pixelSize: 10
                                            }
                                        }
                                        MouseArea {
                                            id: fMa
                                            anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.watchRequested(fTile.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ═══════════ THE BOUNTY BOARD — the manga, posted like pirates ═══════════
            Item {
                width: parent.width; height: 118
                visible: (root.uni.manga || []).length > 0
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "THE BOUNTY BOARD"; color: root.seaGold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "The manga, posted like pirates"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 31 }
                    Text { text: "Oda's source and the crew's spin-offs — tear one down to read it."
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
            Flickable {
                width: parent.width; height: 292
                contentWidth: posterRow.implicitWidth + theme.margin * 2
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                visible: (root.uni.manga || []).length > 0
                Row {
                    id: posterRow
                    x: theme.margin
                    spacing: 20
                    Repeater {
                        model: root.uni.manga
                        delegate: WantedPoster {
                            required property var modelData
                            manga: modelData
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

    // ═══ SagaIsland — one landing on the chart: node on the course, card moored off it ═══
    component SagaIsland: FocusScope {
        id: island
        property var saga: ({})
        property int ord: 0
        readonly property bool high: ord % 2 === 0          // node above the midline?
        readonly property int nodeCY: root.nodeY(ord)
        width: 236; height: 470
        activeFocusOnTab: true

        // ordinal — beside the node, off the card side
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: island.high ? island.nodeCY - 58 : island.nodeCY + 26
            text: (island.saga.n < 10 ? "0" : "") + island.saga.n
            color: islandMa.containsMouse || island.activeFocus ? root.seaGold : Qt.rgba(0.94,0.77,0.29,0.5)
            font.family: theme.display; font.italic: true; font.pixelSize: 21
            Behavior on color { ColorAnimation { duration: 140 } }
        }

        // connector: node → card
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: island.high ? island.nodeCY + 14 : island.nodeCY - 44
            width: 1; height: 30
            color: Qt.rgba(0.94, 0.77, 0.29, islandMa.containsMouse ? 0.55 : 0.25)
        }

        // THE NODE — a sunset buoy; the last saga is the red X (treasure)
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            y: island.nodeCY - 15
            width: 30; height: 30
            Rectangle {                                     // hover halo
                anchors.centerIn: parent
                width: islandMa.containsMouse || island.activeFocus ? 34 : 22
                height: width; radius: width / 2
                color: "transparent"
                border.width: islandMa.containsMouse ? 8 : 5
                border.color: island.saga.treasure === true
                              ? Qt.rgba(0.85, 0.29, 0.20, islandMa.containsMouse ? 0.4 : 0.16)
                              : Qt.rgba(0.97, 0.79, 0.29, islandMa.containsMouse ? 0.34 : 0.13)
                Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
            // the X — two crossed strokes, drawn, treasure red
            Item {
                visible: island.saga.treasure === true
                anchors.centerIn: parent
                width: 22; height: 22
                Rectangle { anchors.centerIn: parent; width: 24; height: 4; radius: 2
                            rotation: 45; color: root.treasureX }
                Rectangle { anchors.centerIn: parent; width: 24; height: 4; radius: 2
                            rotation: -45; color: root.treasureX }
            }
            // the buoy
            Rectangle {
                visible: island.saga.treasure !== true
                anchors.centerIn: parent
                width: 15; height: 15; radius: 7.5
                gradient: Gradient {
                    GradientStop { position: 0; color: root.sunset }
                    GradientStop { position: 1; color: "#c9741f" }
                }
                border.width: 1; border.color: Qt.rgba(1,1,1,0.55)
                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5; color: "#dfeaf0" }
            }
        }

        // THE CARD — moored on the opposite side of the course from the node's swing
        Rectangle {
            id: islandCard
            anchors.horizontalCenter: parent.horizontalCenter
            y: island.high ? island.nodeCY + 44 + (islandMa.containsMouse ? -4 : 0)
                           : island.nodeCY - 44 - 158 + (islandMa.containsMouse ? 4 : 0)
            width: 216; height: 158
            radius: 14
            color: islandMa.containsMouse || island.activeFocus ? Qt.rgba(0.075,0.15,0.21,0.94)
                                                                : Qt.rgba(0.035,0.09,0.135,0.86)
            border.width: island.activeFocus ? 2 : 1
            border.color: island.activeFocus || islandMa.containsMouse
                          ? (island.saga.treasure === true ? root.treasureX : root.seaGold)
                          : Qt.rgba(0.55, 0.75, 0.88, 0.14)
            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Behavior on border.color { ColorAnimation { duration: 140 } }
            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top; anchors.margins: 15
                spacing: 8
                Text {
                    width: parent.width
                    text: island.saga.name || ""
                    color: islandMa.containsMouse ? root.seaGold : theme.ink
                    font.family: theme.display; font.pixelSize: 19
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
                Rectangle {
                    width: epText.implicitWidth + 18; height: 21; radius: 10
                    color: Qt.rgba(0.94,0.77,0.29,0.13)
                    border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.38)
                    Text { id: epText; anchors.centerIn: parent
                           text: island.saga.eps || ""; color: root.seaGold
                           font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold }
                }
                Text {
                    width: parent.width
                    text: island.saga.hook || ""
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    lineHeight: 1.3; wrapMode: Text.WordWrap
                    maximumLineCount: 3; elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: islandMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: if (root.uni.anime) root.watchRequested(root.uni.anime)
        }
        Keys.onReturnPressed: if (root.uni.anime) root.watchRequested(root.uni.anime)
        Keys.onEnterPressed: if (root.uni.anime) root.watchRequested(root.uni.anime)
        Keys.onSpacePressed: if (root.uni.anime) root.watchRequested(root.uni.anime)
    }

    // ═══ WantedPoster — a manga posted on the bounty board: parchment, letterhead, mugshot ═══
    component WantedPoster: FocusScope {
        id: wp
        property var manga: ({})
        width: 176; height: 268
        activeFocusOnTab: true

        // pin shadow-lift on hover
        Rectangle {                                       // the dark board frame behind
            anchors.fill: poster
            anchors.margins: -3
            radius: 4
            color: "#160e06"
            opacity: 0.9
        }
        Rectangle {
            id: poster
            anchors.fill: parent
            anchors.margins: 4
            anchors.topMargin: wpMa.containsMouse || wp.activeFocus ? 0 : 6
            anchors.bottomMargin: wpMa.containsMouse || wp.activeFocus ? 8 : 2
            radius: 2
            color: root.parchment
            border.width: wp.activeFocus ? 2 : 1
            border.color: wp.activeFocus ? root.seaGold : "#8a6a38"
            Behavior on anchors.topMargin { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

            // inner rule frame, like a printed notice
            Rectangle {
                anchors.fill: parent; anchors.margins: 6
                color: "transparent"
                border.width: 1; border.color: Qt.rgba(0.42, 0.30, 0.13, 0.55)
            }

            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top; anchors.margins: 13
                spacing: 7
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "WANTED"
                    color: root.parchInk
                    font.family: theme.display; font.pixelSize: 24; font.weight: Font.Bold
                    font.letterSpacing: 5
                }
                // the mugshot — the real AniList cover in a printed frame
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 132; height: 126
                    color: "#d9c28e"
                    border.width: 1; border.color: Qt.rgba(0.42, 0.30, 0.13, 0.7)
                    Image {
                        anchors.fill: parent; anchors.margins: 3
                        source: wp.manga.cover || ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                }
                // DEAD OR ALIVE strip
                Item {
                    width: parent.width; height: 14
                    Rectangle { anchors.verticalCenter: parent.verticalCenter
                                width: parent.width; height: 1; color: Qt.rgba(0.42,0.30,0.13,0.5) }
                    Rectangle {
                        anchors.centerIn: parent
                        width: doaText.implicitWidth + 14; height: 14
                        color: root.parchment
                        Text { id: doaText; anchors.centerIn: parent
                               text: "DEAD OR ALIVE"; color: Qt.rgba(0.42,0.30,0.13,0.9)
                               font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 2 }
                    }
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: wp.manga.t || ""
                    color: root.parchInk
                    font.family: theme.display; font.pixelSize: 15
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: 9
                text: wpMa.containsMouse ? "OPEN THE MANGA  →" : "TEAR DOWN TO READ"
                color: wpMa.containsMouse ? "#7a4a14" : Qt.rgba(0.42, 0.30, 0.13, 0.75)
                font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 2
            }
        }
        MouseArea {
            id: wpMa
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.seriesRequested(wp.manga.q || wp.manga.t)
        }
        Keys.onReturnPressed: root.seriesRequested(wp.manga.q || wp.manga.t)
        Keys.onEnterPressed: root.seriesRequested(wp.manga.q || wp.manga.t)
        Keys.onSpacePressed: root.seriesRequested(wp.manga.q || wp.manga.t)
    }
}
