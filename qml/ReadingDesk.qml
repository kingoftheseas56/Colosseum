// ReadingDesk — the Biblio HOME mode-intro widget: a READING DESK. This week's charts lying flat —
// a pile of books with the No. 1 resting cover-up on top (it lifts when you hover), one big chart
// number beside it, the genre chips beneath, and the No. 2 standing face-out on the right. Picked
// over the spine shelf on the 2026-07-04 mock review (agents/colosseum-home-biblio-theatre-mock.html).
//
// Data = the Apple Books Top chart via BiblioApi.loadBiblio (read-only import — A2's file), the one
// metadata spine Biblio runs on. Covers remote + disk-cached via the native launcher, like Bookshelf.
// The pile's slab tints come from ranks 3–5's palette: the rest of the chart IS the pile.

import QtQuick
import "BiblioApi.js" as BiblioApi
import "Catalog.js" as Catalog

Glass {
    id: desk

    property string heading: "Biblio"
    property var chart: []             // ranked chart books from BiblioApi ("top" would shadow Item.top)
    readonly property bool compactLayout: width < 600

    signal clicked()                   // title / books → open the Biblio world (v1, like Bookshelf)
    signal genrePicked(string name)    // a chip → that genre inside Biblio

    radius: 18
    height: desk.compactLayout ? 560 : 400

    Theme { id: theme }

    Component.onCompleted: BiblioApi.loadBiblio(function(r) { desk.chart = (r && r.top) ? r.top : [] })

    function _tint(rank, fallback) {
        return (chart.length > rank && chart[rank].c1) ? chart[rank].c1 : fallback;
    }

    // flat book seen edge-on — one layer of the pile (inline components live at document root)
    component Slab: Rectangle {
        height: 22; radius: 4
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
        // top-edge page highlight
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 1
            height: 3; radius: 2; color: Qt.rgba(1, 1, 1, 0.22)
        }
    }

    // ---- main title (centered) ----
    Text {
        anchors.top: parent.top; anchors.topMargin: desk.compactLayout ? 20 : 28
        anchors.horizontalCenter: parent.horizontalCenter
        text: desk.heading; color: theme.ink
        font.family: theme.display; font.pixelSize: desk.compactLayout ? 30 : 33
        MouseArea {
            anchors.fill: parent; anchors.margins: -12
            cursorShape: Qt.PointingHandCursor; onClicked: desk.clicked()
        }
    }

    // ---- corner label (Bookshelf idiom) ----
    Text {
        anchors.left: parent.left; anchors.leftMargin: 46
        anchors.top: parent.top; anchors.topMargin: 36
        visible: !desk.compactLayout
        text: "Top charts"; color: theme.inkDim
        font.family: theme.display; font.italic: true; font.pixelSize: 22
    }

    // ---- the desk: pile · chart stat + chips · runner-up face-out ----
    Row {
        visible: !desk.compactLayout
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 44
        spacing: 74

        // -- the pile: three flat slabs (ranks 3–5's tints) with No. 1 cover-up on top --
        Item {
            id: pile
            width: 230; height: 262
            anchors.bottom: parent.bottom

            HoverHandler { id: pileHover }

            // soft ground shadow
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: -8
                width: 236; height: 12; radius: 6
                color: Qt.rgba(0, 0, 0, 0.4)
            }
            Slab { width: 198; anchors.horizontalCenter: pile.horizontalCenter; anchors.bottom: pile.bottom; rotation: -1.4; color: desk._tint(4, "#37505f") }
            Slab { width: 184; anchors.horizontalCenter: pile.horizontalCenter; anchors.bottom: pile.bottom; anchors.bottomMargin: 24; rotation: 1.8; color: desk._tint(3, "#6d3a2a") }
            Slab { width: 192; anchors.horizontalCenter: pile.horizontalCenter; anchors.bottom: pile.bottom; anchors.bottomMargin: 48; rotation: -0.8; color: desk._tint(2, "#54432c") }

            // No. 1, cover up
            Item {
                id: topBook
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: pileHover.hovered ? 80 : 72
                width: 122; height: 182
                rotation: pileHover.hovered ? 1.5 : 4
                Behavior on anchors.bottomMargin { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    x: 4; y: 8
                    radius: 8; color: Qt.rgba(0, 0, 0, 0.42)
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 8; clip: true
                    gradient: Gradient {
                        GradientStop { position: 0; color: desk._tint(0, "#5a4a28") }
                        GradientStop { position: 1; color: "#1d160c" }
                    }
                    border.width: pileHover.hovered ? 2 : 1
                    border.color: pileHover.hovered ? theme.gold : Qt.rgba(1, 1, 1, 0.12)

                    Image {
                        anchors.fill: parent
                        source: desk.chart.length > 0 ? desk.chart[0].cover : ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 300; sourceSize.height: 450
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom; anchors.topMargin: 12
                    text: "No. 1 today"; color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.6
                    font.capitalization: Font.AllUppercase
                }
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: desk.clicked()
            }
        }

        // -- the chart stat + genre chips --
        Column {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 30
            spacing: 8

            Text {
                text: "Top 10"; color: theme.ink
                font.family: theme.display; font.pixelSize: 50
            }
            Text {
                width: 250
                text: "today on the Apple Books chart — fresh every day"
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                wrapMode: Text.WordWrap; lineHeight: 1.35
            }
            Item { width: 1; height: 8 }
            Flow {
                width: 270; spacing: 8
                Repeater {
                    model: Catalog.biblioGenres.slice(0, 4)
                    delegate: Rectangle {
                        required property var modelData
                        width: chipText.implicitWidth + 24; height: 26; radius: 13
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        border.color: chipMa.containsMouse ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.5) : theme.edge
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: parent.modelData.name
                            color: chipMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11
                        }
                        MouseArea {
                            id: chipMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: desk.genrePicked(parent.modelData.name)
                        }
                    }
                }
            }
        }

        // -- the runner-up, face-out --
        Column {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 30
            spacing: 10

            Item {
                width: 118; height: 176
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.fill: parent
                    x: 4; y: 8; radius: 8; color: Qt.rgba(0, 0, 0, 0.42)
                }
                Rectangle {
                    anchors.fill: parent
                    y: faceMa.containsMouse ? -8 : 0
                    radius: 8; clip: true
                    Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    gradient: Gradient {
                        GradientStop { position: 0; color: desk._tint(1, "#37505f") }
                        GradientStop { position: 1; color: "#0e161c" }
                    }
                    border.width: faceMa.containsMouse ? 2 : 1
                    border.color: faceMa.containsMouse ? theme.gold : Qt.rgba(1, 1, 1, 0.12)

                    Image {
                        anchors.fill: parent
                        source: desk.chart.length > 1 ? desk.chart[1].cover : ""
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 300; sourceSize.height: 450
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                }
                MouseArea {
                    id: faceMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: desk.clicked()
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "No. 2"; color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.6
                font.capitalization: Font.AllUppercase
            }
        }
    }

    // Phone composition: preserve the chart/cover identity, but use the vertical canvas instead
    // of squeezing the three-column desktop desk into a 360-430 dp viewport.
    Column {
        id: compactDesk
        visible: desk.compactLayout
        anchors.top: parent.top
        anchors.topMargin: 78
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(260, parent.width - 32)
        spacing: 10

        Item {
            width: 132; height: 198
            anchors.horizontalCenter: parent.horizontalCenter
            Rectangle {
                anchors.fill: parent; x: 4; y: 8; radius: 9
                color: Qt.rgba(0, 0, 0, 0.42)
            }
            Rectangle {
                anchors.fill: parent; radius: 9; clip: true
                gradient: Gradient {
                    GradientStop { position: 0; color: desk._tint(0, "#5a4a28") }
                    GradientStop { position: 1; color: "#1d160c" }
                }
                border.width: compactCoverHit.containsMouse ? 2 : 1
                border.color: compactCoverHit.containsMouse ? theme.gold : Qt.rgba(1, 1, 1, 0.12)
                Image {
                    anchors.fill: parent
                    source: desk.chart.length > 0 ? desk.chart[0].cover : ""
                    asynchronous: true; cache: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 300; sourceSize.height: 450
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 220 } }
                }
            }
            MouseArea {
                id: compactCoverHit; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: desk.clicked()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "TOP 10 ? APPLE BOOKS"; color: theme.gold
            font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.8; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Fresh chart picks, with the same Biblio catalogue one tap away."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
            wrapMode: Text.WordWrap; lineHeight: 1.3
        }
        Item { width: 1; height: 4 }
        Flow {
            width: parent.width; spacing: 8
            Repeater {
                model: Catalog.biblioGenres.slice(0, 4)
                delegate: Rectangle {
                    required property var modelData
                    width: compactChipText.implicitWidth + 22; height: 28; radius: 14
                    color: Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: compactChipHit.containsMouse ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.5) : theme.edge
                    Text {
                        id: compactChipText; anchors.centerIn: parent
                        text: parent.modelData.name
                        color: compactChipHit.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 11
                    }
                    MouseArea {
                        id: compactChipHit; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: desk.genrePicked(parent.modelData.name)
                    }
                }
            }
        }
    }
}
