// GalaxyUniversePage — the GALAXY universe template (Star Wars). Born 2026-07-12: the
// generic page starved empty here (modern shows don't carry "Star Wars" in their names, so
// the relevance filter killed the canon). This page is built the saga way — curated canon,
// live sources only dress it — and shaped like the thing itself:
//
//   THE SKYWALKER SAGA — a trilogy TRIPTYCH: Prequels / Originals / Sequels standing side
//   by side, each era a column of three episode plates (roman numerals, the era's rhythm).
//   THE STANDALONE STORIES — Rogue One and Solo on their own shelf.
//   THE SERIES — two rails: live-action, then animated.
//
// Watch = Episode IV (the beginning). Everything routes to A4's TheatreSeries.
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga

Item {
    id: root
    anchors.fill: parent

    // shell contract (watch-first — no read verbs on this universe yet)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "",
                         trilogies: [], standalones: [], liveShows: [], animatedShows: [],
                         firstWatch: null })

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        Saga.loadGalaxy(root.universeName, function(u) { if (u) root.uni = u; })
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    // ---- the deep field the page floats over ----
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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.015, 0.02, 0.04, 0.9) }
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
                        GradientStop { position: 0.0; color: Qt.rgba(0.015,0.02,0.04,0.14) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.015,0.02,0.04,0.05) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.015,0.02,0.04,0.93) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 28
                    spacing: 9
                    Text { text: "UNIVERSE  ·  THE GALAXY"; color: theme.gold; font.family: theme.ui
                           font.pixelSize: 12; font.letterSpacing: 4; font.bold: true }
                    Text { text: root.uni.name; color: theme.ink
                           font.family: theme.display; font.pixelSize: 62 }
                    Text { text: root.uni.metaline; color: theme.inkDimmer
                           font.family: theme.ui; font.pixelSize: 14 }
                }
            }

            // ===== BODY =====
            Column {
                x: 54; width: parent.width - 108; spacing: 0
                topPadding: 24

                Row {
                    width: parent.width
                    bottomPadding: 30
                    spacing: 30
                    Text {
                        width: parent.width - beginBtn.width - 30
                        text: root.uni.blurb
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                        lineHeight: 1.5; wrapMode: Text.WordWrap
                        maximumLineCount: 3; elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    // the golden path — where the galaxy begins
                    Rectangle {
                        id: beginBtn
                        radius: 12; height: 50; width: beginRow.implicitWidth + 46
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!root.uni.firstWatch
                        gradient: Gradient {
                            GradientStop { position: 0; color: beginMa.containsMouse ? Qt.rgba(1,1,1,0.23) : Qt.rgba(1,1,1,0.14) }
                            GradientStop { position: 1; color: beginMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05) }
                        }
                        border.width: 1
                        border.color: beginMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.85) : Qt.rgba(1,1,1,0.26)
                        Behavior on border.color { ColorAnimation { duration: 160 } }
                        Row {
                            id: beginRow; anchors.centerIn: parent; spacing: 10
                            Text { text: "Begin the saga — A New Hope"; color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "→"; color: theme.gold; font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea {
                            id: beginMa; anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: if (root.uni.firstWatch) root.watchRequested(root.uni.firstWatch)
                        }
                    }
                }

                // ===== THE SKYWALKER SAGA — the trilogy triptych =====
                Column {
                    width: parent.width
                    spacing: 18
                    visible: root.uni.trilogies.length > 0
                    Row {
                        spacing: 12
                        Text { text: "The Skywalker Saga"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 25 }
                        Text { text: "nine episodes  ·  three eras"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Row {
                        id: triptych
                        width: parent.width
                        spacing: 22
                        readonly property real colW: (width - spacing * 2) / 3
                        Repeater {
                            model: root.uni.trilogies
                            delegate: Rectangle {
                                id: era
                                required property var modelData
                                required property int index
                                width: triptych.colW
                                height: 460
                                radius: 16
                                color: Qt.rgba(1, 1, 1, 0.045)
                                border.width: 1; border.color: Qt.rgba(0.97, 0.97, 0.96, 0.09)
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 12
                                    Text { text: era.modelData.era; color: theme.gold
                                           font.family: theme.display; font.italic: true; font.pixelSize: 19 }
                                    Repeater {
                                        model: era.modelData.films
                                        delegate: Rectangle {
                                            id: ep
                                            required property var modelData
                                            required property int index
                                            width: parent.width; height: 118
                                            radius: 10; clip: true
                                            color: ep.modelData.c1 || "#10141f"
                                            border.width: 1
                                            border.color: epMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                                             : Qt.rgba(0.97,0.97,0.96,0.10)
                                            Image {
                                                anchors.fill: parent
                                                source: ep.modelData.art || ep.modelData.cover || ""
                                                asynchronous: true; cache: true
                                                fillMode: Image.PreserveAspectCrop
                                                opacity: status === Image.Ready ? (epMa.containsMouse ? 0.75 : 0.5) : 0
                                                Behavior on opacity { NumberAnimation { duration: 220 } }
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                gradient: Gradient {
                                                    orientation: Gradient.Horizontal
                                                    GradientStop { position: 0; color: Qt.rgba(0,0,0,0.72) }
                                                    GradientStop { position: 1; color: Qt.rgba(0,0,0,0.15) }
                                                }
                                            }
                                            Row {
                                                anchors.left: parent.left; anchors.leftMargin: 16
                                                anchors.verticalCenter: parent.verticalCenter
                                                spacing: 14
                                                Text {   // the episode numeral — the saga's spine
                                                    text: ["I","II","III","IV","V","VI","VII","VIII","IX"][era.index * 3 + ep.index] || ""
                                                    color: Qt.rgba(0.94, 0.77, 0.29, 0.85)
                                                    font.family: theme.display; font.italic: true; font.pixelSize: 34
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                                Text {
                                                    width: ep.width - 100
                                                    // episode subtitle only — the numeral already says the rest
                                                    text: String(ep.modelData.title).replace(/^Star Wars: Episode [IVX]+ - /, "")
                                                    color: theme.ink; font.family: theme.display; font.pixelSize: 18
                                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }
                                            MouseArea {
                                                id: epMa
                                                anchors.fill: parent
                                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                onClicked: root.watchRequested(ep.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 44 }

                // ===== THE STANDALONE STORIES + THE SERIES =====
                GalaxyRow { width: parent.width; title: "The Standalone Stories"; items: root.uni.standalones }
                GalaxyRow { width: parent.width; title: "The Series — Live Action"; items: root.uni.liveShows }
                GalaxyRow { width: parent.width; title: "The Series — Animated";    items: root.uni.animatedShows }

                Item { width: 1; height: 60 }
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

    // ---- one horizontal rail of watch tiles (canon-ordered) ----
    component GalaxyRow: Column {
        id: grow
        property string title
        property var items: []
        spacing: 16
        visible: items && items.length > 0
        bottomPadding: 34
        Row {
            spacing: 12
            Text { text: grow.title; color: theme.ink
                   font.family: theme.display; font.pixelSize: 25 }
            Text { text: (grow.items ? grow.items.length : 0) + (grow.items && grow.items.length === 1 ? " title" : " titles")
                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                   anchors.baseline: parent.children[0].baseline }
        }
        Flickable {
            width: parent.width; height: 238
            contentWidth: railRow.width; contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: railRow
                spacing: 18
                Repeater {
                    model: grow.items
                    delegate: Item {
                        id: gTile
                        required property var modelData
                        width: 150; height: 232
                        Rectangle {
                            anchors.fill: parent
                            radius: 8; clip: true
                            color: "#131a28"
                            border.width: 1
                            border.color: gMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                            : Qt.rgba(0.97,0.97,0.96,0.12)
                            Image {
                                anchors.fill: parent
                                source: gTile.modelData.cover || ""
                                asynchronous: true; cache: true
                                fillMode: Image.PreserveAspectCrop
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 220 } }
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 52
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: Qt.rgba(0,0,0,0.86) }
                                }
                            }
                            Text {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 9
                                text: gTile.modelData.title
                                color: theme.ink; font.family: theme.ui
                                font.pixelSize: 12; font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap; maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: gMa
                            anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.watchRequested(gTile.modelData)
                        }
                    }
                }
            }
        }
    }
}
