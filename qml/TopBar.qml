// TopBar — the shared Colosseum shell chrome: clock/date · library pills · system icons.
// ONE source for the top bar across the home AND every world page.
//   activeMedium == ""   → HOME: no pill is selected (the no-selection rule).
//   activeMedium == "X"  → WORLD: pill X carries the gold selected accent, and a "‹ Home"
//                          affordance appears at the left.
// Emits intent signals; the host (home / world) decides what navigation happens.

import QtQuick

Item {
    id: bar

    required property Item backdrop          // wallpaper to composite the pills' glass over
    property string activeMedium: ""         // "" = home / no selection
    property string clock: "8:29"
    property string ampm: "PM"
    property string date: "Wednesday, June 24"

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal minimizeClicked()
    signal powerClicked()

    implicitHeight: 56

    Theme { id: theme }

    // ---- inline: a system icon button (Image renders the local SVG reliably; tint via opacity) ----
    component SysIcon: Item {
        id: sysRoot
        property url source
        signal clicked()
        width: 22; height: 22
        Image {
            anchors.fill: parent
            source: sysRoot.source
            sourceSize.width: 22; sourceSize.height: 22
            fillMode: Image.PreserveAspectFit
            opacity: sma.containsMouse ? 1.0 : 0.72
        }
        MouseArea {
            id: sma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: sysRoot.clicked()
        }
    }

    // ---- inline: a library pill (selected when its label == activeMedium).
    //      Clean centered TEXT — icons return later with proper active/inactive tinting.
    //      comingSoon → a placeholder mode (e.g. Vinyl): muted "SOON" tag, not navigable. ----
    component Pill: Item {
        id: pill
        property string label
        property url icon
        property bool comingSoon: false
        readonly property bool active: bar.activeMedium === pill.label
        readonly property bool hot: pma.containsMouse && !pill.comingSoon
        implicitWidth: pillContent.implicitWidth + 34
        implicitHeight: 34

        Rectangle {
            anchors.fill: parent; radius: 999
            color: pill.active ? theme.gold : (pill.hot ? theme.glassHi : "transparent")
            border.width: 1
            border.color: pill.active ? "transparent" : (pill.hot ? theme.edge : "transparent")
        }
        Row {
            id: pillContent
            anchors.centerIn: parent
            spacing: 6
            Text {
                text: pill.label
                color: pill.active ? "#1a1408" : (pma.containsMouse && !pill.comingSoon ? theme.ink : theme.inkDim)
                opacity: pill.comingSoon ? 0.6 : 1.0
                font.family: theme.ui; font.pixelSize: 14
                font.weight: pill.active ? Font.DemiBold : Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {   // "SOON" marker — placeholder mode, no world yet
                visible: pill.comingSoon
                anchors.verticalCenter: parent.verticalCenter
                radius: 4; height: 15; width: soonText.implicitWidth + 10
                color: Qt.rgba(1,1,1,0.10)
                Text {
                    id: soonText; anchors.centerIn: parent; text: "SOON"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 0.8
                }
            }
        }
        MouseArea {
            id: pma; anchors.fill: parent
            hoverEnabled: !pill.comingSoon
            cursorShape: pill.comingSoon ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: if (!pill.comingSoon) bar.mediumSelected(pill.label)
        }
    }

    // ---- left: "‹ Home" (world only) + clock/date ----
    Row {
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        spacing: 18
        Item {
            visible: bar.activeMedium !== ""
            width: visible ? homeRow.implicitWidth : 0
            height: 34
            anchors.verticalCenter: parent.verticalCenter
            Row {
                id: homeRow; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                Text { text: "‹"; color: hma.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Home"; color: hma.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: hma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: bar.homeRequested()
            }
        }
        Column {
            spacing: 3
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 5
                Text { text: bar.clock; color: theme.ink; font.family: theme.display; font.pixelSize: 32 }
                Text { text: bar.ampm; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4 }
            }
            Text { text: bar.date; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
        }
    }

    // ---- center: library pills in a glass capsule ----
    Glass {
        backdrop: bar.backdrop
        anchors.centerIn: parent
        radius: 999
        width: pillsRow.implicitWidth + 14; height: 46
        Row {
            id: pillsRow
            anchors.centerIn: parent
            spacing: 4
            // The four modes (Hemanth-locked 2026-06-24). Tankoban = comics+manga · Biblio = books ·
            // Theatre = movies/video · Vinyl = music (placeholder, no world yet).
            Pill { label: "Tankoban" }
            Pill { label: "Biblio" }
            Pill { label: "Theatre" }
            Pill { label: "Vinyl"; comingSoon: true }
        }
    }

    // ---- right: system icons ----
    Row {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        spacing: 20
        SysIcon { source: "../assets/icons/search.svg";   onClicked: bar.searchClicked() }
        SysIcon { source: "../assets/icons/star.svg";     onClicked: bar.wallpaperClicked() }
        SysIcon { source: "../assets/icons/minimize.svg"; onClicked: bar.minimizeClicked() }
        SysIcon { source: "../assets/icons/power.svg";    onClicked: bar.powerClicked() }
    }
}
