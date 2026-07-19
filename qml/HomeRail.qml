// HomeRail — one reusable AF2 section: a header (optional colored world tag + title + "See all")
// over a horizontally-scrolling track of HomeCards, on the AF2 view_row cadence (theme.rowH) with
// view_pad gutters (theme.homePad). Card shape is set per rail (landscape / portrait / jacket).

import QtQuick

Item {
    id: rail

    // ── public API ──
    property string worldTag: ""            // "" | "Theatre" | "Tankoban" | "Biblio"
    property string railTitle: ""
    property var model: []
    property string cardShape: "landscape"
    signal seeAll()
    signal cardActivated(int index)

    Theme { id: theme }

    readonly property int headerH: 30
    readonly property int cardTotalH: cardShape === "landscape" ? 215 : 264
    // landscape rails hold the AF2 view_row rhythm (theme.rowH); portrait/jacket rails run taller.
    height: cardShape === "landscape" ? theme.rowH : (headerH + 15 + cardTotalH + 14)

    readonly property color tagColor: worldTag === "Theatre" ? theme.tintTheatre
                                     : worldTag === "Tankoban" ? theme.tintTankoban
                                     : worldTag === "Biblio" ? theme.tintBiblio
                                     : theme.inkDim

    // ── header ──
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left; anchors.right: parent.right
        height: rail.headerH

        Row {
            anchors.left: parent.left; anchors.leftMargin: theme.homePad
            anchors.verticalCenter: parent.verticalCenter
            spacing: 13

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: rail.worldTag.length > 0
                text: rail.worldTag.toUpperCase(); color: rail.tagColor
                font.family: theme.displaySans; font.pixelSize: 10
                font.weight: Font.ExtraBold; font.letterSpacing: 2.2
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: rail.railTitle; color: theme.ink
                font.family: theme.displaySans; font.pixelSize: 18
                font.weight: Font.Bold; font.letterSpacing: -0.2
            }
        }

        Text {
            id: seeAllBtn
            anchors.right: parent.right; anchors.rightMargin: theme.homePad
            anchors.verticalCenter: parent.verticalCenter
            text: "See all"
            color: seeMa.containsMouse ? theme.gold : theme.inkDimmer
            font.family: theme.displaySans; font.pixelSize: 12; font.weight: Font.DemiBold
            MouseArea {
                id: seeMa
                anchors.fill: parent; anchors.margins: -8
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: rail.seeAll()
            }
        }
    }

    // ── track ──
    ListView {
        id: track
        anchors.top: header.bottom; anchors.topMargin: 15
        anchors.left: parent.left; anchors.right: parent.right
        height: rail.cardTotalH
        orientation: ListView.Horizontal
        spacing: 15
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        leftMargin: theme.homePad
        rightMargin: theme.homePad
        model: rail.model

        delegate: HomeCard {
            required property var modelData
            required property int index
            shape: rail.cardShape
            art: modelData.art !== undefined ? modelData.art : ""
            worldTag: modelData.tag !== undefined ? modelData.tag : ""
            titleText: modelData.title !== undefined ? modelData.title : ""
            subText: modelData.sub !== undefined ? modelData.sub : ""
            progress: modelData.progress !== undefined ? modelData.progress : 0
            jacketTitle: modelData.jacketTitle !== undefined ? modelData.jacketTitle
                       : (modelData.title !== undefined ? modelData.title : "")
            jacketAuthor: modelData.jacketAuthor !== undefined ? modelData.jacketAuthor : ""
            jacketColor: modelData.jacketColor !== undefined ? modelData.jacketColor : "#2b2350"
            onActivated: rail.cardActivated(index)
        }
    }
}
