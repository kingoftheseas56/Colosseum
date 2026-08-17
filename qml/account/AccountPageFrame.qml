// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property Item backdrop: null
    property string eyebrow: ""
    property string headline: ""
    property string detail: ""
    property real panelWidth: 560
    property real panelMinimumHeight: 0
    default property alias panelContent: panelColumn.data

    readonly property bool compact: width < 1040
    readonly property real horizontalMargin: compact ? 34 : theme.margin
    readonly property real panelActualWidth: Math.min(
        panelWidth,
        width - horizontalMargin * 2)
    readonly property real introWidth: compact
        ? width - horizontalMargin * 2
        : Math.max(320, width - horizontalMargin * 3 - panelActualWidth - 52)

    Theme { id: theme }

    Item {
        id: wallpaper
        anchors.fill: parent

        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }

        Image {
            anchors.fill: parent
            visible: root.backdrop === null
            source: "../../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.012, 0.016, 0.031, 0.78)
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(
            height,
            panel.y + panel.height + (root.compact ? 52 : 20))

        ScrollBar.vertical: ScrollBar {
            policy: scroller.contentHeight > scroller.height
                ? ScrollBar.AsNeeded
                : ScrollBar.AlwaysOff
        }

        Item {
            id: intro
            x: root.horizontalMargin
            y: root.compact
                ? 42
                : Math.max(30, (scroller.height - height) / 2)
            width: root.introWidth
            height: introColumn.implicitHeight

            Column {
                id: introColumn
                width: parent.width
                spacing: 0

                Text {
                    width: parent.width
                    text: root.eyebrow
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2.6
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 12 }

                Text {
                    width: parent.width
                    text: root.headline
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: root.compact ? 50 : Math.min(76, Math.max(56, root.width * 0.052))
                    font.weight: Font.Medium
                    font.letterSpacing: -1.2
                    lineHeight: 0.99
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 26 }

                Rectangle {
                    width: 34
                    height: 3
                    radius: 2
                    color: theme.gold
                }

                Item { width: 1; height: 24 }

                Text {
                    width: Math.min(parent.width, 610)
                    text: root.detail
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 14
                    lineHeight: 1.7
                    wrapMode: Text.WordWrap
                }
            }
        }

        Glass {
            id: panel
            backdrop: wallpaper
            width: root.panelActualWidth
            height: Math.max(
                root.panelMinimumHeight,
                panelColumn.implicitHeight + 68)
            x: root.compact
                ? (root.width - width) / 2
                : root.width - root.horizontalMargin - width
            y: root.compact
                ? intro.y + intro.height + 44
                : Math.max(26, (scroller.height - height) / 2)
            radius: 18
            tint: 0.09
            scrim: 0.20

            Column {
                id: panelColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 34
                spacing: 0
            }
        }
    }
}
