// TrendingTopSeries — the Books-vs-Series ranked shelf, in the SAME visual language as TrendingTop10
// (big ghost rank numerals behind the tiles, WidgetHeader, a Flickable strip with ‹ / › chevrons).
// The only difference is the tile: a stacked gradient "series" card with a gold "N books" chip instead
// of a single portrait cover — the canonical graph carries no cover art, so the gradient IS the tile.
// Fed by SeriesIndex.topSeries(); tapping a rank opens that series' canonical roster.

import QtQuick

Column {
    id: topSeries

    property string title: "Top Series"
    property var items: []            // [{ series, author, count, c1, c2 }]
    signal seriesClicked(int index)

    width: parent ? parent.width : 800
    spacing: 14

    Theme { id: theme }

    // Flat edge chevron (same as TrendingTop10): serif glyph (ink → gold on hover) over an inward scrim.
    component Chevron: Item {
        id: chev
        property string glyph: "›"
        property bool atRight: false
        property bool shown: false
        signal tapped()

        width: 46
        height: parent ? parent.height : 236
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

    // no series-index page to explore into yet → no dead "more" chevron (navigable: false)
    WidgetHeader { width: parent.width; title: topSeries.title; navigable: false }

    Item {
        id: strip
        width: parent.width
        height: 236

        property bool moreLeft: false
        property bool moreRight: false
        function refreshArrows() {
            var can = flick.contentWidth > flick.width + 1
            moreLeft = can && flick.contentX > 1
            moreRight = can && flick.contentX < flick.contentWidth - flick.width - 1
        }
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
                    model: topSeries.items
                    delegate: Item {
                        id: rank
                        required property var modelData
                        required property int index
                        width: num.width + 140 - 32
                        height: 236

                        // big ghost rank numeral — the house tell for a "Top …" row
                        Text {
                            id: num
                            text: (rank.index + 1)
                            color: Qt.rgba(1, 1, 1, 0.16)
                            font.family: theme.display; font.bold: true; font.pixelSize: 132
                            anchors.left: parent.left
                            anchors.bottom: stack.bottom; anchors.bottomMargin: -8
                        }

                        // the series stack tile, overlapping the numeral like TrendingTop10's cover
                        Item {
                            id: stack
                            width: 140; height: 196
                            anchors.left: num.right; anchors.leftMargin: -32; anchors.top: parent.top

                            // two offset cards behind = "this is more than one book"
                            Rectangle { width: 132; height: 196; radius: 10; x: 7; y: 8; rotation: 1.4
                                color: "#15131c"; border.width: 1; border.color: Qt.rgba(1,1,1,0.05) }
                            Rectangle { width: 132; height: 196; radius: 10; x: 4; y: 4; rotation: -1.0
                                color: "#1b1822"; border.width: 1; border.color: Qt.rgba(1,1,1,0.06) }

                            Rectangle {
                                id: scv
                                width: 132; height: 196; radius: 10; clip: true
                                border.width: 1; border.color: rankMa.containsMouse ? theme.gold : Qt.rgba(1,1,1,0.08)
                                scale: rankMa.containsMouse ? 1.03 : 1.0
                                Behavior on scale { NumberAnimation { duration: 130 } }
                                gradient: Gradient {
                                    GradientStop { position: 0; color: rank.modelData.c1 || "#5a3a64" }
                                    GradientStop { position: 1; color: rank.modelData.c2 || "#170d1b" }
                                }
                                // series title washed onto the spine (no cover art in the graph)
                                Text {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.margins: 12
                                    text: rank.modelData.series || ""
                                    color: Qt.rgba(1,1,1,0.94); font.family: theme.display; font.pixelSize: 16
                                    wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                }
                                // gold "N books" chip — the tell that this is a series, not one book
                                Rectangle {
                                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8
                                    radius: 7; height: 23; width: cntRow.implicitWidth + 15
                                    color: Qt.rgba(0.04, 0.035, 0.028, 0.80)
                                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
                                    Row { id: cntRow; anchors.centerIn: parent; spacing: 5
                                        Text { text: rank.modelData.count > 0 ? rank.modelData.count : "•"
                                               color: theme.gold; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.Bold }
                                        Text { text: "books"; color: theme.gold; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 0.4 }
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.top: stack.bottom; anchors.topMargin: 12
                            anchors.left: stack.left
                            width: 132
                            text: rank.modelData.author || ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                            elide: Text.ElideRight; maximumLineCount: 1
                        }

                        MouseArea {
                            id: rankMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: topSeries.seriesClicked(rank.index)
                        }
                    }
                }
            }
        }

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
