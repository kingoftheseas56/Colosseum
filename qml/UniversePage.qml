// UniversePage - the cat-1 (anime) UNIVERSE template, live. PROTOTYPE harness: run standalone
//   native\build\colosseum.exe qml\UniversePage.qml     (or: qml.exe qml\UniversePage.qml)
// Approved design (mock one-piece-universe-v3): banner -> blurb -> the BIG cleaved READ/WATCH
// duality (gold seam + treasure node) -> a row per medium (Manga/Anime/Specials/Movies).
// Data: UniverseApi.js (MAL via Jikan, no login). Manga row routes into A1's MangaSeries.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UniverseApi.js" as Api
import "Universes.js" as Universes

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors the world pages / MangaSeries layer)
    property Item backdrop: null
    // NO default universe: a "One Piece" default made every fresh open construct AS One
    // Piece, fetch One Piece, and only then rename — the stale-flash Hemanth caught
    // (2026-07-12). Empty means "wait for the host to say who I am".
    property string universeName: ""
    // per-universe presentation from the curation point: western IPs on this template say
    // "TV Shows", and readMode "none" suppresses the manga machinery entirely (the LOTR
    // yaoi-anthology lesson: fuzzy manga search is HARM on a non-manga IP).
    readonly property var cfg: Universes.configFor(universeName)
    readonly property string seriesLabel: (cfg && cfg.seriesLabel) ? cfg.seriesLabel : "Anime"
    readonly property bool hasRead: !cfg || cfg.readMode !== "none"
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal seriesRequested(string title)    // manga / READ → A1's MangaSeries.qml
    signal watchRequested(var item)         // anime / movie / WATCH → A4's TheatreSeries.qml

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "",
                         read: { sub: "" }, watch: { sub: "" },
                         manga: [], anime: [], movies: [] })

    function reload() {
        if (!root.universeName.length) return   // no name yet — never load a default universe
        Api.loadUniverse(root.universeName, function(u) { if (u) root.uni = u; });
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    // ---- persistent wallpaper the page floats over ----
    Item {
        id: wall
        anchors.fill: parent
        // Live shell wallpaper (the user's pick), mirrored from the shell's wall item.
        // The bundled default only paints when no backdrop was injected (harness runs).
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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03,0.04,0.07,0.82) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

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
                    // READ half (warm) — hidden entirely on readMode "none" IPs (never route a
                    // western universe into a fuzzy manga search — the LOTR anthology lesson)
                    DualityHalf {
                        id: readHalf
                        visible: root.hasRead
                        anchors.left: parent.left; width: parent.width/2; height: parent.height
                        align: Qt.AlignLeft
                        label: "Read"; sub: root.uni.read ? root.uni.read.sub : "Start the manga"
                        icon: "../assets/icons/manga.svg"
                        artImage: root.uni.read ? (root.uni.read.art || root.uni.read.cover || "") : ""
                        warm: true
                        onActivated: root.seriesRequested(root.uni.manga.length ? root.uni.manga[0].title : root.universeName)
                    }
                    // WATCH half (cool) — takes the whole panel when there's no read side
                    DualityHalf {
                        id: watchHalf
                        anchors.right: parent.right
                        width: root.hasRead ? parent.width/2 : parent.width
                        height: parent.height
                        align: Qt.AlignRight
                        label: "Watch"; sub: root.uni.watch ? root.uni.watch.sub : "Start watching"
                        icon: "../assets/icons/movies.svg"
                        artImage: root.uni.watch ? (root.uni.watch.art || root.uni.watch.cover || "") : ""
                        warm: false
                        onActivated: if (root.uni.watch && root.uni.watch.id) root.watchRequested(root.uni.watch)
                    }
                    // luminous gold seam
                    Rectangle {
                        visible: root.hasRead
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
                        visible: root.hasRead
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

                // ===== MEDIUM ROWS — read routes to the manga page, watch to the Theatre detail =====
                MediumRow { width: parent.width; title: "Manga";  kind: "read";  items: root.hasRead ? root.uni.manga : [] }
                MediumRow { width: parent.width; title: root.seriesLabel; kind: "watch"; items: root.uni.anime }
                MediumRow { width: parent.width; title: "Movies"; kind: "watch"; items: root.uni.movies }
                Item { width: 1; height: 50 }
            }
        }
    }

    // ---- fixed back / system controls over the page ----
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 52; z: 30
        BackAction {
            variant: "capsule"; tip: "Back"
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 22
            onTriggered: root.backRequested()
        }
        Row {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 26
            spacing: 20
            Image { source: "../assets/icons/search.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Image { source: "../assets/icons/minimize.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Image { source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"; width: 17; height: 17; opacity: 0.7
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
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
        property string kind: ""        // "read" → MangaSeries (by title) · "watch" → TheatreSeries (by id)
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
                                        onClicked: {
                                            if (mrow.kind === "read") root.seriesRequested(modelData.title)
                                            else if (mrow.kind === "watch") root.watchRequested(modelData)
                                        } }
                        }
                    }
                }
            }
        }
    }

    ScrollGlide { flick: page }
}
