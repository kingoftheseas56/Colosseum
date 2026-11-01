// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import ".."

SettingsScaffold {
    id: root

    property Item backdrop: null
    readonly property bool phoneLayout:layout.phoneLayout
    readonly property Item focusedTextControl: {
        const window = root.Window.window
        const focused = window ? window.activeFocusItem : null
        if (!root.layout.touch || !focused || focused.cursorRectangle === undefined) return null
        let ancestor = focused
        while (ancestor && ancestor !== root) ancestor = ancestor.parent
        return ancestor === root ? focused : null
    }
    readonly property real focusedScrollExtent: {
        if (!root.focusedTextControl || root.keyboardOverlap > 0) return 0
        return root.focusedTextControl.mapToItem(scroller.contentItem, 0, 0).y + scroller.height + 16
    }

    function revealFocusedControl() {
        if (!layout.touch || !visible || scroller.height <= 0) return
        const window = root.Window.window
        const focused = window ? window.activeFocusItem : null
        if (!focused) return
        let ancestor = focused
        while (ancestor && ancestor !== root) ancestor = ancestor.parent
        if (ancestor !== root) return
        const position = focused.mapToItem(scroller.contentItem, 0, 0)
        let target = scroller.contentY
        // A fullscreen Android host may not report an IME inset. Keep the field
        // near the top in that case, with enough scroll range for the last input.
        if (focused === root.focusedTextControl && root.keyboardOverlap <= 0)
            target = position.y - 16
        else if (position.y - 16 < target)
            target = position.y - 16
        else if (position.y + focused.height + 24 > target + scroller.height)
            target = position.y + focused.height + 24 - scroller.height
        scroller.contentY = Math.max(0, Math.min(target, scroller.contentHeight - scroller.height))
    }
    onFocusedTextControlChanged: Qt.callLater(root.revealFocusedControl)
    onKeyboardOverlapChanged: Qt.callLater(root.revealFocusedControl)
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(root.revealFocusedControl) }
    }
    property var contentStore: typeof Content !== "undefined" ? Content : null
    readonly property string fallbackWallpaperSource: root.contentStore
        ? (root.contentStore.generation, root.contentStore.assetUrl("assets/wallpaper/captured-motion.jpg")) : ""
    property string eyebrow: ""
    property string headline: ""
    property string detail: ""
    property real panelWidth: 560
    property real panelMinimumHeight: 0
    default property alias panelContent: panelColumn.data

    readonly property bool compact: layout.touch ? (layout.phoneLayout || layout.usableWidth<panelWidth+320+settingsInset*3+52) : width<1040
    readonly property real horizontalMargin: layout.touch ? settingsInset : compact ? 34 : theme.margin
    readonly property real panelActualWidth: Math.min(
        panelWidth,
        width - horizontalMargin * 2)
    readonly property real introWidth: compact
        ? width - horizontalMargin * 2
        : Math.max(320, width - horizontalMargin * 3 - panelActualWidth - 52)

    Theme { id: theme }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        keyboardScroll.handle(event)
    }
    Keys.onReleased: function(event) {
        keyboardScroll.handleRelease(event)
    }

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
            objectName: "fallbackWallpaperImage"
            anchors.fill: parent
            visible: root.backdrop === null
            source: root.fallbackWallpaperSource
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
        objectName: "accountPageFrameScrollRegion"
        anchors.fill: parent
        anchors.bottomMargin: root.keyboardOverlap
        onHeightChanged: Qt.callLater(root.revealFocusedControl)
        onContentHeightChanged: Qt.callLater(root.revealFocusedControl)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(
            height,
            panel.y + panel.height + (root.compact ? 52 : 20),
            root.focusedScrollExtent)

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
                    font.pixelSize: theme.fontSize(12)
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
                    font.pixelSize: theme.fontSize(root.compact ? 50 : Math.min(76, Math.max(56, root.width * 0.052)))
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
                    font.pixelSize: theme.fontSize(14)
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
