import QtQuick
import ".."
import "VinylCardPresentation.js" as Presentation

KeyboardAction {
    focusOnPointer: false
    id: card
    required property var record
    required property var artistData
    property string shape: "square"
    property string badge: ""
    property string artObjectName: ""
    property bool darkCaptionSurface: true
    pointerEnabled: true
    showFocusFrame: true

    readonly property bool songCard: !!record.song
    readonly property real artHeight: shape === "wide" ? width * 9 / 16
        : presentation.status === Loader.Ready ? (presentation.item as Item).implicitHeight : width
    readonly property real artRadius: shape === "round" ? width * 0.1 : (shape === "wide" ? 9 : 13)
    readonly property string secondaryText: songCard
        ? Presentation.secondary(record, Presentation.artist(record)) : String(record.subtitle || "")
    readonly property bool hasCaption: shape === "round" || songCard ? secondaryText.length > 0 : true
    implicitHeight: artHeight + (hasCaption ? (shape === "wide" ? 0 : 14) + captionPlate.height : 0)
    focusRadius: artRadius
    spaceActivates: true
    accessibleName: String(record.title || "") + (record.subtitle ? ", " + record.subtitle : "")

    signal cardRequested(var record)
    onTriggered: cardRequested(card.record)

    Theme { id: theme }

    Loader {
        id: presentation
        width: card.width
        height: card.artHeight
        active: card.shape !== "wide"
        sourceComponent: card.shape === "round" ? artistPresentation
            : card.songCard ? songPresentation : albumPresentation
    }
    Component { id: artistPresentation; VinylArtistWidget {
        objectName: card.artObjectName
        title: String(card.record.title || "")
        artworkSource: card.artistData.artwork(card.record.cover)
        displayFont: theme.display
    } }
    Component { id: songPresentation; VinylSongWidget {
        objectName: card.artObjectName
        title: String(card.record.title || "")
        artist: Presentation.artist(card.record)
        artworkSource: card.artistData.artwork(card.record.cover)
        displayFont: theme.display; uiFont: theme.ui
    } }
    Component { id: albumPresentation; VinylAlbumWidget {
        objectName: card.artObjectName
        title: String(card.record.title || "")
        artworkSource: card.artistData.artwork(card.record.cover)
    } }

    // Artwork container with composed HTML-parity fallback (.cover-fallback / .film-fallback)
    Rectangle {
        id: coverFrame
        visible: card.shape === "wide"
        width: parent.width
        height: card.artHeight
        radius: card.artRadius
        color: card.record && card.record.fallbackTint ? card.record.fallbackTint : "#262c3a"
        border.width: 1
        border.color: card.interactionActive ? theme.edge : Qt.rgba(1, 1, 1, 0.12)
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: card.artRadius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.16) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.58) }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            anchors.bottomMargin: card.shape === "wide" ? 60 : 14
            spacing: 6
            visible: card.shape !== "round"
            Text {
                width: parent.width
                text: (card.artistData && card.artistData.record ? card.artistData.record.name.toUpperCase() : "VINYL")
                    + (card.shape === "wide" ? (card.badge.indexOf("LIVE") === 0 ? " / LIVE" : " / FILM") : " / VINYL")
                textFormat: Text.PlainText
                color: Qt.rgba(0.94, 0.77, 0.29, 0.88)
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.8
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !card.songCard && card.shape !== "round"
                text: String(card.record.title || "")
                textFormat: Text.PlainText
                color: theme.ink
                font.family: theme.display
                font.pixelSize: card.shape === "wide" ? 20 : 18
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }

        RoundedPosterImage {
            id: poster
            visible: sources.length > 0
            objectName: card.shape !== "square" ? card.artObjectName : ""
            anchors.fill: parent
            radius: card.artRadius
            sources: card.shape !== "square" && card.artistData && card.record && card.artistData.artwork(card.record.cover)
                ? [card.artistData.artwork(card.record.cover)] : []
            hovered: card.interactionActive
        }

        // Bottom-left circular play bubble on 16:9 Videos / Live cards (.play-bubble parity)
        Rectangle {
            visible: card.shape === "wide"
            x: 13
            y: parent.height - height - 13
            width: 39
            height: 39
            radius: 20
            color: card.interactionActive ? theme.gold : Qt.rgba(0.031, 0.035, 0.055, 0.82)
            border.width: 1
            border.color: card.interactionActive ? theme.gold : Qt.rgba(1, 1, 1, 0.22)
            Image {
                anchors.centerIn: parent
                width: 16; height: 16
                source: card.interactionActive ? "../../assets/icons/play-dark.svg" : "../../assets/icons/play.svg"
            }
        }

        // Legacy badge pill for non-wide cards when badge is specified
        Rectangle {
            x: 10
            y: card.artHeight - height - 10
            width: badgeText.implicitWidth + 16
            height: 24
            radius: 6
            color: "#d906070b"
            visible: card.badge.length > 0 && card.shape !== "wide"
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: card.badge
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 10
            }
        }
    }

    // Localized dark caption backing (.album-copy parity: rgba(6, 7, 11, 0.84) with 13px bottom radius)
    // Protects 15px/12px caption legibility over bright blurred artist photographs.
    Rectangle {
        id: captionPlate
        visible: card.hasCaption
        y: card.artHeight + (card.shape === "wide" ? 0 : 14)
        width: parent.width
        height: card.shape === "wide" ? Math.max(78, captionColumn.implicitHeight + 16)
            : captionColumn.implicitHeight + (card.darkCaptionSurface ? 16 : 0)
        radius: 0
        bottomLeftRadius: card.shape === "round" ? 0 : 13
        bottomRightRadius: card.shape === "round" ? 0 : 13
        color: (card.darkCaptionSurface && card.shape !== "round")
            ? Qt.rgba(0.024, 0.027, 0.043, 0.84)
            : "transparent"
        border.width: (card.darkCaptionSurface && card.shape !== "round") ? 1 : 0
        border.color: card.interactionActive ? theme.edge : Qt.rgba(1, 1, 1, 0.06)

        Column {
            id: captionColumn
            x: (card.darkCaptionSurface && card.shape !== "round") ? 10 : 0
            y: (card.darkCaptionSurface && card.shape !== "round") ? 8 : 2
            width: parent.width - 2 * x
            spacing: card.shape === "wide" ? 4 : 6

            Text {
                width: parent.width
                visible: !card.songCard && card.shape !== "round"
                text: String(card.record.title || "")
                textFormat: Text.PlainText
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: card.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
            }

            Row {
                width: parent.width
                spacing: 6
                visible: card.shape === "wide" && card.badge.length > 0
                Rectangle {
                    width: wideTagText.implicitWidth + 10
                    height: 18
                    radius: 4
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                    border.width: 1
                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.35)
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: wideTagText
                        anchors.centerIn: parent
                        text: card.badge
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                    }
                }
                Text {
                    width: Math.max(0, parent.width - wideTagText.width - 18)
                    text: card.secondaryText
                    textFormat: Text.PlainText
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Text {
                width: parent.width
                visible: !(card.shape === "wide" && card.badge.length > 0)
                text: card.secondaryText
                textFormat: Text.PlainText
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
                elide: Text.ElideRight
                horizontalAlignment: card.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
            }
        }
    }
}
