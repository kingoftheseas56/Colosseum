// BiblioBookRail — the book-specific horizontal shelf for the Biblio Explore page (plan
// `2026-08-01-biblio-discover-explore.md`, Task 7). Mirrors TheatreCatalogRow's shape (a
// Column: header + Flickable Row of cards) but owns book-specific card behaviour that the
// shared CataloguePosterCard does not: AUTHOR always visible at rest (no hover needed), and
// rating/source revealed on POINTER HOVER *OR* KEYBOARD FOCUS (CataloguePosterCard's rating is
// pointer-hover-only by contract — Biblio's is deliberately broader per this task's spec).
// "Ranked" mode (Top 10) shows ONLY the 1-10 numeral badge — no star/rating glyph competing
// with it. NO row blurb is ever rendered here — title only, per the plan's global constraint.
//
// Test hooks mirror CataloguePosterCard's `testHovered` convention: since a rail holds many
// cards, hover/focus are simulated per-index from the rail (`testHoveredIndex`, `keyboardMode`
// + `currentIndex`) rather than a single bool, so an offscreen harness can pin reveal-gating
// without a real pointer.
import QtQuick

pragma ComponentBehavior: Bound

Column {
    id: rail

    property string title: ""
    // [{ id, title, author, cover, rating, source }] — already normalized by BiblioExplorePage.
    property var items: []
    property bool ranked: false          // Top 10: numeral badge only, no rating glyph
    property bool loading: false         // an independent per-shelf loading state (skeleton cards)
    property bool showSeeAll: true       // mosaics render through a different component, not this
    property int currentIndex: -1
    property bool keyboardMode: false
    property int testHoveredIndex: -1    // offscreen pointer-hover simulation, -1 = none simulated

    signal itemActivated(var item)
    signal seeAllActivated()

    // test/introspection helpers (mirrors TheatreCatalogPage's mainShelfAt/liveShelfCount
    // convention) — an offscreen harness cannot simulate a real pointer, so reveal-gating is
    // asserted directly against the delegate's own computed properties.
    function revealedAt(index) {
        var d = cardRepeater.itemAt(index);
        return d ? d.revealed : false;
    }
    function ratingVisibleAt(index) {
        var d = cardRepeater.itemAt(index);
        return d ? d.ratingVisible : false;
    }
    function sourceVisibleAt(index) {
        var d = cardRepeater.itemAt(index);
        return d ? d.sourceVisible : false;
    }

    width: parent ? parent.width : 900
    spacing: 12
    // a genuinely empty, non-loading rail collapses entirely (extension rows with no results
    // never render a placeholder shelf) — a loading rail still reserves its skeleton space.
    visible: rail.items.length > 0 || rail.loading

    Theme { id: theme }

    Row {
        width: parent.width
        height: railTitle.implicitHeight

        Text {
            id: railTitle
            text: rail.title
            color: theme.ink
            font.family: theme.display; font.pixelSize: 20; font.weight: Font.DemiBold
        }
        Item { width: Math.max(0, parent.width - railTitle.width - seeAllText.width - 16); height: 1 }
        Text {
            id: seeAllText
            visible: rail.showSeeAll
            text: "See All"
            color: seeAllMa.containsMouse ? theme.gold : theme.inkDim
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            MouseArea {
                id: seeAllMa
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: rail.seeAllActivated()
            }
            Keys.onReturnPressed: rail.seeAllActivated()
        }
    }

    Item {
        width: parent.width
        height: rail.ranked ? 236 : 218

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: cardRow.width
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: cardRow
                spacing: rail.ranked ? 26 : 16
                leftPadding: rail.ranked ? 30 : 6
                rightPadding: 20

                // real book cards, one per item
                Repeater {
                    id: cardRepeater
                    model: rail.items
                    delegate: Item {
                        id: card
                        required property var modelData
                        required property int index

                        readonly property var book: card.modelData || {}
                        readonly property bool revealed: hov.hovered
                            || rail.testHoveredIndex === card.index
                            || (rail.keyboardMode && rail.currentIndex === card.index)
                        readonly property string titleText: card.book.title || ""
                        readonly property string authorText: card.book.author || ""
                        readonly property string ratingText: (card.book.rating !== undefined
                            && card.book.rating !== null && String(card.book.rating).length > 0)
                            ? String(card.book.rating) : ""
                        readonly property string sourceText: card.book.source || ""
                        // ranked mode shows ONLY the numeral badge — never a competing star glyph.
                        readonly property bool ratingVisible: card.revealed && !rail.ranked
                            && card.ratingText.length > 0
                        readonly property bool sourceVisible: card.revealed && card.sourceText.length > 0

                        width: 130
                        height: rail.ranked ? 236 : 218
                        activeFocusOnTab: true

                        // large translucent rank numeral, ranked mode only (mirrors TheatreCatalogRow).
                        Text {
                            visible: rail.ranked
                            text: String(card.index + 1)
                            color: Qt.rgba(1, 1, 1, 0.16)
                            font.family: theme.display; font.bold: true; font.pixelSize: 92
                            anchors.left: parent.left
                            anchors.bottom: cover.bottom
                            anchors.bottomMargin: -6
                            anchors.leftMargin: -28
                        }

                        Rectangle {
                            id: cover
                            width: 130; height: 182; radius: 8
                            anchors.top: parent.top
                            color: "#1b1b22"
                            clip: true

                            Rectangle {
                                // skeleton pulse while the shelf itself is loading and no cover yet.
                                visible: rail.loading && !card.book.cover
                                anchors.fill: parent
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }
                            Image {
                                visible: !!card.book.cover
                                anchors.fill: parent
                                source: card.book.cover || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }

                            // rating + source: hover OR keyboard-focus reveal, bottom-left, no blurb.
                            Column {
                                visible: card.ratingVisible || card.sourceVisible
                                anchors.left: parent.left; anchors.leftMargin: 8
                                anchors.right: parent.right; anchors.rightMargin: 8
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                                spacing: 2
                                Text {
                                    visible: card.ratingVisible
                                    text: "★ " + card.ratingText
                                    color: theme.gold
                                    font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                                }
                                Text {
                                    visible: card.sourceVisible
                                    text: card.sourceText
                                    color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 10
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }

                            Rectangle {
                                // keyboard focus ring — never mistaken for the hover reveal itself.
                                anchors.fill: parent; radius: 8
                                visible: rail.keyboardMode && rail.currentIndex === card.index
                                color: "transparent"
                                border.width: 2; border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
                            }
                        }

                        // title + author — BOTH visible at rest, no hover/focus required.
                        Text {
                            id: cardTitle
                            text: card.titleText
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                            anchors.left: cover.left; anchors.right: parent.right
                            anchors.top: cover.bottom; anchors.topMargin: 8
                            elide: Text.ElideRight; maximumLineCount: 1
                        }
                        Text {
                            visible: card.authorText.length > 0
                            text: card.authorText
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11
                            anchors.left: cover.left; anchors.right: parent.right
                            anchors.top: cardTitle.bottom; anchors.topMargin: 2
                            elide: Text.ElideRight; maximumLineCount: 1
                        }

                        HoverHandler { id: hov }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: rail.itemActivated(card.book)
                        }
                        Keys.onReturnPressed: rail.itemActivated(card.book)
                        Keys.onEnterPressed: rail.itemActivated(card.book)
                    }
                }

                // loading-only skeleton cards — reserve exact shelf geometry while items are empty.
                Repeater {
                    model: rail.loading && rail.items.length === 0 ? 6 : 0
                    delegate: Rectangle {
                        width: 130; height: 182; radius: 8
                        anchors.top: parent ? parent.top : undefined
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)
                        SequentialAnimation on opacity {
                            running: rail.loading
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.5; to: 0.85; duration: 800; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.85; to: 0.5; duration: 800; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }
        }
    }
}
