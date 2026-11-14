// TooltipStyle — Colosseum's one tooltip look (approved 2026-11-14, Hemanth: mock "A").
// Frosted glass that blurs only what sits under the panel, a 1px gold edge, and Cinzel 13px
// for short hints (the app UI font for anything longer than five words).
//
// Instantiate ONCE (Main.qml) with the window's content item as `backdrop`. It restyles Qt's
// shared ToolTip instance, which every attached `ToolTip.text / ToolTip.visible` in the app
// (KeyboardAction included) uses, so no call site changes. A hand-built `ToolTip { }` can opt
// in with `tooltipStyle.applyTo(myToolTip)`.
//
// Why a one-shot snapshot instead of a live blur: tooltips live in the window overlay, which
// is itself inside `backdrop`. A live grab would capture the tooltip and feed back into itself.
// So the panel holds at opacity 0 for two frames, takes ONE recursive snapshot of the patch
// underneath (the tooltip is invisible in it), then fades in over the blurred snapshot. A hint
// lasts ~2 s, so a still backdrop is indistinguishable from live, and it costs one small grab.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: tooltipSkin
    required property Item backdrop
    visible: false

    readonly property int maxTextWidth: 248   // 280px panel minus 2 x 16px padding
    readonly property color gold: "#f0c44a"
    readonly property color ink: "#fbf6e9"

    Theme { id: theme }
    FontLoader { id: cinzel; source: "../assets/fonts/cinzel/Cinzel-Variable.ttf" }

    function isShort(text) {
        return String(text).trim().split(/\s+/).length <= 5
    }

    property var _styled: []
    function applyTo(tip) {
        if (!tip || tooltipSkin._styled.indexOf(tip) >= 0)
            return
        tooltipSkin._styled.push(tip)
        tip.padding = 0
        tip.topPadding = 9; tip.bottomPadding = 9
        tip.leftPadding = 16; tip.rightPadding = 16
        tip.background = backgroundComponent.createObject(tip, { tip: tip })
        tip.contentItem = textComponent.createObject(tip, { tip: tip })
        tip.enter = enterComponent.createObject(tip)
        tip.exit = exitComponent.createObject(tip)
    }

    // Attached on this item, ToolTip.toolTip is the engine-wide shared instance.
    Component.onCompleted: applyTo(ToolTip.toolTip)

    Component {
        id: enterComponent
        Transition {
            SequentialAnimation {
                PropertyAction { property: "opacity"; value: 0 }
                PauseAnimation { duration: 34 }   // two frames: the backdrop snapshot lands here
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }
    Component {
        id: exitComponent
        Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: 90; easing.type: Easing.InCubic }
        }
    }

    Component {
        id: textComponent
        // Plain Item host: Text.implicitWidth is read-only, and the popup sizes itself from
        // contentItem.implicitWidth. Short hints hug their text; long ones wrap at 248px.
        Item {
            id: host
            property QtObject tip
            implicitWidth: Math.min(Math.ceil(metrics.advanceWidth) + 1, tooltipSkin.maxTextWidth)
            implicitHeight: label.implicitHeight
            Text {
                id: label
                readonly property bool shortHint: tooltipSkin.isShort(text)
                width: host.width
                text: host.tip ? host.tip.text : ""
                textFormat: Text.PlainText
                color: tooltipSkin.ink
                font.family: shortHint && cinzel.status === FontLoader.Ready ? cinzel.name : theme.ui
                font.pixelSize: theme.fontSize(13)
                font.weight: shortHint ? Font.Medium : Font.Normal
                font.letterSpacing: shortHint ? 0.8 : 0
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            TextMetrics { id: metrics; font: label.font; text: label.text }
        }
    }

    Component {
        id: backgroundComponent
        Item {
            id: panel
            property QtObject tip
            readonly property real radius: 12
            readonly property point origin: {
                panel.x; panel.y; panel.width; panel.height; panel.visible
                return tooltipSkin.backdrop ? panel.mapToItem(tooltipSkin.backdrop, 0, 0) : Qt.point(0, 0)
            }

            // One recursive, non-live snapshot of the patch under the panel, refreshed each time
            // the tooltip opens or moves while still transparent.
            ShaderEffectSource {
                id: grab
                anchors.fill: parent
                visible: false
                live: false
                recursive: true
                hideSource: false
                sourceItem: panel.visible ? tooltipSkin.backdrop : null
                sourceRect: Qt.rect(panel.origin.x, panel.origin.y, panel.width, panel.height)
            }
            function refresh() {
                if (panel.visible && panel.width > 0 && panel.height > 0)
                    grab.scheduleUpdate()
            }
            onVisibleChanged: Qt.callLater(panel.refresh)
            onOriginChanged: if (panel.tip && panel.tip.opacity < 0.05) Qt.callLater(panel.refresh)

            // Rounded mask captured explicitly (same reason as Glass.qml: a hidden layer item
            // rendered empty inside the full app scene).
            Rectangle { id: maskShape; anchors.fill: parent; radius: panel.radius; color: "white" }
            ShaderEffectSource {
                id: mask
                anchors.fill: parent
                visible: false
                hideSource: true
                sourceItem: maskShape
            }
            MultiEffect {
                anchors.fill: parent
                source: grab
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1.0
                blurMax: 32
                saturation: 0.35
                maskEnabled: true
                maskSource: mask
            }
            // Dark scrim keeps the text legible over light wallpaper and white pages,
            // then the glass film, gold edge and faint inner highlight.
            Rectangle {
                anchors.fill: parent
                radius: panel.radius
                color: Qt.rgba(0.02, 0.02, 0.035, 0.72)
            }
            Rectangle {
                anchors.fill: parent
                radius: panel.radius
                color: Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.85)
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: panel.radius - 1
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)
            }
        }
    }
}
