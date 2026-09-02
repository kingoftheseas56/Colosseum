pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import "PlayerFocusContainment.js" as FocusContainment

Item {
    id: bar
    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.top: parent ? parent.top : undefined
    height: 130
    visible: open
    z: 25

    property bool open: false
    property var player
    property var focusReturnItem: null
    focusPolicy: open ? Qt.TabFocus : Qt.NoFocus
    function moveFocus(forward) {
        var w = bar.Window.window
        var item = w ? w.activeFocusItem : null
        if (!item || !item.nextItemInFocusChain) return false
        var next = item.nextItemInFocusChain(forward)
        if (!next || next === item) return false
        next.forceActiveFocus(Qt.TabFocusReason)
        return true
    }
    onOpenChanged: {
        if (open) {
            var w = bar.Window.window; bar.focusReturnItem = w ? w.activeFocusItem : null
            Qt.callLater(function() { bar.forceActiveFocus(Qt.PopupFocusReason) })
        } else if (bar.focusReturnItem) {
            var target = bar.focusReturnItem; bar.focusReturnItem = null
            Qt.callLater(function() { if (target && target.visible && target.enabled && target.forceActiveFocus) target.forceActiveFocus(Qt.TabFocusReason) })
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { bar.open = false; event.accepted = true }
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) event.accepted = bar.moveFocus(true)
        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) event.accepted = bar.moveFocus(false)
    }
    Keys.onTabPressed: function(event) { event.accepted = FocusContainment.move(bar.Window.window, bar, true) }
    Keys.onBacktabPressed: function(event) { event.accepted = FocusContainment.move(bar.Window.window, bar, false) }

    Settings {
        id: prefs
        category: "subtitleStyle"
        property real scale: 1.0
        property string textColor: "#FFFFFF"
        property real outlineSize: 2.0
        property string outlineColor: "#000000"
        property int position: 94
        property string assOverride: "scale"
        // Only push these to mpv once the user actually changes something — otherwise a
        // fresh stream must render exactly as stock mpv would (don't silently nudge sub-pos
        // or override embedded ASS before the appearance bar is ever opened).
        property bool customized: false
    }

    function clamp(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    function round1(v) {
        return Math.round(v * 10) / 10;
    }

    // Called from the user's control handlers → marks the prefs as customized so they
    // persist AND get reapplied on later files/launches.
    function setOption(key, value) {
        prefs.customized = true;
        if (bar.player && bar.player.setSubOption)
            bar.player.setSubOption(key, value);
    }

    // Reapply saved styling — only invoked once the user has customized (calls the player
    // directly so it doesn't itself flip the customized flag).
    function applyAll() {
        if (!bar.player || !bar.player.setSubOption)
            return;
        bar.player.setSubOption("sub-scale", prefs.scale);
        bar.player.setSubOption("sub-color", prefs.textColor);
        bar.player.setSubOption("sub-border-size", prefs.outlineSize);
        bar.player.setSubOption("sub-border-color", prefs.outlineColor);
        bar.player.setSubOption("sub-pos", prefs.position);
        bar.player.setSubOption("sub-ass-override", prefs.assOverride);
    }

    Component.onCompleted: if (prefs.customized) applyAll()
    onPlayerChanged: if (prefs.customized) applyAll()

    Rectangle {
        id: panel
        anchors.top: parent.top
        anchors.topMargin: 68
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 56, 760)
        height: 56
        radius: 14
        color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)

        Theme { id: theme }

        // Absorb background clicks so the bar body never dismisses itself (parity spec F2).
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }

        Row {
            anchors.centerIn: parent
            spacing: 8

            Cluster {
                label: "SIZE"
                valueText: Number(prefs.scale).toFixed(1)
                onMinus: {
                    prefs.scale = bar.round1(bar.clamp(prefs.scale - 0.1, 0.5, 2.0));
                    bar.setOption("sub-scale", prefs.scale);
                }
                onPlus: {
                    prefs.scale = bar.round1(bar.clamp(prefs.scale + 0.1, 0.5, 2.0));
                    bar.setOption("sub-scale", prefs.scale);
                }
            }

            Swatches {
                label: "TEXT"
                selected: prefs.textColor
                colors: ["#FFFFFF", "#F0C44A", "#EDE7D1", "#9FE7FF"]
                onPicked: function(color) {
                    prefs.textColor = color;
                    bar.setOption("sub-color", color);
                }
            }

            Cluster {
                label: "OUTLINE"
                valueText: Number(prefs.outlineSize).toFixed(1)
                onMinus: {
                    prefs.outlineSize = bar.round1(bar.clamp(prefs.outlineSize - 0.5, 0, 6));
                    bar.setOption("sub-border-size", prefs.outlineSize);
                }
                onPlus: {
                    prefs.outlineSize = bar.round1(bar.clamp(prefs.outlineSize + 0.5, 0, 6));
                    bar.setOption("sub-border-size", prefs.outlineSize);
                }
            }

            Swatches {
                label: "EDGE"
                selected: prefs.outlineColor
                colors: ["#000000", "#2B1D10", "#20314A", "#FFFFFF"]
                onPicked: function(color) {
                    prefs.outlineColor = color;
                    bar.setOption("sub-border-color", color);
                }
            }

            Cluster {
                label: "POS"
                valueText: prefs.position
                onMinus: {
                    prefs.position = Math.round(bar.clamp(prefs.position - 3, 0, 100));
                    bar.setOption("sub-pos", prefs.position);
                }
                onPlus: {
                    prefs.position = Math.round(bar.clamp(prefs.position + 3, 0, 100));
                    bar.setOption("sub-pos", prefs.position);
                }
            }

            SelectCluster {
                label: "ASS"
                valueText: prefs.assOverride
                onCycle: {
                    if (prefs.assOverride === "no")
                        prefs.assOverride = "scale";
                    else if (prefs.assOverride === "scale")
                        prefs.assOverride = "force";
                    else
                        prefs.assOverride = "no";
                    bar.setOption("sub-ass-override", prefs.assOverride);
                }
            }
        }
    }

    component Cluster: Rectangle {
        id: cluster
        property string label: ""
        property string valueText: ""
        signal minus()
        signal plus()
        width: 110
        height: 44
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.07)
        Row {
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cluster.label
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 10
                font.weight: Font.Bold
            }
            MiniButton { text: "-"; onClicked: cluster.minus() }
            Text {
                width: 26
                anchors.verticalCenter: parent.verticalCenter
                text: cluster.valueText
                color: theme.ink
                font.family: theme.hud; font.features: ({ "tnum": 1 })
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            MiniButton { text: "+"; onClicked: cluster.plus() }
        }
    }

    component SelectCluster: Rectangle {
        id: cluster
        property string label: ""
        property string valueText: ""
        signal cycle()
        width: 112
        height: 44
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.07)
        Row {
            anchors.centerIn: parent
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cluster.label
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 10
                font.weight: Font.Bold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cluster.valueText
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: cluster.cycle()
        }
        KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: cluster.label + " " + cluster.valueText; onTriggered: cluster.cycle() }
    }

    component Swatches: Rectangle {
        id: swatches
        property string label: ""
        property string selected: ""
        property var colors: []
        property int keyboardIndex: Math.max(0, colors.map(function(c) { return String(c).toLowerCase() }).indexOf(String(selected).toLowerCase()))
        signal picked(string color)
        focusPolicy: bar.open && colors.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                var next = keyboardIndex + (event.key === Qt.Key_Left ? -1 : 1)
                if (next >= 0 && next < colors.length) { keyboardIndex = next; event.accepted = true }
            } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
            else if (event.key === Qt.Key_End) { keyboardIndex = colors.length - 1; event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { swatches.picked(String(colors[keyboardIndex])); event.accepted = true }
        }
        width: 126
        height: 44
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.07)
        Row {
            anchors.centerIn: parent
            spacing: 7
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: swatches.label
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 10
                font.weight: Font.Bold
            }
            Repeater {
                model: swatches.colors
                delegate: Rectangle {
                    id: dot
                    required property int index
                    required property string modelData
                    width: 18
                    height: 18
                    radius: 9
                    color: modelData
                    border.width: (swatches.activeFocus && swatches.keyboardIndex === dot.index) || swatches.selected.toLowerCase() === modelData.toLowerCase() ? 2 : 1
                    border.color: (swatches.activeFocus && swatches.keyboardIndex === dot.index) || swatches.selected.toLowerCase() === modelData.toLowerCase() ? theme.gold : Qt.rgba(0, 0, 0, 0.45)
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: swatches.picked(dot.modelData)
                    }
                }
            }
        }
    }

    component MiniButton: Rectangle {
        id: button
        property string text: ""
        signal clicked()
        width: 22
        height: 24
        radius: 6
        color: miniMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        Text {
            anchors.centerIn: parent
            text: button.text
            color: theme.ink
            font.family: theme.hud; font.features: ({ "tnum": 1 })
            font.pixelSize: 13
            font.weight: Font.Bold
        }
        MouseArea {
            id: miniMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
        KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: button.text; onTriggered: button.clicked() }
    }
}
