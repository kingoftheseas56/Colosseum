// EraUniversePage — the ERAS universe template: screen canons that live as ordered EPOCHS.
// One page, four wearers, each defined entirely by its curated data (Universes.js):
//   James Bond   — the actor eras, Dr. No to No Time to Die (THE DOSSIER)
//   Star Trek    — the fleet chronology: classic shows / streaming shows / film crews (THE FLEET)
//   DC Animated  — the Timmverse timeline: Gotham → the League → the Future (THE TIMELINE)
//   Avatar       — the animated canon and its live-action retelling (THE CANON)
// Era COLUMNS stand in a horizontal gallery (each a column of numbered plates); flat rails
// below when the canon carries extras. Canon-slotted via SagaApi.loadEras — an era can never
// grow a title its curation doesn't name. Everything routes to A4's TheatreSeries.
import QtQuick
import QtQuick.Controls
import "SagaApi.js" as Saga

Item {
    id: root
    anchors.fill: parent

    // shell contract (watch-first)
    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", kicker: "THE ERAS", metaline: "",
                         eras: [], rails: [], firstWatch: null, firstWatchLabel: "" })

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        Saga.loadEras(root.universeName, function(u) { if (u) root.uni = u; })
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.025, 0.045, 0.9) }
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
                        GradientStop { position: 0.0; color: Qt.rgba(0.02,0.025,0.045,0.14) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.02,0.025,0.045,0.05) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02,0.025,0.045,0.93) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 28
                    spacing: 9
                    Text { text: "UNIVERSE  ·  " + root.uni.kicker; color: theme.gold; font.family: theme.ui
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
                        width: parent.width - (beginBtn.visible ? beginBtn.width + 30 : 0)
                        text: root.uni.blurb
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                        lineHeight: 1.5; wrapMode: Text.WordWrap
                        maximumLineCount: 3; elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
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
                            Text { text: root.uni.firstWatchLabel.length ? root.uni.firstWatchLabel : "Begin here"
                                color: theme.ink
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

                // ===== THE ERA GALLERY — epoch columns in a horizontal walk =====
                Flickable {
                    width: parent.width
                    height: eraRow.implicitHeight
                    contentWidth: eraRow.implicitWidth; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    visible: root.uni.eras.length > 0
                    Row {
                        id: eraRow
                        spacing: 22
                        Repeater {
                            model: root.uni.eras
                            delegate: Rectangle {
                                id: era
                                required property var modelData
                                width: 330
                                height: eraCol.implicitHeight + 36
                                radius: 16
                                color: Qt.rgba(1, 1, 1, 0.045)
                                border.width: 1; border.color: Qt.rgba(0.97, 0.97, 0.96, 0.09)
                                visible: era.modelData.items.length > 0
                                Column {
                                    id: eraCol
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 18
                                    spacing: 12
                                    Text { text: era.modelData.era; color: theme.gold
                                           font.family: theme.display; font.italic: true; font.pixelSize: 19 }
                                    Repeater {
                                        model: era.modelData.items
                                        delegate: Rectangle {
                                            id: plate
                                            required property var modelData
                                            required property int index
                                            width: parent.width; height: 96
                                            radius: 10; clip: true
                                            color: "#10141f"
                                            border.width: 1
                                            border.color: plateMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                                                : Qt.rgba(0.97,0.97,0.96,0.10)
                                            Image {
                                                anchors.fill: parent
                                                source: plate.modelData.art || plate.modelData.cover || ""
                                                asynchronous: true; cache: true
                                                fillMode: Image.PreserveAspectCrop
                                                opacity: status === Image.Ready ? (plateMa.containsMouse ? 0.7 : 0.42) : 0
                                                Behavior on opacity { NumberAnimation { duration: 220 } }
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                gradient: Gradient {
                                                    orientation: Gradient.Horizontal
                                                    GradientStop { position: 0; color: Qt.rgba(0,0,0,0.72) }
                                                    GradientStop { position: 1; color: Qt.rgba(0,0,0,0.16) }
                                                }
                                            }
                                            Row {
                                                anchors.left: parent.left; anchors.leftMargin: 14
                                                anchors.right: parent.right; anchors.rightMargin: 10
                                                anchors.verticalCenter: parent.verticalCenter
                                                spacing: 12
                                                Text {   // the epoch ordinal
                                                    text: (plate.index + 1 < 10 ? "0" : "") + (plate.index + 1)
                                                    color: Qt.rgba(0.94, 0.77, 0.29, 0.75)
                                                    font.family: theme.display; font.italic: true; font.pixelSize: 24
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                                Text {
                                                    width: plate.width - 78
                                                    text: plate.modelData.title
                                                    color: theme.ink; font.family: theme.display; font.pixelSize: 16
                                                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }
                                            MouseArea {
                                                id: plateMa
                                                anchors.fill: parent
                                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                onClicked: root.watchRequested(plate.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 40 }

                // ===== flat rails (the canon's extras) =====
                Repeater {
                    model: root.uni.rails
                    delegate: Column {
                        id: rail
                        required property var modelData
                        width: parent.width
                        spacing: 16
                        visible: rail.modelData.items.length > 0
                        bottomPadding: 34
                        Row {
                            spacing: 12
                            Text { text: rail.modelData.title; color: theme.ink
                                   font.family: theme.display; font.pixelSize: 25 }
                            Text { text: rail.modelData.items.length
                                         + (rail.modelData.items.length === 1 ? " title" : " titles")
                                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                   anchors.baseline: parent.children[0].baseline }
                        }
                        Flickable {
                            width: parent.width; height: 238
                            contentWidth: railInner.width; contentHeight: height
                            clip: true
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: railInner
                                spacing: 18
                                Repeater {
                                    model: rail.modelData.items
                                    delegate: Item {
                                        id: rTile
                                        required property var modelData
                                        width: 150; height: 232
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8; clip: true
                                            color: "#131a28"
                                            border.width: 1
                                            border.color: rMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                                            : Qt.rgba(0.97,0.97,0.96,0.12)
                                            Image {
                                                anchors.fill: parent
                                                source: rTile.modelData.cover || ""
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
                                                text: rTile.modelData.title
                                                color: theme.ink; font.family: theme.ui
                                                font.pixelSize: 12; font.weight: Font.DemiBold
                                                wrapMode: Text.WordWrap; maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }
                                        }
                                        MouseArea {
                                            id: rMa
                                            anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.watchRequested(rTile.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

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
}
