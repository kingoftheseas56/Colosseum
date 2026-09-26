pragma ComponentBehavior: Bound
// TitlePageBar — the one top bar every title page wears (world feel design, 2026-09-24,
// docs/superpowers/specs/2026-09-24-colosseum-world-feel-design.md, "The shared frame").
//
// Left: a Back pill that names where Back goes. Centre: the Tankoban · Biblio · Theatre
// pills — Hemanth's one-app rule keeps them on every page except the readers and the
// player. Right: search and ONE system menu (minimize / fullscreen / quit) so the window
// controls stop sitting in the focus path, and Quit asks before it closes the app.
//
// The bar is transparent over a hero and turns solid once the page scrolls (`solid`).
// It only emits intent; the host page (and Main.qml above it) decides what happens.
import QtQuick

Item {
    id: bar
    objectName: "titlePageBar"

    property string world: ""                 // "Theatre" | "Tankoban" | "Biblio" — the active pill
    property string backLabel: "Back"         // destination-aware ("Theatre", "Search", "Magic")
    property string backObjectName: ""        // keeps each page's historical Back automation name
    property bool solid: false                // true once the page has scrolled under the bar
    property bool searchVisible: true
    readonly property bool menuOpen: systemMenu.visible
    readonly property Item backItem: backAction

    signal backRequested()
    signal worldRequested(string world)
    signal searchRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()

    height: 76

    function openMenu() {
        bar.quitArmed = false
        systemMenu.visible = true
        Qt.callLater(function() { if (bar.menuMinimize) bar.menuMinimize.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function closeMenu(restoreFocus) {
        systemMenu.visible = false
        bar.quitArmed = false
        if (restoreFocus !== false)
            Qt.callLater(function() { menuButtonAction.forceActiveFocus(Qt.PopupFocusReason) })
    }
    property bool quitArmed: false

    Theme { id: theme }

    component MenuRow: Item {
        id: mrow
        property string label: ""
        property bool danger: false
        property alias action: rowAction
        signal chosen()
        width: parent ? parent.width : 0
        height: 40
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 6; anchors.rightMargin: 6
            radius: 9
            color: rowAction.interactionActive ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: mrow.label
            color: mrow.danger ? "#f0b4a6" : theme.ink
            font.family: theme.ui; font.pixelSize: 14
        }
        KeyboardAction {
            id: rowAction
            anchors.fill: parent
            anchors.leftMargin: 6; anchors.rightMargin: 6
            accessibleName: mrow.label
            focusRadius: 9
            focusColor: bar.focusGold
            onTriggered: mrow.chosen()
            Keys.onEscapePressed: bar.closeMenu(true)
        }
    }
    readonly property color focusGold: Qt.rgba(0.94, 0.77, 0.29, 1.0)

    // ---- ground: a scrim over the hero, a solid band once scrolled ----
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.024, 0.027, 0.043, 0.985)
        opacity: bar.solid ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: theme.edge }
    }
    Rectangle {
        anchors.fill: parent
        opacity: bar.solid ? 0.0 : 1.0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }
    MouseArea { anchors.fill: parent }          // the bar never lets a click fall through to the page

    // ---- left: Back pill naming its destination ----
    BackAction {
        id: backAction
        objectName: bar.backObjectName
        x: theme.margin
        anchors.verticalCenter: parent.verticalCenter
        label: bar.backLabel
        labelSize: 15
        idleColor: theme.ink
        hoverColor: theme.gold
        onTriggered: bar.backRequested()
    }

    // ---- centre: the world pills (compact) ----
    Rectangle {
        id: pillCapsule
        anchors.centerIn: parent
        height: 42
        width: pillRow.implicitWidth + 12
        radius: 21
        color: Qt.rgba(1, 1, 1, 0.07)
        border.width: 1
        border.color: theme.edge
        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: ["Tankoban", "Biblio", "Theatre"]
                delegate: Item {
                    id: pill
                    required property string modelData
                    readonly property bool active: bar.world === pill.modelData
                    width: pillText.implicitWidth + 30
                    height: 32
                    Rectangle {
                        anchors.fill: parent
                        radius: 16
                        color: pill.active ? theme.gold
                             : (pillAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    }
                    Text {
                        id: pillText
                        anchors.centerIn: parent
                        text: pill.modelData
                        color: pill.active ? "#1a1408" : (pillAction.interactionActive ? theme.ink : theme.inkDim)
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: pill.active ? Font.DemiBold : Font.Medium
                    }
                    KeyboardAction {
                        id: pillAction
                        objectName: "titleBarPill_" + pill.modelData
                        anchors.fill: parent
                        accessibleName: pill.modelData
                        focusRadius: 16
                        focusColor: bar.focusGold
                        onTriggered: bar.worldRequested(pill.modelData)
                    }
                }
            }
        }
    }

    // ---- right: search + the one system menu ----
    Row {
        id: rightCluster
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Item {
            visible: bar.searchVisible
            width: 40; height: 40
            Rectangle { anchors.fill: parent; radius: 20; color: searchAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent" }
            Image {
                anchors.centerIn: parent
                width: 21; height: 21
                source: "../assets/icons/search.svg"
                sourceSize.width: 42; sourceSize.height: 42
                opacity: searchAction.interactionActive ? 1.0 : 0.8
            }
            KeyboardAction {
                id: searchAction
                objectName: "titleBarSearch"
                anchors.fill: parent
                accessibleName: "Search"
                focusRadius: 20
                focusColor: bar.focusGold
                onTriggered: bar.searchRequested()
            }
        }
        Item {
            width: 40; height: 40
            Rectangle { anchors.fill: parent; radius: 20
                        color: (menuButtonAction.interactionActive || bar.menuOpen) ? Qt.rgba(1, 1, 1, 0.12) : "transparent" }
            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 3
                    Rectangle { width: 4; height: 4; radius: 2; color: theme.ink; opacity: 0.85 }
                }
            }
            KeyboardAction {
                id: menuButtonAction
                objectName: "titleBarSystemMenu"
                anchors.fill: parent
                accessibleName: "Window and app"
                focusRadius: 20
                focusColor: bar.focusGold
                onTriggered: bar.menuOpen ? bar.closeMenu(true) : bar.openMenu()
            }
        }
    }

    // ---- the system menu (same window, floats under the bar) ----
    Rectangle {
        id: systemMenu
        objectName: "titleBarSystemMenuPanel"
        visible: false
        z: 50
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: bar.height - 6
        width: 240
        height: menuCol.implicitHeight + 16
        radius: 14
        color: Qt.rgba(0.043, 0.051, 0.075, 0.98)
        border.width: 1
        border.color: theme.edge
        Keys.onEscapePressed: bar.closeMenu(true)

        Column {
            id: menuCol
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.topMargin: 8
            Column {
                visible: !bar.quitArmed
                width: parent.width
                MenuRow {
                    id: menuMinimizeRow
                    label: "Minimize"
                    onChosen: { bar.closeMenu(false); bar.minimizeRequested() }
                    Component.onCompleted: bar.menuMinimize = action
                }
                MenuRow {
                    label: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "Fullscreen" : "Exit fullscreen"
                    onChosen: { bar.closeMenu(false); bar.fullscreenRequested() }
                }
                Rectangle { width: parent.width - 24; x: 12; height: 1; color: theme.edge }
                MenuRow {
                    label: "Quit Colosseum…"
                    danger: true
                    onChosen: {
                        bar.quitArmed = true
                        Qt.callLater(function() { bar.quitNo.forceActiveFocus(Qt.PopupFocusReason) })
                    }
                }
            }
            Column {
                visible: bar.quitArmed
                width: parent.width
                spacing: 6
                Text {
                    x: 18
                    width: parent.width - 36
                    text: "Quit Colosseum?"
                    color: theme.ink
                    font.family: theme.display; font.pixelSize: 17
                }
                MenuRow {
                    id: quitNoRow
                    label: "Keep watching"
                    onChosen: bar.closeMenu(true)
                    Component.onCompleted: bar.quitNo = action
                }
                MenuRow {
                    objectName: "titleBarQuitConfirm"
                    label: "Quit"
                    danger: true
                    onChosen: { bar.closeMenu(false); bar.quitRequested() }
                }
            }
        }
    }
    property Item menuMinimize: null
    property Item quitNo: null
}
