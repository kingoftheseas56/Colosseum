// BackAction — THE back button. One canonical vector chevron, worn three ways; no font-glyph
// arrows ("<", "‹") anywhere else in the app. Spec: haven docs/superpowers/specs/
// 2026-07-05-colosseum-back-navigation-design.md · ratified mock agents/colosseum-backaction-variants-mock.html.
//
//   variant "plain"     chevron + label, no container   (detail pages, ‹ Home, search headers)
//   variant "capsule"   42×34 dark pill, icon-only      (genre / universe / index headers)
//   variant "immersive" icon-only, chrome-mounted       (reader HUD; player keeps its own Canvas
//                                                        icon system but shares this stroke grammar)
//
// The hit target is always ≥44px (invisible margins), icon-only variants must pass `tip`.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string variant: "plain"          // "plain" | "capsule" | "immersive"
    property string label: "Back"             // plain only; destination-aware ("Home", "Search")
    property color idleColor: theme.ink
    property color hoverColor: theme.gold     // Biblio passes white; TopBar Home passes ink
    property int labelSize: 15
    property string tip: ""                   // tooltip — required when icon-only
    // Opt-in drop shadow under the chevron and label. Default OFF, so every existing caller
    // renders exactly as before. The reader HUD needs it: its back control sits directly on
    // bright manga artwork with no chrome behind it, where flat white on white vanishes —
    // Hemanth's legibility ruling. Without this the reader could not adopt the shared
    // component at all, which is why its back control was hand-built and out of law.
    property bool raisedLabel: false
    readonly property bool iconOnly: variant !== "plain"
    readonly property bool hovered: ma.containsMouse
    signal triggered()

    Theme { id: theme }

    readonly property color _c: ma.containsMouse ? hoverColor : idleColor
    readonly property int _chev: variant === "plain" ? 20 : (variant === "capsule" ? 18 : 24)

    implicitWidth: variant === "capsule" ? 42
                 : variant === "immersive" ? 34
                 : chevron.width + (labelText.visible ? 9 + labelText.implicitWidth : 0) + 8
    implicitHeight: variant === "capsule" ? 34 : 34
    width: implicitWidth
    height: implicitHeight

    // capsule container (the familiar dark pill over busy art)
    Rectangle {
        visible: root.variant === "capsule"
        anchors.fill: parent
        radius: height / 2
        color: ma.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.40)
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: root.iconOnly ? parent.horizontalCenter : undefined
        anchors.left: root.iconOnly ? undefined : parent.left
        anchors.leftMargin: root.iconOnly ? 0 : 2
        spacing: 9

        // the canonical chevron — vector, currentColor, round caps (never a font glyph).
        // Wrapped in a plain Item (not directly in the Row) so the raisedLabel shadow Shape
        // below can sit in the same layout cell, offset by 1px, without the Row's positioner
        // fighting over two siblings' x/y — the Row only ever sees one child here, unchanged.
        Item {
            id: chevron
            width: root._chev; height: root._chev
            anchors.verticalCenter: parent.verticalCenter

            // raisedLabel drop shadow for the chevron — a second Shape, offset 1px, stroked in
            // translucent black, painted BEHIND the real chevron below. Shape/ShapePath has no
            // styleColor (that's a Text-only property), so a duplicate offset stroke is the
            // only way to fake the same drop shadow the label gets. Default invisible, opt-in.
            Shape {
                id: chevronShadow
                visible: root.raisedLabel
                x: 1; y: 1
                width: root._chev; height: root._chev
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Qt.rgba(0, 0, 0, 0.5)
                    strokeWidth: 2.5 * root._chev / 24
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: 14.5 * root._chev / 24; startY: 5.5 * root._chev / 24
                    PathLine { x: 8.0 * root._chev / 24; y: 12.0 * root._chev / 24 }
                    PathLine { x: 14.5 * root._chev / 24; y: 18.5 * root._chev / 24 }
                }
            }

            Shape {
                id: chevronShape
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: root._c
                    strokeWidth: 2.5 * root._chev / 24
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: 14.5 * root._chev / 24; startY: 5.5 * root._chev / 24
                    PathLine { x: 8.0 * root._chev / 24; y: 12.0 * root._chev / 24 }
                    PathLine { x: 14.5 * root._chev / 24; y: 18.5 * root._chev / 24 }
                }
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }

        Text {
            id: labelText
            visible: !root.iconOnly && root.label.length > 0
            text: root.label
            color: root._c
            font.family: theme.ui
            font.pixelSize: root.labelSize
            style: root.raisedLabel ? Text.Raised : Text.Normal
            styleColor: Qt.rgba(0, 0, 0, 0.5)
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        // stretch the interactive target to ≥44px in both axes without moving the visuals
        anchors.margins: -Math.max(0, Math.ceil((44 - Math.min(root.width, root.height)) / 2))
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered()
    }

    // hand-rolled tooltip (no Controls dependency) — icon-only variants announce themselves
    Rectangle {
        visible: root.tip.length > 0 && ma.containsMouse && tipDelay.done
        anchors.top: parent.bottom; anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: tipText.implicitWidth + 18; height: 24; radius: 6
        color: Qt.rgba(0.04, 0.04, 0.05, 0.92)
        border.width: 1; border.color: theme.edge
        z: 1000
        Text { id: tipText; anchors.centerIn: parent; text: root.tip
            color: theme.ink; font.family: theme.ui; font.pixelSize: 11 }
    }
    Timer {
        id: tipDelay
        property bool done: false
        interval: 550; running: ma.containsMouse && root.tip.length > 0
        onTriggered: done = true
    }
    Connections { target: ma; function onContainsMouseChanged() { if (!ma.containsMouse) tipDelay.done = false } }

    Accessible.role: Accessible.Button
    Accessible.name: root.tip.length > 0 ? root.tip : root.label
    Accessible.onPressAction: root.triggered()
}
