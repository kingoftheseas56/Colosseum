// OnePieceUniversePage — ROAD PONEGLYPH CHAMBER.
// Four monumental stones organize the pinned One Piece catalog into Watch, Read, Films,
// and Adaptations. The layout carries the meaning; below the sourced hero lead, the page
// uses labels, titles, years, counts, and actions only. (Agent 5 (Codex), 2026-07-18.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    property string universeName: ""
    property bool reducedMotion: false
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)
    signal seriesRequested(string title)

    Theme { id: theme }

    readonly property color abyss: "#08070A"
    readonly property color basalt: "#17131A"
    readonly property color stoneRed: "#8E1826"
    readonly property color carvedLight: "#FFB35B"
    readonly property color bone: "#F2E6CF"
    readonly property color seaGlass: "#5CB8B2"
    readonly property int pageMargin: Math.max(34, Math.min(76, width * 0.055))

    property var roomLabels: ["WATCH", "READ", "FILMS", "ADAPTATIONS"]
    property var uni: ({ name: "", blurb: "", banner: "", anime: null, firstRead: null,
                         sagas: [], adaptations: [], filmEras: [], manga: [] })

    function reload() {
        if (!root.universeName.length) return
        var found = UDB.configFor(root.universeName)
        if (found && found.name) root.uni = found
    }
    Component.onCompleted: { reload(); reveal.start() }
    onUniverseNameChanged: reload()

    function poster(id) {
        return id ? "https://live.metahub.space/poster/medium/" + id + "/img" : ""
    }
    function backdropFor(id) {
        return id ? "https://live.metahub.space/background/medium/" + id + "/img" : ""
    }
    function watchSeries(pin) {
        return pin ? { "id": pin.id, "type": "series", "title": pin.t } : null
    }
    function watchMovie(pin) {
        return pin ? { "id": pin.id, "type": "movie", "title": pin.t } : null
    }
    function filmCount() {
        var total = 0
        for (var i = 0; i < (root.uni.filmEras || []).length; ++i)
            total += root.uni.filmEras[i].films.length
        return total
    }
    function roomCount(route) {
        if (route === "WATCH") return root.uni.anime ? 1 : 0
        if (route === "READ") return (root.uni.manga || []).length
        if (route === "FILMS") return filmCount()
        if (route === "ADAPTATIONS") return (root.uni.adaptations || []).length
        return 0
    }
    function scrollToRoom(route) {
        var target = route === "WATCH" ? watchRoom
                   : route === "READ" ? readRoom
                   : route === "FILMS" ? filmsRoom : adaptationsRoom
        page.contentY = Math.max(0, target.y - 24)
    }

    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
            opacity: 0.08
        }
        Rectangle { anchors.fill: parent; color: root.abyss }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#120B10" }
                GradientStop { position: 0.46; color: root.abyss }
                GradientStop { position: 1.0; color: "#050508" }
            }
        }
        Canvas {
            anchors.fill: parent
            opacity: 0.2
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "#8E1826"
                ctx.lineWidth = 1
                for (var x = 48; x < width; x += 112) {
                    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x - 180, height); ctx.stroke()
                }
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
            spacing: 0
            opacity: 0

            Item {
                id: hero
                width: parent.width
                height: chamberGrid.columns === 2 ? 790 : 1260

                Item {
                    id: heroTitle
                    x: root.pageMargin
                    y: 62
                    width: parent.width - root.pageMargin * 2
                    height: 176

                    Text {
                        id: title
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: root.uni.name || "One Piece"
                        color: root.bone
                        font.family: theme.display
                        font.pixelSize: Math.max(50, Math.min(82, root.width * 0.06))
                        font.weight: Font.DemiBold
                        lineHeight: 0.9
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.top: title.bottom
                        anchors.topMargin: 12
                        width: Math.min(650, parent.width * 0.62)
                        text: root.uni.blurb || ""
                        color: Qt.rgba(0.95, 0.90, 0.82, 0.68)
                        font.family: theme.ui
                        font.pixelSize: 14
                        lineHeight: 1.3
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        spacing: 12
                        ActionPill {
                            label: "WATCH"
                            enabled: !!root.uni.anime
                            onActivated: if (root.uni.anime) root.watchRequested(root.watchSeries(root.uni.anime))
                        }
                        ActionPill {
                            label: "READ"
                            enabled: !!root.uni.firstRead
                            onActivated: if (root.uni.firstRead) root.seriesRequested(root.uni.firstRead.t)
                        }
                    }
                }

                Grid {
                    id: chamberGrid
                    x: root.pageMargin
                    y: 268
                    width: parent.width - root.pageMargin * 2
                    columns: width >= 820 ? 2 : 1
                    columnSpacing: 18
                    rowSpacing: 18

                    Repeater {
                        model: root.roomLabels
                        delegate: RoadStone {
                            required property string modelData
                            required property int index
                            width: chamberGrid.columns === 2
                                   ? (chamberGrid.width - chamberGrid.columnSpacing) / 2
                                   : chamberGrid.width
                            height: 226
                            route: modelData
                            count: root.roomCount(modelData)
                            order: index
                            onActivated: root.scrollToRoom(route)
                        }
                    }
                }
            }

            Item {
                id: watchRoom
                width: parent.width
                height: 520
                property bool reached: page.contentY + page.height >= watchRoom.y + 120
                opacity: reached ? 1 : 0
                transform: Translate {
                    y: watchRoom.reached ? 0 : 14
                    Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
                }
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                RoomHeader { label: "WATCH"; count: root.roomCount("WATCH"); y: 42 }
                MediaPortal {
                    objectName: "watchPortal"
                    x: root.pageMargin
                    y: 122
                    width: parent.width - root.pageMargin * 2
                    height: 340
                    media: root.uni.anime || ({})
                    actionLabel: "WATCH"
                    detail: "1100+ EPISODES"
                    sourceUrl: root.backdropFor(media.id)
                    onActivated: if (media.id) root.watchRequested(root.watchSeries(media))
                }
            }

            Item {
                id: readRoom
                width: parent.width
                height: root.width >= 1120 ? 800 : root.width >= 760 ? 1060 : root.width >= 520 ? 1580 : 2440
                property bool reached: page.contentY + page.height >= readRoom.y + 120
                opacity: reached ? 1 : 0
                transform: Translate {
                    y: readRoom.reached ? 0 : 14
                    Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
                }
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                RoomHeader { label: "READ"; count: root.roomCount("READ"); y: 38 }
                Flow {
                    id: mangaField
                    x: root.pageMargin
                    y: 118
                    width: parent.width - root.pageMargin * 2
                    spacing: 14
                    Repeater {
                        model: root.uni.manga || []
                        delegate: MangaGate {
                            required property var modelData
                            required property int index
                            manga: modelData
                            featured: index === 0
                            width: featured ? 260 : 158
                            height: featured ? 374 : 258
                            onActivated: if (manga.t) root.seriesRequested(manga.q || manga.t)
                        }
                    }
                }
            }

            Item {
                id: filmsRoom
                width: parent.width
                height: 720
                property bool reached: page.contentY + page.height >= filmsRoom.y + 120
                opacity: reached ? 1 : 0
                transform: Translate {
                    y: filmsRoom.reached ? 0 : 14
                    Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
                }
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                RoomHeader { label: "FILMS"; count: root.roomCount("FILMS"); y: 36 }
                Column {
                    x: root.pageMargin
                    y: 112
                    width: parent.width - root.pageMargin * 2
                    spacing: 22
                    Repeater {
                        model: root.uni.filmEras || []
                        delegate: Item {
                            id: filmRibbon
                            required property var modelData
                            width: parent.width
                            height: 268
                            Text {
                                text: filmRibbon.modelData.era.toUpperCase()
                                color: root.bone
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 2.2
                            }
                            Text {
                                anchors.right: parent.right
                                text: filmRibbon.modelData.films.length
                                color: root.carvedLight
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                            Flickable {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 28
                                height: 236
                                contentWidth: filmRow.implicitWidth
                                contentHeight: height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick
                                boundsBehavior: Flickable.StopAtBounds
                                Row {
                                    id: filmRow
                                    spacing: 12
                                    Repeater {
                                        model: filmRibbon.modelData.films
                                        delegate: FilmFrame {
                                            required property var modelData
                                            film: modelData
                                            onActivated: if (film.id) root.watchRequested(root.watchMovie(film))
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: adaptationsRoom
                width: parent.width
                height: 500
                property bool reached: page.contentY + page.height >= adaptationsRoom.y + 120
                opacity: reached ? 1 : 0
                transform: Translate {
                    y: adaptationsRoom.reached ? 0 : 14
                    Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
                }
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 220 } }
                RoomHeader { label: "ADAPTATIONS"; count: root.roomCount("ADAPTATIONS"); y: 38 }
                Row {
                    id: adaptationsRow
                    x: root.pageMargin
                    y: 122
                    width: parent.width - root.pageMargin * 2
                    spacing: 16
                    Repeater {
                        model: root.uni.adaptations || []
                        delegate: MediaPortal {
                            required property var modelData
                            width: (adaptationsRow.width - adaptationsRow.spacing) / 2
                            height: 312
                            media: modelData
                            upcoming: modelData.upcoming === true
                            actionLabel: "WATCH"
                            sourceUrl: root.backdropFor(media.id)
                            onActivated: if (media.id) root.watchRequested(root.watchSeries(media))
                        }
                    }
                }
            }

            Item { width: 1; height: 88 }
        }

        NumberAnimation {
            id: reveal
            target: contentColumn
            property: "opacity"
            from: 0
            to: 1
            duration: root.reducedMotion ? 0 : 280
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        id: topChrome
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 46
        color: Qt.rgba(0.03, 0.025, 0.035, 0.82)
        border.width: 0
        z: 20

        ChromeAction {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            label: "←"
            onActivated: root.backRequested()
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            ChromeAction { label: "—"; onActivated: root.minimizeRequested() }
            ChromeAction { label: "×"; onActivated: root.closeRequested() }
        }
    }

    component RoomHeader: Item {
        id: roomHeader
        property string label: ""
        property int count: 0
        x: root.pageMargin
        width: parent.width - root.pageMargin * 2
        height: 56
        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: roomHeader.label
            color: root.bone
            font.family: theme.display
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            text: roomHeader.count < 10 ? "0" + roomHeader.count : String(roomHeader.count)
            color: root.carvedLight
            font.family: theme.ui
            font.pixelSize: 13
            font.letterSpacing: 2
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -14
            height: 1
            color: Qt.rgba(1.0, 0.70, 0.36, 0.26)
        }
    }

    component RoadStone: FocusScope {
        id: stone
        property string route: ""
        property int count: 0
        property int order: 0
        property real introOffset: root.reducedMotion ? 0 : 8
        opacity: 0
        signal activated()
        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onEnterPressed: activated()
        transform: Translate { y: stone.introOffset }

        Rectangle {
            id: stoneFace
            anchors.fill: parent
            radius: 5
            color: root.stoneRed
            border.width: stone.activeFocus || stoneMouse.containsMouse ? 2 : 1
            border.color: stone.activeFocus || stoneMouse.containsMouse
                          ? root.carvedLight : Qt.rgba(1.0, 0.70, 0.36, 0.24)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0.18, 0.02, 0.04, 0.55)
            }
            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 88
                x: stoneMouse.containsMouse || stone.activeFocus ? parent.width - width : -width
                opacity: 0.15
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.5; color: root.carvedLight }
                    GradientStop { position: 1; color: "transparent" }
                }
                Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 260; easing.type: Easing.OutCubic } }
            }
            Canvas {
                anchors.fill: parent
                anchors.margins: 18
                opacity: 0.42
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = "#FFB35B"
                    ctx.lineWidth = 1.2
                    var seed = stone.order + 2
                    for (var i = 0; i < 9; ++i) {
                        var x = 22 + ((i * 53 + seed * 17) % Math.max(60, width - 45))
                        var y = 20 + ((i * 31 + seed * 23) % Math.max(55, height - 42))
                        ctx.beginPath()
                        ctx.moveTo(x - 10, y)
                        ctx.lineTo(x, y - 10 - (i % 3) * 3)
                        ctx.lineTo(x + 10 + (i % 2) * 4, y)
                        ctx.lineTo(x, y + 11)
                        ctx.closePath()
                        ctx.stroke()
                    }
                }
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 25
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                text: stone.route
                color: root.bone
                font.family: theme.display
                font.pixelSize: 31
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 28
                text: (stone.count < 10 ? "0" : "") + stone.count
                color: root.carvedLight
                font.family: theme.ui
                font.pixelSize: 13
                font.letterSpacing: 2
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.top: parent.top
                anchors.topMargin: 20
                text: "↘"
                color: stone.activeFocus || stoneMouse.containsMouse ? root.bone : Qt.rgba(0.95, 0.90, 0.82, 0.36)
                font.pixelSize: 18
                opacity: stone.activeFocus || stoneMouse.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 120 } }
            }
        }
        MouseArea {
            id: stoneMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { stone.forceActiveFocus(); stone.activated() }
        }
        SequentialAnimation {
            id: stoneIntro
            PauseAnimation { duration: root.reducedMotion ? 0 : stone.order * 58 }
            ParallelAnimation {
                NumberAnimation { target: stone; property: "introOffset"; to: 0; duration: root.reducedMotion ? 0 : 260; easing.type: Easing.OutCubic }
                NumberAnimation { target: stone; property: "opacity"; from: 0; to: 1; duration: root.reducedMotion ? 0 : 220 }
            }
        }
        Component.onCompleted: stoneIntro.start()
    }

    component MediaPortal: FocusScope {
        id: portal
        property var media: ({})
        property string sourceUrl: ""
        property string actionLabel: "WATCH"
        property string detail: ""
        property bool upcoming: false
        readonly property bool artReady: portalImage.status === Image.Ready
        readonly property color fallbackColor: root.basalt
        signal activated()
        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onEnterPressed: activated()

        Rectangle {
            anchors.fill: parent
            radius: 7
            color: root.basalt
            clip: true
            border.width: portal.activeFocus || portalMouse.containsMouse ? 2 : 1
            border.color: portal.activeFocus || portalMouse.containsMouse
                          ? root.seaGlass : Qt.rgba(0.95, 0.90, 0.82, 0.13)
            Image {
                id: portalImage
                anchors.fill: parent
                source: portal.sourceUrl
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                opacity: status === Image.Ready ? (portalMouse.containsMouse || portal.activeFocus ? 0.78 : 0.58) : 0
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 180 } }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.025, 0.035, 0.96) }
                    GradientStop { position: 0.58; color: Qt.rgba(0.03, 0.025, 0.035, 0.44) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.025, 0.035, 0.14) }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 26
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                width: parent.width * 0.72
                spacing: 7
                Text {
                    text: portal.media.t || ""
                    color: root.bone
                    font.family: theme.display
                    font.pixelSize: Math.min(30, Math.max(21, portal.width * 0.055))
                    font.weight: Font.DemiBold
                    width: parent.width
                    elide: Text.ElideRight
                }
                Row {
                    spacing: 12
                    Text { text: portal.media.year || ""; color: root.carvedLight; font.family: theme.ui; font.pixelSize: 11 }
                    Text { visible: portal.detail.length > 0; text: portal.detail; color: root.bone; opacity: 0.62; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.1 }
                    Text { text: portal.actionLabel; color: root.seaGlass; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
                }
            }
            Rectangle {
                visible: portal.upcoming
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 18
                width: upcomingText.implicitWidth + 16
                height: 25
                radius: 3
                color: Qt.rgba(0.03, 0.025, 0.035, 0.74)
                border.width: 1
                border.color: root.carvedLight
                Text {
                    id: upcomingText
                    anchors.centerIn: parent
                    text: "UPCOMING"
                    color: root.carvedLight
                    font.family: theme.ui
                    font.pixelSize: 9
                    font.letterSpacing: 1.5
                }
            }
        }
        MouseArea {
            id: portalMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { portal.forceActiveFocus(); portal.activated() }
        }
    }

    component MangaGate: FocusScope {
        id: gate
        property var manga: ({})
        property bool featured: false
        signal activated()
        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onEnterPressed: activated()

        Rectangle {
            anchors.fill: parent
            radius: 5
            color: root.basalt
            clip: true
            border.width: gate.activeFocus || gateMouse.containsMouse ? 2 : 1
            border.color: gate.activeFocus || gateMouse.containsMouse
                          ? root.carvedLight : Qt.rgba(0.95, 0.90, 0.82, 0.12)
            Image {
                anchors.fill: parent
                source: gate.manga.cover || ""
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: gate.featured ? 108 : 78
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.025, 0.035, 0.98) }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: gate.featured ? 16 : 11
                spacing: 4
                Text {
                    width: parent.width
                    text: gate.manga.t || ""
                    color: root.bone
                    font.family: theme.ui
                    font.pixelSize: gate.featured ? 16 : 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Text {
                    text: "READ"
                    color: root.carvedLight
                    font.family: theme.ui
                    font.pixelSize: 9
                    font.letterSpacing: 1.7
                    opacity: gateMouse.containsMouse || gate.activeFocus ? 1 : 0.58
                }
            }
        }
        MouseArea {
            id: gateMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { gate.forceActiveFocus(); gate.activated() }
        }
    }

    component FilmFrame: FocusScope {
        id: frame
        property var film: ({})
        signal activated()
        width: 146
        height: 232
        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onEnterPressed: activated()

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: root.basalt
            clip: true
            border.width: frame.activeFocus || frameMouse.containsMouse ? 2 : 1
            border.color: frame.activeFocus || frameMouse.containsMouse
                          ? root.carvedLight : Qt.rgba(0.95, 0.90, 0.82, 0.12)
            Image {
                anchors.fill: parent
                source: root.poster(frame.film.id)
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 74
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.025, 0.035, 0.98) }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 9
                spacing: 2
                Text {
                    width: parent.width
                    text: frame.film.t || ""
                    color: root.bone
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Text { text: frame.film.year || ""; color: root.carvedLight; font.family: theme.ui; font.pixelSize: 9 }
            }
        }
        MouseArea {
            id: frameMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { frame.forceActiveFocus(); frame.activated() }
        }
    }

    component ActionPill: FocusScope {
        id: action
        property string label: ""
        signal activated()
        width: 92
        height: 38
        activeFocusOnTab: true
        Keys.onReturnPressed: if (enabled) activated()
        Keys.onEnterPressed: if (enabled) activated()
        opacity: enabled ? 1 : 0.34
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: actionMouse.containsMouse || action.activeFocus ? Qt.rgba(0.55, 0.09, 0.15, 0.72) : "transparent"
            border.width: 1
            border.color: actionMouse.containsMouse || action.activeFocus ? root.carvedLight : Qt.rgba(0.95, 0.90, 0.82, 0.25)
            Text {
                anchors.centerIn: parent
                text: action.label
                color: root.bone
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.5
            }
        }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            enabled: action.enabled
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { action.forceActiveFocus(); action.activated() }
        }
    }

    component ChromeAction: FocusScope {
        id: chrome
        property string label: ""
        signal activated()
        width: 32
        height: 28
        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onEnterPressed: activated()
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: chromeMouse.containsMouse || chrome.activeFocus ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
            Text { anchors.centerIn: parent; text: chrome.label; color: root.bone; font.family: theme.ui; font.pixelSize: 15 }
        }
        MouseArea {
            id: chromeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { chrome.forceActiveFocus(); chrome.activated() }
        }
    }
}
