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
    property bool navigable: true     // false → hide the Explore affordance (rows with no explore target)
    property bool compactLayout: false
    property real posterWidthOverride: 0
    readonly property real effectivePosterWidth: posterWidthOverride > 0
        ? posterWidthOverride : (compactLayout ? 98 : 132)
    readonly property real effectivePosterHeight: compactLayout || posterWidthOverride > 0
        ? Math.round(effectivePosterWidth * 1.5) : 196
    readonly property real compactCaptionHeight: Math.max(32, Math.ceil(compactCaptionMetrics.lineSpacing) * 2)
    readonly property real effectiveStripHeight: compactLayout
        ? effectivePosterHeight + 8 + compactCaptionHeight : 212
    readonly property real effectiveRailHeight: implicitHeight
    // World-namespaced automation name prefix (naming law: never a bare shared stem — this
    // component is reused verbatim by Tankoban/Theatre/Biblio). Empty by default (no name);
    // a caller that wants Lanista reach opts in with its own world-prefixed stem, e.g.
    // "tankobanTopMangaTile_" (catalogue-independence Slice 3, 2026-08-20).
    property string namePrefix: ""
    signal itemClicked(int index)
    signal exploreClicked()

    width: parent ? parent.width : 800
    spacing: 14

    Theme { id: theme }
    FontMetrics {
        id: compactCaptionMetrics
        font.family: theme.ui
        font.pixelSize: theme.fontSize(13)
        font.weight: Font.DemiBold
    }

    // A flat edge chevron, used at both ends of the strip: serif glyph (ink → gold on hover) over a
    // soft inward scrim so it stays legible on top of a cover. Inline so the widget stays one file.
    component Chevron: Item {
        id: chev
        property string glyph: "›"
        property bool atRight: false
        property bool shown: false
        signal tapped()

        width: 46
        height: parent ? parent.height : top10.effectiveStripHeight
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
        KeyboardAction {
            id: chevronKeyboard
            anchors.fill: parent
            pointerEnabled: false
            // Not a keyboard stop (world-feel, like the mock's rail edges): Left/Right move through
            // the rail's cards, and as a stop the chevron sat between rows and caught Up/Down.
            enabled: false
            focusEnabled: false
            accessibleName: chev.atRight ? qsTr("Show later items") : qsTr("Show earlier items")
            onTriggered: chev.tapped()
        }
    }

    WidgetHeader {
        width: parent.width
        title: top10.title
        moreLabel: "Explore"
        navigable: top10.navigable
        onMoreClicked: top10.exploreClicked()
    }

    Item {
        id: strip
        width: parent.width
        height: top10.effectiveStripHeight
        property int currentIndex: 0
        activeFocusOnTab: top10.items.length > 0

        Keys.onPressed: (event) => top10Keys.handle(event)

        // Entered by the spatial navigator (it lands on a card's face): the first visit selects the
        // item nearest that landing, like the mock; later visits keep the rail's own place.
        property bool keyboardVisited: false
        onActiveFocusChanged: if (strip.activeFocus) strip.keyboardVisited = true
        function keyboardEnterAt(sceneX) {
            if (strip.keyboardVisited)
                return
            var best = -1
            var bestDistance = 0
            for (var i = 0; i < rankRepeater.count; ++i) {
                var item = rankRepeater.itemAt(i)
                if (!item)
                    continue
                var left = item.mapToItem(flick, 0, 0).x
                if (left + item.width < 0 || left > flick.width)
                    continue                                   // off the rail's visible strip
                var distance = Math.abs(item.mapToItem(null, item.width / 2, 0).x - sceneX)
                if (best < 0 || distance < bestDistance) {
                    best = i
                    bestDistance = distance
                }
            }
            if (best >= 0)
                strip.currentIndex = best
        }

        KeyboardCollectionController {
            id: top10Keys
            view: strip
            orientation: "horizontal"
            count: top10.items.length
            positionIndexFn: function(index) {
                var item = rankRepeater.itemAt(index)
                if (!item)
                    return
                var left = item.x
                var right = item.x + item.width
                if (left < flick.contentX)
                    flick.contentX = left
                else if (right > flick.contentX + flick.width)
                    flick.contentX = right - flick.width
                strip.refreshArrows()
            }
            onActivated: top10.itemClicked(index)
        }

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
                objectName: "trendingTop10Row_" + top10.namePrefix
                spacing: top10.compactLayout ? 12 : 30
                leftPadding: top10.compactLayout ? 0 : 6
                Repeater {
                    id: rankRepeater
                    model: top10.items
                    delegate: Item {
                        id: rank
                        objectName: top10.namePrefix.length ? (top10.namePrefix + rank.index) : ""
                        required property var modelData
                        required property int index
                        width: top10.compactLayout ? top10.effectivePosterWidth : num.width + cov.width - 32
                        height: top10.compactLayout ? top10.effectiveStripHeight : 212

                        Text {
                            id: num
                            visible: !top10.compactLayout
                            text: (rank.index + 1)
                            color: Qt.rgba(1, 1, 1, 0.16)
                            font.family: theme.display; font.bold: true; font.pixelSize: theme.fontSize(132)
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.bottomMargin: -8
                        }
                        Item {
                            id: cov
                            objectName: top10.namePrefix.length
                                ? top10.namePrefix + "poster_" + rank.index : ""
                            width: top10.effectivePosterWidth
                            height: top10.effectivePosterHeight
                            x: top10.compactLayout ? 0 : num.width - 32
                            y: top10.compactLayout ? 0 : parent.height - height
                            PortraitTile {
                                id: poster
                                anchors.fill: parent
                                enabled: !top10.compactLayout
                                posterWidth: top10.effectivePosterWidth
                                caption: top10.compactLayout ? "" : rank.modelData.caption
                                cover: rank.modelData.cover !== undefined ? rank.modelData.cover : ""
                                coverCandidates: rank.modelData.coverCandidates || []
                                c1: rank.modelData.c1 !== undefined ? rank.modelData.c1 : "#444"
                                c2: rank.modelData.c2 !== undefined ? rank.modelData.c2 : "#111"
                                keyboardEnabled: false
                                keyboardFocused: strip.activeFocus && strip.currentIndex === rank.index
                                onClicked: {
                                    strip.currentIndex = rank.index
                                    strip.forceActiveFocus(Qt.MouseFocusReason)
                                    top10.itemClicked(rank.index)
                                }
                            }
                        }
                        Rectangle {
                            id: compactRank
                            objectName: top10.namePrefix.length
                                ? top10.namePrefix + "compactRank_" + rank.index : ""
                            visible: top10.compactLayout
                            anchors.left: cov.left; anchors.top: cov.top
                            anchors.leftMargin: 6; anchors.topMargin: 6
                            width: compactRankText.implicitWidth + 10
                            height: compactRankText.implicitHeight + 6
                            radius: 4
                            color: Qt.rgba(0, 0, 0, 0.68)

                            Text {
                                id: compactRankText
                                anchors.centerIn: parent
                                text: String(rank.index + 1)
                                color: theme.ink
                                font.family: theme.ui; font.bold: true; font.pixelSize: theme.fontSize(16)
                            }
                        }
                        Text {
                            id: compactCaption
                            objectName: top10.namePrefix.length
                                ? top10.namePrefix + "compactCaption_" + rank.index : ""
                            visible: top10.compactLayout
                            text: String(rank.modelData.caption || "")
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold
                            anchors.left: cov.left; anchors.right: cov.right; anchors.top: cov.bottom
                            anchors.topMargin: 8
                            height: top10.compactCaptionHeight
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        KeyboardAction {
                            anchors.fill: parent
                            visible: top10.compactLayout
                            focusEnabled: false
                            accessibleName: String(rank.modelData.caption || "")
                            onTriggered: {
                                strip.currentIndex = rank.index
                                strip.forceActiveFocus(Qt.MouseFocusReason)
                                top10.itemClicked(rank.index)
                            }
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
