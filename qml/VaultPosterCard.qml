// VaultPosterCard — the 2:3 poster face for folder/show/season/film rows (Vault Browse face,
// execution plan Slice 4). Consumes ONE browseAt() row and paints the locked design's card
// language (design §6.3) exactly: artwork edge to edge with nothing printed over it; a centered
// one-line title below with elision; a dimmer physical-fact line beneath, in the slot Jellyfin
// uses for the year; near-square corners (~5px); circular corner indicators, never rectangular
// badges; hover dims the art and reveals a play affordance; gold reserved for the uncertainty
// mark only; away reads as reduced ink + desaturation with no hover and no open signal;
// resolving shows the filename on plain ground and crossfades to the settled face in place.
//
// UNWIRED (Slice 4 scope): no page instantiates this yet. Slice 5 assembles the grid. `state`,
// `displayTitle` and `physicalFact` are exposed as readable properties on purpose — they are the
// Lanista/Quick-Test vocabulary Slices 5-9 read.
import QtQuick
import QtQuick.Effects

Item {
    id: card
    required property var row              // {key,nodeType,displayTitle,physicalFact,path,
                                             //  counts:{items},coverRef,state,away}
    property int cardWidth: 150

    // ---- the Lanista/Quick-Test vocabulary ---------------------------------------------------
    readonly property string state: row && row.state !== undefined ? row.state : "resolving"
    readonly property string displayTitle: row && row.displayTitle !== undefined ? row.displayTitle : ""
    readonly property string physicalFact: row && row.physicalFact !== undefined ? row.physicalFact : ""
    readonly property bool away: !!(row && row.away)
    readonly property string coverRef: row && row.coverRef !== undefined ? row.coverRef : ""
    readonly property string nodeKey: row && row.key !== undefined ? row.key : ""
    readonly property string nodeType: row && row.nodeType !== undefined ? row.nodeType : ""
    readonly property int itemCount: (row && row.counts && row.counts.items !== undefined) ? row.counts.items : 0

    // "filename" while unresolved (the design's signature latency state, §4.6); "settled" once
    // there is a face to show (identified / uncertain / localOnly all have SOMETHING to paint,
    // even if that something is only the typographic fallback).
    readonly property string faceState: card.state === "resolving" ? "filename" : "settled"
    // one crossfade run per faceState change; a single Behavior is the one source of the signal
    // so a swap never double-fires (two layers reading the same animated value would).
    property real settledOpacity: card.faceState === "settled" ? 1 : 0
    Behavior on settledOpacity {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic
            onRunningChanged: if (!running) card.faceCrossfaded()
        }
    }
    signal faceCrossfaded()
    signal openRequested(var row)
    signal identifyRequested(var row)

    // away beats uncertainty beats a plain item count (design §4.3); a film/episode/clip's own
    // copy-of-one is not interesting enough to badge — only multi-item container nodes are.
    readonly property bool showIndicator: card.faceState === "settled" && (card.away
        || card.state === "uncertain"
        || (card.itemCount > 0 && card.nodeType !== "film" && card.nodeType !== "episode" && card.nodeType !== "clip"))
    readonly property string indicatorKind: card.away ? "away" : (card.state === "uncertain" ? "uncertain" : "count")

    objectName: "vaultBrowseCard_" + card.nodeKey
    width: cardWidth
    height: artBox.height + textBlock.height + 9
    opacity: card.away ? 0.62 : 1.0

    Theme { id: theme }

    Rectangle {
        id: artBox
        objectName: "vaultBrowseCard_" + card.nodeKey + "_art"
        width: parent.width
        height: Math.round(width * 3 / 2)   // 2:3 poster
        radius: 5                            // near-square corners, not the app's larger panels
        clip: true
        color: Qt.rgba(1, 1, 1, 0.035)
        border.width: card.showIndicator && card.indicatorKind === "uncertain" ? 1 : 0
        border.color: Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.45)

        // ── FILENAME LAYER (resolving): the raw name on plain ground, nothing else printed. ──
        Text {
            id: filenameText
            objectName: "vaultBrowseCard_" + card.nodeKey + "_filename"
            anchors.fill: parent
            anchors.margins: 12
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 6
            text: card.displayTitle
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 11
            opacity: 1 - card.settledOpacity
            visible: opacity > 0
        }

        // ── SETTLED LAYER: typographic fallback (always present underneath) + real art on top. ──
        Item {
            id: settledLayer
            anchors.fill: parent
            opacity: card.settledOpacity
            visible: opacity > 0

            // typographic treatment of the real title — the permanent floor. Never an empty
            // frame, never a broken-image glyph: real art crossfades ONTO this, never replaces it.
            Text {
                anchors.fill: parent
                anchors.margins: 12
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 4
                text: card.displayTitle
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
            }

            Image {
                id: artImage
                objectName: "vaultBrowseCard_" + card.nodeKey + "_artImage"
                anchors.fill: parent
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                source: card.coverRef
                visible: false   // sampled by the effect below so away can desaturate uniformly
            }
            MultiEffect {
                anchors.fill: parent
                source: artImage
                saturation: card.away ? -1.0 : 0.0
                opacity: artImage.status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }

            // hover: dim the art and reveal a play affordance — never a scale transform.
            Rectangle {
                id: hoverScrim
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.55)
                opacity: cardMa.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 44; height: 44; radius: 22
                color: Qt.rgba(0, 0, 0, 0.5)
                border.width: 1; border.color: theme.edge
                opacity: hoverScrim.opacity
                Image {
                    anchors.centerIn: parent
                    width: 14; height: 14
                    source: "../assets/icons/play.svg"
                    fillMode: Image.PreserveAspectFit
                }
            }
        }
        // The hit area is a sibling of settledLayer, not nested inside it: settledLayer is
        // `visible: opacity > 0`, and an invisible item receives no mouse events, so a MouseArea
        // living inside it would only ever be reachable once the card had already settled. That
        // silently broke design §4.6's own contract ("a tile mid-resolve remains fully
        // interactive — openable") — found live driving a real Play click on a resolving Film
        // card in Slice 5's Lanista replay (its identity never resolves without a live catalogue
        // lookup, so it would have stayed permanently unclickable). Hover chrome (the scrim +
        // play glyph above) stays settled-only by design; only the CLICK now works in both faces.
        MouseArea {
            id: cardMa
            objectName: "vaultBrowseCard_" + card.nodeKey + "_hitArea"
            anchors.fill: parent
            enabled: !card.away
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: card.openRequested(card.row)
        }

        // ── circular corner indicator: away glyph, uncertainty mark, or a plain item count. ──
        Rectangle {
            id: indicator
            visible: card.showIndicator
            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 7
            width: 22; height: 22; radius: 11
            color: Qt.rgba(0, 0, 0, 0.62)
            border.width: 1
            border.color: card.indicatorKind === "uncertain"
                ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.5) : theme.edge

            Text {
                visible: card.indicatorKind !== "away"
                anchors.centerIn: parent
                text: card.indicatorKind === "uncertain" ? "?" : String(card.itemCount)
                color: card.indicatorKind === "uncertain" ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
            }
            // away glyph: a slashed circle — no icon asset invented for this, two primitives.
            Item {
                visible: card.indicatorKind === "away"
                anchors.centerIn: parent
                width: 12; height: 12
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 1.4; border.color: theme.inkDim
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 1.15; height: 1.4
                    rotation: 45
                    color: theme.inkDim
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: card.indicatorKind === "uncertain"
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: card.identifyRequested(card.row)
            }
        }
    }

    Column {
        id: textBlock
        anchors.top: artBox.bottom
        anchors.topMargin: 9
        width: parent.width
        spacing: 3

        Text {
            id: titleText
            objectName: "vaultBrowseCard_" + card.nodeKey + "_title"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 1
            text: card.faceState === "filename" ? qsTr("Resolving…") : card.displayTitle
            color: card.faceState === "filename" ? theme.inkDimmer : theme.ink
            font.family: theme.ui
            font.pixelSize: 14
        }
        Text {
            id: factText
            objectName: "vaultBrowseCard_" + card.nodeKey + "_fact"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 1
            // Column excludes invisible children from layout on its own; no manual height
            // collapse needed (an earlier `height: visible ? implicitHeight : 0` binding here
            // created a self-referential binding loop QML warned about — the fix is to trust
            // the positioner instead of fighting it).
            visible: card.faceState === "settled" && card.physicalFact.length > 0
            text: card.physicalFact
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
        }
    }
}
