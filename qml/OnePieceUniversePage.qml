// OnePieceUniversePage — the bespoke GRAND LINE page for One Piece (Agent 5, 2026-07-15,
// Hemanth free-reign commission). Dragon Ball is seven separate anime, so it got seven orbs;
// One Piece is ONE unbroken voyage, so its SIGNATURE is the Grand Line itself — the canon
// sagas charted as island waypoints strung along a golden course line, East Blue to the
// final sea, where the last marker is the treasure. The numbering is EARNED (the sagas ARE
// an ordered voyage), never decor.
//
// Data = the pinned curation in Universes.js (anime / sagas / adaptations / filmEras / manga).
// The one anime + every film dress DIRECTLY by verified Cinemeta id via live.metahub.space
// (IPv4-pinned); manga carry AniList covers (s4.anilist.co, IPv4-pinned). Anime/films →
// A4's TheatreSeries (watchRequested); manga → the manga reader (seriesRequested). Every saga
// waypoint opens the one anime — arcs of a single story, real doors, not fakes.
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

    // One Piece palette — deep sea + sunset gold, over the house dark/gold.
    readonly property color seaGold: theme.gold           // the Grand Line + accents
    readonly property color sunset:  "#f2a04a"            // One Piece sunset (sparing)
    readonly property color foam:    "#dfeaf0"            // node centres / sea-light

    property var uni: ({ name: "", blurb: "", banner: "", anime: null, firstRead: null,
                         sagas: [], adaptations: [], filmEras: [], manga: [] })
    function reload() {
        if (!root.universeName.length) return
        var arr = UDB.universes
        for (var i = 0; i < arr.length; i++)
            if (arr[i].name === root.universeName) { root.uni = arr[i]; return }
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    function poster(id) { return id ? "https://live.metahub.space/poster/medium/" + id + "/img" : "" }

    // ---- the wall ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.05, 0.075, 0.92) }   // deep sea
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: col
            width: page.width
            spacing: 0

            // ===== BANNER =====
            Item {
                width: parent.width; height: 380
                Image {
                    anchors.fill: parent
                    source: root.uni.banner
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.02,0.05,0.075,0.18) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.02,0.05,0.075,0.10) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02,0.05,0.075,0.96) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 30
                    spacing: 9
                    Text { text: "UNIVERSE  ·  THE GRAND LINE"; color: root.sunset; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name; color: theme.ink
                           font.family: theme.display; font.pixelSize: 64 }
                    Text {
                        text: "One voyage  ·  1,100+ episodes  ·  seventeen films  ·  the manga that started it all — by Eiichiro Oda"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // ===== BODY =====
            Column {
                x: 54; width: parent.width - 108; spacing: 0
                topPadding: 26

                // blurb + the two doors: Set sail (anime) / Read from Chapter 1 (manga)
                Row {
                    width: parent.width
                    bottomPadding: 8
                    spacing: 30
                    Text {
                        width: parent.width - doors.width - 30
                        text: root.uni.blurb
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                        lineHeight: 1.5; wrapMode: Text.WordWrap
                        maximumLineCount: 3; elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Row {
                        id: doors
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        // Set sail → the anime
                        Rectangle {
                            radius: 12; height: 50; width: sailRow.implicitWidth + 42
                            visible: !!root.uni.anime
                            gradient: Gradient {
                                GradientStop { position: 0; color: sailMa.containsMouse ? Qt.rgba(0.95,0.63,0.29,0.40) : Qt.rgba(0.95,0.63,0.29,0.20) }
                                GradientStop { position: 1; color: sailMa.containsMouse ? Qt.rgba(0.82,0.45,0.14,0.28) : Qt.rgba(0.82,0.45,0.14,0.12) }
                            }
                            border.width: 1
                            border.color: sailMa.containsMouse ? root.seaGold : Qt.rgba(0.95,0.63,0.29,0.5)
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Row {
                                id: sailRow; anchors.centerIn: parent; spacing: 9
                                Text { text: "Set sail"; color: theme.ink
                                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "→"; color: root.seaGold; font.pixelSize: 16
                                    anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: sailMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.uni.anime) root.watchRequested(root.uni.anime) }
                        }
                        // Read from Chapter 1 → the manga
                        Rectangle {
                            radius: 12; height: 50; width: readRow.implicitWidth + 42
                            visible: !!root.uni.firstRead
                            color: "transparent"
                            border.width: 1
                            border.color: readMa.containsMouse ? theme.ink : Qt.rgba(1,1,1,0.24)
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Row {
                                id: readRow; anchors.centerIn: parent; spacing: 9
                                Text { text: "Read from Ch. 1"; color: readMa.containsMouse ? theme.ink : theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: readMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.uni.firstRead) root.seriesRequested(root.uni.firstRead.t) }
                        }
                    }
                }

                Item { width: 1; height: 34 }

                // ===== SIGNATURE — THE GRAND LINE (the voyage, saga by saga) =====
                Row {
                    spacing: 14
                    Text { text: "The Grand Line"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 28 }
                    Text { text: "the whole voyage, saga by saga — every stop opens the anime"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                           anchors.baseline: parent.children[0].baseline }
                }
                Item { width: 1; height: 18 }

                Flickable {
                    width: parent.width
                    height: 300
                    contentWidth: voyage.width; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Item {
                        id: voyage
                        width: wpRow.width; height: 300

                        // ---- THE COURSE LINE (behind the waypoints, threading node to node) ----
                        Rectangle {
                            x: 118; y: 62
                            width: Math.max(0, voyage.width - 236); height: 2
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.04; color: Qt.rgba(0.94,0.77,0.29,0.6) }
                                GradientStop { position: 0.96; color: Qt.rgba(0.94,0.77,0.29,0.6) }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }

                        Row {
                            id: wpRow
                            spacing: 0
                            Repeater {
                                model: root.uni.sagas
                                delegate: Item {
                                    id: wp
                                    required property var modelData
                                    width: 236; height: 300

                                    // saga ordinal
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: 8
                                        text: (wp.modelData.n < 10 ? "0" : "") + wp.modelData.n
                                        color: wpMa.containsMouse ? root.seaGold : Qt.rgba(0.94,0.77,0.29,0.6)
                                        font.family: theme.display; font.italic: true; font.pixelSize: 22
                                        Behavior on color { ColorAnimation { duration: 140 } }
                                    }

                                    // the node ON the course line (y 62)
                                    Item {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: 50; width: 26; height: 26
                                        // hover halo
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: wpMa.containsMouse ? 30 : 20
                                            height: width; radius: width / 2
                                            color: "transparent"
                                            border.width: wpMa.containsMouse ? 7 : 4
                                            border.color: wpMa.containsMouse ? Qt.rgba(0.97,0.79,0.29,0.32)
                                                                             : Qt.rgba(0.94,0.77,0.29,0.12)
                                            Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                        }
                                        // treasure marker for the final sea; a compass buoy for the rest
                                        Text {
                                            visible: wp.modelData.treasure === true
                                            anchors.centerIn: parent
                                            text: "★"; color: root.seaGold; font.pixelSize: 20
                                            style: Text.Outline; styleColor: Qt.rgba(0.15,0.10,0.02,0.7)
                                        }
                                        Rectangle {
                                            visible: wp.modelData.treasure !== true
                                            anchors.centerIn: parent
                                            width: 14; height: 14; radius: 7
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: root.sunset }
                                                GradientStop { position: 1; color: "#c9741f" }
                                            }
                                            border.width: 1; border.color: Qt.rgba(1,1,1,0.5)
                                            Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5
                                                        color: root.foam }
                                        }
                                    }

                                    // the saga card, moored below the line
                                    Rectangle {
                                        id: card
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: wpMa.containsMouse ? 88 : 92
                                        width: 210; height: 188
                                        radius: 14
                                        color: wpMa.containsMouse ? Qt.rgba(0.09,0.15,0.20,0.9)
                                                                  : Qt.rgba(0.05,0.09,0.13,0.82)
                                        border.width: 1
                                        border.color: wpMa.containsMouse ? root.seaGold : Qt.rgba(0.85,0.92,0.97,0.12)
                                        Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                        Column {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.top: parent.top; anchors.margins: 16
                                            spacing: 9
                                            Text {
                                                width: parent.width
                                                text: wp.modelData.name
                                                color: wpMa.containsMouse ? root.seaGold : theme.ink
                                                font.family: theme.display; font.pixelSize: 20
                                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                Behavior on color { ColorAnimation { duration: 140 } }
                                            }
                                            Rectangle {
                                                width: epText.implicitWidth + 18; height: 22; radius: 11
                                                color: Qt.rgba(0.94,0.77,0.29,0.14)
                                                border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.4)
                                                Text { id: epText; anchors.centerIn: parent
                                                       text: wp.modelData.eps; color: root.seaGold
                                                       font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                                            }
                                            Text {
                                                width: parent.width
                                                text: wp.modelData.hook
                                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                                lineHeight: 1.3; wrapMode: Text.WordWrap
                                                maximumLineCount: 3; elide: Text.ElideRight
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: wpMa
                                        anchors.fill: parent
                                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: if (root.uni.anime) root.watchRequested(root.uni.anime)
                                    }
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 44 }

                // ===== THE ADAPTATIONS — the voyage retold =====
                Column {
                    width: parent.width
                    spacing: 16
                    visible: root.uni.adaptations.length > 0
                    bottomPadding: 40
                    Row {
                        spacing: 12
                        Text { text: "The Voyage Retold"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 24 }
                        Text { text: "beyond the original anime"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Row {
                        spacing: 18
                        Repeater {
                            model: root.uni.adaptations
                            delegate: Item {
                                id: adTile
                                required property var modelData
                                width: 330; height: 150
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 12; clip: true
                                    color: "#0a151d"
                                    border.width: 1
                                    border.color: adMa.containsMouse ? root.seaGold : Qt.rgba(1,1,1,0.12)
                                    Image {
                                        anchors.fill: parent
                                        source: root.poster(adTile.modelData.id)
                                        asynchronous: true; cache: true
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: status === Image.Ready ? (adMa.containsMouse ? 0.55 : 0.35) : 0
                                        Behavior on opacity { NumberAnimation { duration: 220 } }
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        gradient: Gradient {
                                            orientation: Gradient.Horizontal
                                            GradientStop { position: 0; color: Qt.rgba(0.02,0.05,0.075,0.9) }
                                            GradientStop { position: 1; color: Qt.rgba(0.02,0.05,0.075,0.35) }
                                        }
                                    }
                                    Column {
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: 18
                                        spacing: 6
                                        Row {
                                            spacing: 8
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
                }

                // ===== THE FILMS — grouped, chronological =====
                Repeater {
                    model: root.uni.filmEras
                    delegate: Column {
                        id: filmEra
                        required property var modelData
                        width: parent.width
                        spacing: 16
                        bottomPadding: 36
                        Row {
                            spacing: 12
                            Text { text: filmEra.modelData.era; color: theme.ink
                                   font.family: theme.display; font.pixelSize: 24 }
                            Text { text: filmEra.modelData.films.length + " films"
                                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                   anchors.baseline: parent.children[0].baseline }
                        }
                        Flickable {
                            width: parent.width; height: 238
                            contentWidth: filmRow.implicitWidth; contentHeight: height
                            clip: true
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: filmRow
                                spacing: 16
                                Repeater {
                                    model: filmEra.modelData.films
                                    delegate: Item {
                                        id: fTile
                                        required property var modelData
                                        width: 150; height: 226
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8; clip: true
                                            color: "#0a151d"
                                            border.width: 1
                                            border.color: fMa.containsMouse ? root.seaGold : Qt.rgba(1,1,1,0.12)
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
                                                height: 58
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

                // ===== THE MANGA =====
                Column {
                    width: parent.width
                    spacing: 16
                    visible: root.uni.manga.length > 0
                    bottomPadding: 60
                    Row {
                        spacing: 12
                        Text { text: "The Manga"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 24 }
                        Text { text: "Oda's source, and the crew's spin-offs"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Flickable {
                        width: parent.width; height: 238
                        contentWidth: mangaRow.implicitWidth; contentHeight: height
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        Row {
                            id: mangaRow
                            spacing: 18
                            Repeater {
                                model: root.uni.manga
                                delegate: Item {
                                    id: mTile
                                    required property var modelData
                                    width: 150; height: 232
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8; clip: true
                                        color: "#0d1620"
                                        border.width: 1
                                        border.color: mMa.containsMouse ? root.seaGold : Qt.rgba(1,1,1,0.12)
                                        Image {
                                            anchors.fill: parent
                                            source: mTile.modelData.cover || ""
                                            asynchronous: true; cache: true
                                            fillMode: Image.PreserveAspectCrop
                                            opacity: status === Image.Ready ? 1 : 0
                                            Behavior on opacity { NumberAnimation { duration: 220 } }
                                        }
                                        Rectangle {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 54
                                            gradient: Gradient {
                                                GradientStop { position: 0; color: "transparent" }
                                                GradientStop { position: 1; color: Qt.rgba(0,0,0,0.88) }
                                            }
                                        }
                                        Text {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 9
                                            text: mTile.modelData.t
                                            color: theme.ink; font.family: theme.ui
                                            font.pixelSize: 12; font.weight: Font.DemiBold
                                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                        }
                                        MouseArea {
                                            id: mMa
                                            anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.seriesRequested(mTile.modelData.q || mTile.modelData.t)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ChromeScrim { z: 16 }
    BackAction {
        x: theme.margin; y: 28; z: 20
        onTriggered: root.backRequested()
    }
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
                onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.closeRequested() }
        }
    }
}
