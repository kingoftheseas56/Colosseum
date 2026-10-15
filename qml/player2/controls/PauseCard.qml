import QtQuick
import "Player2Browser.js" as Browser

// PauseCard — on pause, the player tells you what you're watching (main-player parity: the Tier-2
// pause info card). A bottom-left glass card that fades and slides in a beat after you pause, showing
// the show wordmark (logo art, else the title in caps), a facts line (S/E · year · runtime · ends), the
// quality line, and the plot. Pure render in the Player 2 house style: shell/session state in, nothing
// out. NO episode rating — a standing Hemanth veto.
Item {
    id: card
    anchors.fill: parent

    property QtObject theme
    property bool shown: false
    property string mediaTitle: ""
    property string mediaLogo: ""
    property string currentEpisodeId: ""
    property string mediaYear: ""
    property string mediaPlot: ""
    property real durationSeconds: 0
    property string endsAtClock: ""
    property var tracks: []

    // Width breakpoints. `tight` mirrors the shipped player's chrome-width rule directly (this item
    // fills the player, so its width IS the chrome width). `barTiny` is FED IN from the transport
    // dock, because production derives it from the room left beside the centre transport, not from
    // any width this card can see - guessing it here made the card fold at a different moment than
    // the dock (cross-model review, 2026-07-26).
    readonly property bool tight: card.width < 680
    property bool barTiny: false

    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"

    // The facts line — S/E · year · runtime · ends. No rating (Hemanth veto).
    function factsLine() {
        var parts = []
        var se = Browser.seasonEpisodeLabel(card.currentEpisodeId); if (se.length) parts.push(se)
        if (card.mediaYear.length) parts.push(card.mediaYear)
        var rt = Browser.runtimeLabel(card.durationSeconds); if (rt.length) parts.push(rt)
        if (card.endsAtClock.length) parts.push("ends " + card.endsAtClock)
        return parts.join("  ·  ")
    }
    readonly property string qualityLine: Browser.codecQualityLine(card.tracks)

    Rectangle {
        id: panel
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        // Both margins fold with the dock in production; fixed values left the card floating too
        // high and too far in on a narrow window.
        anchors.leftMargin: card.barTiny ? 28 : 40
        anchors.bottomMargin: (card.tight ? 116 : 126) + 26   // clears the transport dock
        width: Math.min(card.width - 80, 520)
        height: col.implicitHeight + 40
        radius: 12
        color: Qt.rgba(0.04, 0.04, 0.05, 0.72)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)

        opacity: card.shown ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        transform: Translate {
            y: card.shown ? 0 : 10
            Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        }

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 26
            anchors.rightMargin: 26
            spacing: 9

            // Wordmark: the fetched logo art when it loads, else the title in letterspaced caps.
            Image {
                id: logo
                visible: card.mediaLogo.length > 0 && status === Image.Ready
                source: card.mediaLogo
                fillMode: Image.PreserveAspectFit
                height: 46
                width: Math.min(implicitWidth, parent.width)
                asynchronous: true
            }
            Text {
                visible: !logo.visible
                width: parent.width
                text: card.mediaTitle
                color: card.ink
                // The shipped card renders this in the HUD face at 26 (qml/PlayerPage… :4246). A
                // second typeface here was the one thing that made this card read as "not quite the
                // same card" beside production.
                font.family: "Segoe UI"; font.pixelSize: 26; font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase; font.letterSpacing: 4
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: card.factsLine()
                visible: text.length > 0
                color: card.inkDim
                font.family: "Segoe UI"; font.pixelSize: 12; font.features: ({ "tnum": 1 })
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.qualityLine
                visible: text.length > 0
                color: card.inkDimmer
                font.family: "Segoe UI"; font.pixelSize: 11; font.letterSpacing: 2
                font.features: ({ "tnum": 1 })   // parity: production sets it on this row as well
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.mediaPlot
                visible: text.length > 0
                topPadding: 4
                color: card.inkDim
                font.family: "Segoe UI"; font.pixelSize: 12
                lineHeight: 1.35
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }
}
