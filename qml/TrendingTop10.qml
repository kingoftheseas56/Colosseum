// TrendingTop10 — a ranked "Top 10" widget. A non-row shape that VARIES the board (big ghost
// rank numerals behind solid covers) instead of stacking another plain row. This counts as the
// second and last content row a world page is allowed.
//
// All 10 ranks live in the strip, but only ~6 fit on screen; a ‹ / › chevron pair pans to reveal
// 7–10 — the bare Flickable gave a desktop user no affordance to reach the off-screen ranks.
// Shared by the Tankoban, Theatre and Demo (Biblio) world pages, so this one fix covers them all.

import QtQuick

Column {
    id: top10

    property string title: "Top 10 This Week"
    property var items: []            // [{ caption, cover, c1, c2 }]
    signal itemClicked(int index)

    width: parent ? parent.width : 800
    spacing: 14

    Theme { id: theme }

    // A flat edge chevron, used at both ends of the strip: serif glyph (ink → gold on hover) over a
    // soft inward scrim so it stays legible on top of a cover. Inline so the widget stays one file.
    component Chevron: Item {
        id: chev
        property string glyph: "›"
        property bool atRight: false
        property bool shown: false
        signal tapped()

        width: 46
        height: parent ? parent.height : 212
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, chev.atRight ? 0 : 0.42) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, chev.atRight ? 0.42 : 0) }
            }
        }
        Text {
            anchors.centerIn: parent
            text: chev.glyph
            color: chevMa.containsMouse ? theme.gold : theme.ink
            font.family: theme.display; font.pixelSize: 42
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        MouseArea {
            id: chevMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chev.tapped()
        }
    }

    WidgetHeader { width: parent.width; title: top10.title; moreLabel: "Explore" }

    Item {
        id: strip
        width: parent.width
        height: 212

        // scroll-affordance state. Set IMPERATIVELY from the Flickable's signal handlers (below),
        // NOT as a declarative binding — binding a visibility to Flickable position trips QML's
        // binding-loop detector even when there's no real cycle. Plain bools sidestep it cleanly.
        property bool moreLeft: false
        property bool moreRight: false
        function refreshArrows() {
            var can = flick.contentWidth > flick.width + 1
            moreLeft = can && flick.contentX > 1
            moreRight = can && flick.contentX < flick.contentWidth - flick.width - 1
        }

        // page the strip by ~80% of a screenful, clamped to the ends.
        function pageBy(dir) {
            var step = flick.width * 0.8
            slide.to = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX + dir * step))
            slide.restart()
        }

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: row.width; contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            onContentXChanged: strip.refreshArrows()
            onContentWidthChanged: strip.refreshArrows()
            onWidthChanged: strip.refreshArrows()
            Component.onCompleted: strip.refreshArrows()

            Row {
                id: row
                spacing: 30; leftPadding: 6
                Repeater {
                    model: top10.items
                    delegate: Item {
                        id: rank
                        required property var modelData
                        required property int index
                        width: num.width + cov.width - 32
                        height: 212

                        Text {
                            id: num
                            text: (rank.index + 1)
                            color: Qt.rgba(1, 1, 1, 0.16)
                            font.family: theme.display; font.bold: true; font.pixelSize: 132
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.bottomMargin: -8
                        }
                        PortraitTile {
                            id: cov
                            width: 132; height: 196
                            anchors.left: num.right; anchors.leftMargin: -32; anchors.bottom: parent.bottom
                            caption: rank.modelData.caption
                            cover: rank.modelData.cover !== undefined ? rank.modelData.cover : ""
                            c1: rank.modelData.c1 !== undefined ? rank.modelData.c1 : "#444"
                            c2: rank.modelData.c2 !== undefined ? rank.modelData.c2 : "#111"
                            onClicked: top10.itemClicked(rank.index)
                        }
                    }
                }
            }
        }

        // one animation reused by both chevrons — kept OFF a contentX Behavior so it never fights a manual flick.
        NumberAnimation { id: slide; target: flick; property: "contentX"; duration: 300; easing.type: Easing.OutCubic }

        Chevron {
            glyph: "‹"; atRight: false
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            shown: strip.moreLeft
            onTapped: strip.pageBy(-1)
        }
        Chevron {
            glyph: "›"; atRight: true
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            shown: strip.moreRight
            onTapped: strip.pageBy(1)
        }
    }
}
