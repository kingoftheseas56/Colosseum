// UniversePage - the cat-1 (anime) UNIVERSE template, live. PROTOTYPE harness: run standalone
//   native\build\colosseum.exe qml\UniversePage.qml     (or: qml.exe qml\UniversePage.qml)
// Approved design (mock one-piece-universe-v3): banner -> blurb -> the BIG cleaved READ/WATCH
// duality (gold seam + treasure node) -> a row per medium (Manga/Anime/Specials/Movies).
// Data: UniverseApi.js (MAL via Jikan, no login). Manga row routes into A1's MangaSeries.qml.
import QtQuick
import QtQuick.Layouts
import "UniverseApi.js" as Api

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors the world pages / MangaSeries layer)
    property Item backdrop: null
    property string universeName: "One Piece"
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(string title)    // manga / READ → A1's MangaSeries.qml

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "",
                         read: { sub: "" }, watch: { sub: "" },
                         manga: [], anime: [], shows: [], specials: [], movies: [] })

    function reload() { Api.loadUniverse(root.universeName, function(u) { if (u) root.uni = u; }); }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    // ---- persistent wallpaper the page floats over ----
    Item {
        id: wall
        anchors.fill: parent
        Image { anchors.fill: parent; source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.82) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: page.width
            spacing: 0

            // ===== BANNER =====
            Item {
                width: parent.width; height: 360
                Image {
                    anchors.fill: parent
                    source: root.uni.banner
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.035,0.043,0.07,0.12) }
                        GradientStop { position: 0.45; color: Qt.rgba(0.035,0.043,0.07,0.04) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.035,0.043,0.07,0.92) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 28
                    spacing: 9
                    Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name || "One Piece"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 62 }
                    Text { text: root.uni.metaline; color: theme.inkDimmer
                           font.family: theme.ui; font.pixelSize: 14 }
                }
                // rotating-spread hint
                Row {
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.rightMargin: 54; anchors.bottomMargin: 34; spacing: 7
                    Text { text: "COLOR SPREADS"; color: theme.inkDimmer; font.pixelSize: 10
                           font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                    Repeater { model: 5
                        Rectangle { width: index === 0 ? 20 : 7; height: 7; radius: index === 0 ? 4 : 4
                                    color: index === 0 ? theme.gold : Qt.rgba(1,1,1,0.35)
                                    anchors.verticalCenter: parent.verticalCenter } }
                }
            }

            // ===== BODY =====
            Column {
                x: 54; width: parent.width - 108; spacing: 0
                topPadding: 26

                Text {
                    bottomPadding: 30
                    text: root.uni.blurb || "Loading the universe…"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                    lineHeight: 1.5; wrapMode: Text.WordWrap
                    maximumLineCount: 3; elide: Text.ElideRight
                    width: Math.min(parent.width, 760)
                }

                // ===== THE BIG CLEAVED READ / WATCH DUALITY =====
                Rectangle {
                    width: parent.width; height: 330; radius: 22; clip: true
                    color: "transparent"; border.width: 1; border.color: theme.edge
                    // READ half (warm)
                    DualityHalf {
                        id: readHalf
                        anchors.left: parent.left; width: parent.width/2; height: parent.height
                        align: Qt.AlignLeft
                        label: "Read"; sub: root.uni.read ? root.uni.read.sub : "Start the manga"
                        icon: "../assets/icons/manga.svg"
                        artImage: root.uni.read ? (root.uni.read.cover || "") : ""
                        warm: true
                        onActivated: root.seriesRequested(root.uni.manga.length ? root.uni.manga[0].title : root.universeName)
                    }
                    // WATCH half (cool)
                    DualityHalf {
                        id: watchHalf
                        anchors.right: parent.right; width: parent.width/2; height: parent.height
                        align: Qt.AlignRight
                        label: "Watch"; sub: root.uni.watch ? root.uni.watch.sub : "Start the anime"
                        icon: "../assets/icons/movies.svg"
                        artImage: root.uni.watch ? (root.uni.watch.cover || "") : ""
                        warm: false
                    }
                    // luminous gold seam
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 2; z: 3
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 0.18; color: Qt.rgba(0.94,0.77,0.29,0.9) }
                            GradientStop { position: 0.5; color: "#fff7df" }
                            GradientStop { position: 0.82; color: Qt.rgba(0.94,0.77,0.29,0.9) }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                    Rectangle {  // treasure-diamond node
                        anchors.centerIn: parent; width: 30; height: 30; radius: 6; z: 4
                        rotation: 45
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#fff3cf" }
                            GradientStop { position: 1.0; color: "#e0a634" }
                        }
                        border.width: 1; border.color: "#fff7df"
                    }
                }

                Item { width: 1; height: 44 }  // spacer

                // ===== MEDIUM ROWS =====
                MediumRow { width: parent.width; title: "Manga";    routes: true; items: root.uni.manga }
                MediumRow { width: parent.width; title: "Anime";    items: root.uni.anime }
                MediumRow { width: parent.width; title: "Shows";    items: root.uni.shows }
                MediumRow { width: parent.width; title: "Specials"; items: root.uni.specials }
                MediumRow { width: parent.width; title: "Movies";   items: root.uni.movies }
                Item { width: 1; height: 50 }
            }
        }
    }

    // ---- fixed back / system controls over the page ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        Rectangle {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            width: 42; height: 34; radius: 17
            color: backMa.hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(0,0,0,0.40)
            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.pixelSize: 22 }
            HoverHandler { id: backMa }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }

    // ---- one half of the cleaved duality ----
    component DualityHalf: Item {
        id: half
        property string label
        property string sub
        property string icon
        property string artImage: ""
        property bool warm: true
        property int align: Qt.AlignLeft
        clip: true

        Rectangle {  // art: gradient base (shows while loading) + the medium's own photo
            id: art
            anchors.fill: parent
            scale: hov.hovered ? 1.05 : 1.0
            Behavior on scale { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: half.warm ? "#5e1717" : "#0c2c46" }
                GradientStop { position: 0.5; color: half.warm ? "#b83f2c" : "#1f6f9a" }
                GradientStop { position: 1.0; color: half.warm ? "#e0a64a" : "#4fb4cf" }
            }
            Image {
                anchors.fill: parent; source: half.artImage
                fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true
                opacity: half.artImage ? 1 : 0
            }
        }
        Rectangle {  // legibility scrim
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: half.align === Qt.AlignLeft ? Qt.rgba(0.03,0.02,0.04,0.74) : Qt.rgba(0.03,0.02,0.04,0.4) }
                GradientStop { position: 1.0; color: half.align === Qt.AlignLeft ? Qt.rgba(0.03,0.02,0.04,0.4) : Qt.rgba(0.03,0.02,0.04,0.74) }
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: half.align === Qt.AlignLeft ? parent.left : undefined
            anchors.right: half.align === Qt.AlignRight ? parent.right : undefined
            anchors.leftMargin: 56; anchors.rightMargin: 56
            spacing: 8
            Rectangle {
                width: 48; height: 48; radius: 13
                color: Qt.rgba(0.94,0.77,0.29,0.16); border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.5)
                anchors.right: half.align === Qt.AlignRight ? parent.right : undefined
                Image { anchors.centerIn: parent; width: 26; height: 26; source: half.icon
                        fillMode: Image.PreserveAspectFit }
            }
            Text { text: half.label; color: theme.ink; font.family: theme.display; font.pixelSize: 64
                   anchors.right: half.align === Qt.AlignRight ? parent.right : undefined }
            Text { text: half.sub; color: Qt.rgba(1,1,1,0.9); font.family: theme.ui; font.pixelSize: 15
                   anchors.right: half.align === Qt.AlignRight ? parent.right : undefined }
            Text { text: half.align === Qt.AlignLeft ? "Start reading →" : "Start watching →"
                   color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.bold: true
                   opacity: hov.hovered ? 1 : 0; topPadding: 6
                   anchors.right: half.align === Qt.AlignRight ? parent.right : undefined
                   Behavior on opacity { NumberAnimation { duration: 180 } } }
        }
        signal activated()
        HoverHandler { id: hov }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: half.activated() }
    }

    // ---- a medium row: serif header + horizontal cover tiles ----
    component MediumRow: Column {
        id: mrow
        property string title
        property bool routes: false
        property var items: []
        visible: items && items.length > 0
        spacing: 14
        bottomPadding: 30

        Row {
            spacing: 10
            Text { text: title; color: theme.ink; font.family: theme.display; font.pixelSize: 23
                   anchors.verticalCenter: parent.verticalCenter }
            Text { text: "›"; color: theme.gold; font.pixelSize: 20
                   anchors.verticalCenter: parent.verticalCenter }
            Text { text: (items ? items.length : 0) + " titles"; color: theme.inkDimmer
                   font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
        }

        Flickable {
            width: parent.width; height: 200
            contentWidth: tileRow.implicitWidth; contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            clip: true; boundsBehavior: Flickable.StopAtBounds
            Row {
                id: tileRow
                spacing: 16
                Repeater {
                    model: items
                    delegate: Item {
                        width: 132; height: 196
                        Rectangle {
                            id: cv
                            anchors.fill: parent; radius: 10; clip: true
                            border.width: 1; border.color: cvHov.hovered ? theme.gold : Qt.rgba(1,1,1,0.08)
                            scale: cvHov.hovered ? 1.04 : 1.0
                            Behavior on scale { NumberAnimation { duration: 130 } }
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: modelData.c1 || "#33445d" }
                                GradientStop { position: 1.0; color: modelData.c2 || "#0c1118" }
                            }
                            Image {
                                anchors.fill: parent; source: modelData.cover || ""
                                fillMode: Image.PreserveAspectCrop; cache: true; asynchronous: true
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 56
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.72) }
                                }
                            }
                            Text {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 9; text: modelData.title || ""
                                color: "white"; font.family: theme.ui; font.pixelSize: 12
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            }
                            HoverHandler { id: cvHov }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: if (mrow.routes) root.seriesRequested(modelData.title) }
                        }
                    }
                }
            }
        }
    }
}
