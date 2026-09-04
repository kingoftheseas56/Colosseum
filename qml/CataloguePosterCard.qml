// CataloguePosterCard — the shared catalogue poster tile. It owns activation, title, hover, focus,
// and metadata; it delegates the artwork itself to RoundedPosterImage (genuine 12px crop, bounded
// decode, candidate fallback, cheap depth). Two profiles select geometry + motion from the frozen
// CatalogueVisualMetrics tokens:
//   • "classic" (default) — the current 132px single-line card; every surface not yet cleared for
//     the polish stays pixel-behaviour identical (Discover/Tankoban/Comics until their own gate).
//   • "gallery" — the approved Theatre polish: 148px poster, 12px radius, a reserved two-line title,
//     a quiet 7px/260ms hover, a bottom-only scrim, an SVG rating star, and a source attribution.
// In BOTH profiles the IMDb rating is POINTER-HOVER-ONLY; keyboard focus draws the focus halo but
// never the hover reveal. Gallery has NO centered play ring and NO Unicode poster-control glyph.
import QtQuick
import "PosterSourcePolicy.js" as PosterPolicy
import "CatalogueVisualMetrics.js" as Metrics

Item {
    id: card

    property var item: null
    property bool keyboardFocused: false
    property bool skeleton: false
    // test hook: simulate pointer hover offscreen (the harness cannot move a real pointer).
    property bool testHovered: false
    // "classic" | "gallery" — default classic so a shared-file change never silently restyles a world.
    property string visualProfile: "classic"
    // optional source attribution, revealed bottom-right on gallery hover only (Theatre supplies "IMDb").
    property string hoverSourceText: ""
    // Biblio hook (Discover shared-shell, Task 5, arc 2026-08-01): when true, the reveal
    // block (rating + source) opens on KEYBOARD FOCUS too, not pointer hover alone. Default
    // false so Tankoban/Theatre stay byte-identical (focus never impersonates hover for them).
    property bool revealOnFocus: false
    // Biblio hook: when true, render `item.author` PERSISTENTLY under the title (not gated
    // by hover/focus at all) — the author is always-on identity, not reveal-only metadata.
    property bool showAuthorAtRest: false
    signal activated(var item)

    readonly property var _m: Metrics.profile(card.visualProfile)
    readonly property bool _gallery: card.visualProfile === "gallery"

    readonly property bool effectiveHovered: hov.hovered || card.testHovered
    // the reveal block opens on pointer hover always, and ALSO on keyboard focus when a
    // caller opted in via revealOnFocus (Biblio); every other caller keeps hover-only.
    readonly property bool revealActive: card.effectiveHovered || (card.revealOnFocus && card.keyboardFocused)
    readonly property string capText: card.item ? (card.item.title || card.item.caption || "") : ""
    // Discover items carry `year`/`rating`; the Theatre catalogue carries `releaseInfo`/`imdbRating`.
    readonly property string yearText: card.item
        ? String((card.item.year !== undefined ? card.item.year : card.item.releaseInfo) || "") : ""
    readonly property string ratingValue: card.item
        ? String((card.item.rating !== undefined ? card.item.rating : card.item.imdbRating) || "") : ""
    readonly property string ratingText: card.ratingValue.length > 0 ? ("★ " + card.ratingValue) : ""
    // the author (Biblio's Discover cards) — always-on when showAuthorAtRest is true.
    readonly property string authorText: card.item ? (card.item.author || "") : ""
    // the rating is visible ONLY under an active reveal (hover, or hover+focus when opted in).
    readonly property bool ratingVisible: !card.skeleton && card.revealActive && card.ratingValue.length > 0
    // an invariant the harness pins: the rating can never be showing while the card is at rest.
    readonly property bool ratingVisibleAtRest: card.ratingVisible && !card.effectiveHovered
    // gallery source attribution — reveal-only, present only when a label was supplied.
    readonly property bool sourceVisible: card._gallery && !card.skeleton
                                          && card.revealActive && card.hoverSourceText.length > 0
    // classic keeps its centered play ring on hover; gallery never shows one.
    readonly property bool centerPlayVisible: !card._gallery && !card.skeleton && card.effectiveHovered

    // profile tokens exposed for harness inspection and layout math upstream.
    readonly property int posterWidthToken: card._m.posterWidth
    readonly property int posterRadiusToken: card._m.posterRadius
    readonly property int titleLineCount: card._m.titleLines
    readonly property int titleReserve: card._m.titleMinHeight

    // accessible name mirrors the title so the button announces exactly what it opens.
    readonly property string accessibleName: card.capText

    // ordered candidate URLs handed to the shared art primitive (medium→small for Metahub).
    readonly property var coverCandidates: card.item
        ? PosterPolicy.candidates(card.item.cover || "", card.item.coverCandidates || [])
        : []

    Theme { id: theme }

    Accessible.role: Accessible.Button
    Accessible.name: card.accessibleName
    Accessible.onPressAction: {
        if (!card.skeleton) card.activated(card.item)
    }

    // ── poster plane (art + hover reveal), lifted as a render transform so geometry never shifts ──
    Item {
        id: frame
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: Math.floor(width * card._m.posterRatio)

        transform: Translate {
            y: card.effectiveHovered ? -card._m.hoverLift : 0
            Behavior on y { NumberAnimation { duration: card._m.hoverDuration; easing.type: Easing.OutCubic } }
        }

        // skeleton placeholder — the loading cell, reserving exact geometry with a soft pulse.
        Rectangle {
            visible: card.skeleton
            anchors.fill: parent
            radius: card._m.posterRadius
            color: Qt.rgba(1, 1, 1, 0.06)
            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)
            SequentialAnimation on opacity {
                running: card.skeleton
                loops: Animation.Infinite
                NumberAnimation { from: 0.5; to: 0.9; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.9; to: 0.5; duration: 800; easing.type: Easing.InOutSine }
            }
        }

        // the real artwork — genuine rounded crop, bounded decode, candidate fallback, cheap depth.
        RoundedPosterImage {
            id: art
            // Automation identity (Lanista): ride the card's name so the bridge can read this
            // renderer's candidate/decode state for a specific item. Empty when the card is unnamed.
            objectName: card.objectName.length > 0 ? card.objectName + "_art" : ""
            visible: !card.skeleton
            anchors.fill: parent
            radius: card._m.posterRadius
            revealDuration: card._m.imageRevealDuration
            hovered: card.effectiveHovered
            sources: card.coverCandidates
        }

        // ── hover reveal: a rounded scrim + metadata (and, classic only, the centered play ring) ──
        Item {
            id: reveal
            anchors.fill: parent
            visible: !card.skeleton
            opacity: card.revealActive ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: card._m.imageRevealDuration } }

            // classic scrim — the current full-height gradient (unchanged look for Discover).
            Rectangle {
                visible: !card._gallery
                anchors.fill: parent
                radius: card._m.posterRadius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.55; color: Qt.rgba(6 / 255, 5 / 255, 12 / 255, 0.30) }
                    GradientStop { position: 1.0; color: Qt.rgba(6 / 255, 5 / 255, 12 / 255, 0.92) }
                }
            }
            // gallery scrim — bottom-only, letting the artwork dominate (mock 2026-08-02).
            Rectangle {
                visible: card._gallery
                anchors.fill: parent
                radius: card._m.posterRadius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.54; color: "transparent" }
                    GradientStop { position: 0.64; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.10) }
                    GradientStop { position: 1.0; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.92) }
                }
            }

            // classic centered play ring (gold outline + ▶). Gallery omits it entirely.
            Rectangle {
                visible: !card._gallery
                anchors.centerIn: parent
                width: 46; height: 46; radius: 23
                color: Qt.rgba(8 / 255, 7 / 255, 14 / 255, 0.34)
                border.width: 1.5; border.color: theme.gold
                Text {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 2
                    text: "▶"; color: theme.gold; font.pixelSize: 16
                }
            }

            // classic metadata — year + ★rating, bottom-left (unchanged look for Discover).
            Row {
                visible: !card._gallery
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

            // gallery metadata — SVG star + rating bottom-left, source attribution bottom-right.
            Item {
                visible: card._gallery
                anchors.left: parent.left; anchors.leftMargin: 11
                anchors.right: parent.right; anchors.rightMargin: 11
                anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                height: 14
                Row {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    visible: card.ratingValue.length > 0
                    Image {
                        source: "../assets/icons/rating-star.svg"
                        width: 12; height: 12; sourceSize.width: 24; sourceSize.height: 24
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: card.ratingValue
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    visible: card.hoverSourceText.length > 0
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: card.hoverSourceText
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                }
            }
        }
    }

    // keyboard focus ring — a DOUBLE soft-gold halo overlay (never triggers the hover reveal, no lift)
    Rectangle {
        anchors.fill: frame; radius: card._m.posterRadius
        visible: card.keyboardFocused
        color: "transparent"
        border.width: 2; border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: card._m.posterRadius + 2; color: "transparent"
            border.width: 3; border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.18)
        }
    }

    // title — classic: single line 12px; gallery: two reserved lines 13px DemiBold (35px minimum).
    Text {
        id: titleText
        visible: !card.skeleton
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: frame.bottom; anchors.topMargin: card._gallery ? 10 : 8
        height: card._gallery ? Math.max(card.titleReserve, implicitHeight) : implicitHeight
        text: card.capText
        color: card._gallery ? theme.ink : (card.effectiveHovered ? theme.ink : theme.inkDim)
        font.family: theme.ui; font.pixelSize: card._m.titlePixels; font.weight: Font.DemiBold
        wrapMode: card._gallery ? Text.WordWrap : Text.NoWrap
        maximumLineCount: card._m.titleLines
        lineHeight: card._gallery ? 1.15 : 1.0
        elide: Text.ElideRight
    }
    // skeleton title bar (reserves the title row's space too)
    Rectangle {
        visible: card.skeleton
        anchors.left: parent.left; anchors.top: frame.bottom; anchors.topMargin: card._gallery ? 10 : 8
        width: parent.width * 0.7; height: 12; radius: 5
        color: Qt.rgba(1, 1, 1, 0.08)
    }

    // author — Biblio's always-on identity line (never gated by hover/focus/reveal).
    // Anchored to the title's PAINTED text, not its reserved block: the title box always
    // holds two lines so grid rows stay even, which left a phantom empty line between a
    // one-line title and its author (Hemanth's eyes, 2026-08-06). contentHeight follows
    // the real line count, so the byline hugs the title at any title length.
    Text {
        id: authorLabel
        visible: card.showAuthorAtRest && !card.skeleton && card.authorText.length > 0
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: titleText.top; anchors.topMargin: titleText.contentHeight + 3
        text: card.authorText
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 11
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    HoverHandler { id: hov; enabled: !card.skeleton }
    MouseArea {
        anchors.fill: parent
        enabled: !card.skeleton
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activated(card.item)   // skeletons are disabled -> never activate
    }
}
