// CataloguePosterCard — the shared Discover poster tile, extracted verbatim from
// DiscoverBrowser so the Theatre deep-catalogue rails and See-all grid render exactly like
// Discover. Poster + title at rest; on POINTER hover it lifts, drops a scrim, and reveals a
// gold play ring plus year and the IMDb-derived `★ <value>` rating. Keyboard focus draws the
// focus ring but NEVER the hover reveal (the rating stays hidden until the pointer is over it).
import QtQuick

Item {
    id: card

    property var item: null
    property bool keyboardFocused: false
    property bool skeleton: false
    // test hook: simulate pointer hover offscreen (the harness cannot move a real pointer).
    property bool testHovered: false
    signal activated(var item)

    readonly property bool effectiveHovered: hov.hovered || card.testHovered
    readonly property string capText: card.item ? (card.item.title || card.item.caption || "") : ""
    // Discover items carry `year`/`rating`; the Theatre catalogue carries `releaseInfo`/`imdbRating`.
    // The shared card reads whichever is present so both worlds render identically.
    readonly property string yearText: card.item
        ? String((card.item.year !== undefined ? card.item.year : card.item.releaseInfo) || "") : ""
    readonly property string ratingValue: card.item
        ? String((card.item.rating !== undefined ? card.item.rating : card.item.imdbRating) || "") : ""
    readonly property string ratingText: card.ratingValue.length > 0 ? ("★ " + card.ratingValue) : ""
    // the rating is visible ONLY under an active pointer hover, and only when present.
    readonly property bool ratingVisible: !card.skeleton && card.effectiveHovered && card.ratingValue.length > 0
    // an invariant the harness pins: the rating can never be showing while the card is at rest.
    readonly property bool ratingVisibleAtRest: card.ratingVisible && !card.effectiveHovered

    Theme { id: theme }

    Rectangle {
        id: frame
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: Math.floor(width * 1.5)
        radius: 8; clip: true
        color: card.skeleton ? Qt.rgba(1, 1, 1, 0.06) : "#181a20"
        border.width: 1
        border.color: card.skeleton ? Qt.rgba(1, 1, 1, 0.09)
                     : card.effectiveHovered ? Qt.rgba(1, 1, 1, 0.42) : theme.edge
        // hover lift — a render transform, so the grid geometry never shifts.
        transform: Translate {
            y: card.effectiveHovered ? -4 : 0
            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
        // skeleton pulse — only while this is a placeholder
        SequentialAnimation on opacity {
            running: card.skeleton
            loops: Animation.Infinite
            NumberAnimation { from: 0.5; to: 0.9; duration: 800; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.9; to: 0.5; duration: 800; easing.type: Easing.InOutSine }
        }
        Rectangle {
            visible: !card.skeleton
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#343d52" }
                GradientStop { position: 1; color: "#121620" }
            }
            Text {
                anchors.centerIn: parent; width: parent.width - 20
                text: card.capText
                color: Qt.rgba(1, 1, 1, 0.66)
                font.family: theme.display; font.pixelSize: 15; font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight
            }
        }
        Image {
            visible: !card.skeleton
            anchors.fill: parent
            source: card.item ? (card.item.cover || "") : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
        }
        // ── hover reveal: scrim + year·rating rise, a gold play ring appears ──
        Item {
            id: reveal
            anchors.fill: parent
            visible: !card.skeleton
            opacity: card.effectiveHovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.55; color: Qt.rgba(6/255, 5/255, 12/255, 0.30) }
                    GradientStop { position: 1.0; color: Qt.rgba(6/255, 5/255, 12/255, 0.92) }
                }
            }
            Rectangle {                       // centered play ring
                anchors.centerIn: parent
                width: 46; height: 46; radius: 23
                color: Qt.rgba(8/255, 7/255, 14/255, 0.34)
                border.width: 1.5; border.color: theme.gold
                Text {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 2
                    text: "▶"; color: theme.gold; font.pixelSize: 16
                }
            }
            Row {                             // meta, bottom-left
                anchors.left: parent.left; anchors.leftMargin: 11
                anchors.right: parent.right; anchors.rightMargin: 11
                anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                spacing: 9
                Text {
                    visible: card.yearText.length > 0
                    text: card.yearText
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
                Text {
                    visible: card.ratingValue.length > 0
                    text: card.ratingText
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                }
            }
        }
    }
    // keyboard focus ring — a DOUBLE soft-gold halo overlay (never triggers the hover reveal)
    Rectangle {
        anchors.fill: frame; radius: 8
        visible: card.keyboardFocused
        color: "transparent"
        border.width: 2; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.55)
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: 10; color: "transparent"
            border.width: 3; border.color: Qt.rgba(240/255, 196/255, 74/255, 0.18)
        }
    }
    Text {
        visible: !card.skeleton
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: frame.bottom; anchors.topMargin: 8
        text: card.capText
        color: card.effectiveHovered ? theme.ink : theme.inkDim
        font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    // skeleton title bar (reserves the title row's space too)
    Rectangle {
        visible: card.skeleton
        anchors.left: parent.left; anchors.top: frame.bottom; anchors.topMargin: 8
        width: parent.width * 0.7; height: 12; radius: 5
        color: Qt.rgba(1, 1, 1, 0.08)
    }
    HoverHandler { id: hov; enabled: !card.skeleton }
    MouseArea {
        anchors.fill: parent
        enabled: !card.skeleton
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activated(card.item)   // skeletons are disabled -> never activate
    }
}
