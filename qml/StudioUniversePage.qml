// StudioUniversePage — the STUDIO universe template (Studio Ghibli): a body of work, not a
// franchise. The page is THE FILMOGRAPHY — the studio's features as one chronological wall,
// each film a poster with its order plate; nothing else competes with the wall. The golden
// path is the studio's defining film ("Begin with Spirited Away"). Canon-slotted via
// SagaApi.loadStudio; every tile routes to A4's TheatreSeries.
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
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal watchRequested(var item)

    Theme { id: theme }
    property var uni: ({ name: "", blurb: "", banner: "", metaline: "",
                         films: [], firstWatch: null, firstWatchLabel: "" })

    function reload() {
        if (!root.universeName.length) return      // never load a default universe
        Saga.loadStudio(root.universeName, function(u) { if (u) root.uni = u; })
    }
    Component.onCompleted: reload()
    onUniverseNameChanged: reload()

    // ---- the screening-room dark ----
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
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.025, 0.03, 0.035, 0.9) }
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
                        GradientStop { position: 0.0; color: Qt.rgba(0.025,0.03,0.035,0.14) }
                        GradientStop { position: 0.5; color: Qt.rgba(0.025,0.03,0.035,0.05) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.025,0.03,0.035,0.93) }
                    }
                }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 54; anchors.rightMargin: 54; anchors.bottomMargin: 28
                    spacing: 9
                    Text { text: "UNIVERSE  ·  THE STUDIO"; color: theme.gold; font.family: theme.ui
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

                // ===== THE FILMOGRAPHY — the studio's whole body of work as one wall =====
                Column {
                    width: parent.width
                    spacing: 18
                    visible: root.uni.films.length > 0
                    Row {
                        spacing: 12
                        Text { text: "The Filmography"; color: theme.ink
                               font.family: theme.display; font.pixelSize: 25 }
                        Text { text: root.uni.films.length + " features  ·  chronological"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                               anchors.baseline: parent.children[0].baseline }
                    }
                    Flow {
                        width: parent.width
                        spacing: 20
                        Repeater {
                            model: root.uni.films
                            delegate: Item {
                                id: film
                                required property var modelData
                                required property int index
                                width: 168; height: 260
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 10; clip: true
                                    color: "#1b2420"
                                    border.width: 1
                                    border.color: fMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.7)
                                                                    : Qt.rgba(0.97,0.97,0.96,0.12)
                                    Image {
                                        anchors.fill: parent
                                        source: film.modelData.cover || ""
                                        asynchronous: true; cache: true
                                        fillMode: Image.PreserveAspectCrop
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 220 } }
                                    }
                                    Rectangle {   // the order plate — the wall reads as a chronology
                                        anchors.top: parent.top; anchors.left: parent.left
                                        anchors.margins: 8
                                        width: 30; height: 30; radius: 8
                                        color: Qt.rgba(0, 0, 0, 0.62)
                                        border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.55)
                                        Text {
                                            anchors.centerIn: parent
                                            text: film.index + 1
                                            color: theme.gold; font.family: theme.ui
                                            font.pixelSize: 13; font.weight: Font.Bold
                                        }
                                    }
                                    Rectangle {
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 56
                                        gradient: Gradient {
                                            GradientStop { position: 0; color: "transparent" }
                                            GradientStop { position: 1; color: Qt.rgba(0,0,0,0.86) }
                                        }
                                    }
                                    Text {
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 10
                                        text: film.modelData.title
                                        color: theme.ink; font.family: theme.ui
                                        font.pixelSize: 12; font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap; maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                                MouseArea {
                                    id: fMa
                                    anchors.fill: parent
                                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.watchRequested(film.modelData)
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
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.fullscreenRequested()
            }
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
