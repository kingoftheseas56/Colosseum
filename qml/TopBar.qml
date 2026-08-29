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
    // Retained world pages stay instantiated for state preservation, but hidden bars must not
    // keep their live clock timer waking the GUI every second.
    property bool lifecycleActive: true
    property string clock: "8:29"
    property string ampm: "PM"
    property string date: "Wednesday, June 24"
    property var accountController:
        typeof AccountController !== "undefined" ? AccountController : null
    readonly property bool accountPresent: accountController
        && (accountController.mode === "signedIn"
            || accountController.mode === "offline")

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal accountClicked()
    signal fullscreenClicked()
    signal minimizeClicked()
    signal powerClicked()
    signal updateClicked()

    // Update availability flags (home only): drive the silver badge on the home
    // Update glyph the same way the taskbar badge pulses on updateUnseen. Bound
    // from Main.qml's Updates singleton; inert on worlds (the glyph is hidden).
    property bool updateAvailable: false
    property bool updateUnseen: false
    property bool reducedMotion: false

    // Glyph state only — the toggle ACTION stays with the host (Main.qml drives
    // WindowMode.toggleShellMode, the same authority as A5's F11 door). Reading
    // the global context property here keeps both TopBar instances honest
    // without threading state through two hosts.
    readonly property bool shellWindowed:
        typeof WindowMode !== "undefined" && WindowMode.shellWindowed

    implicitHeight: 56

    Theme { id: theme }

    // ---- live wall clock: the bar owns its own time, so the home page AND every
    //      world / reader / player page that reuses this chrome show the real minute
    //      instead of the build-day placeholder above. Ticks each second so the minute
    //      rolls over promptly; identical string sets are no-op notifies. ----
    function refreshClock() {
        const now = new Date()
        const h = now.getHours()
        const m = now.getMinutes()
        let h12 = h % 12; if (h12 === 0) h12 = 12
        bar.clock = h12 + ":" + (m < 10 ? "0" + m : m)
        bar.ampm = h < 12 ? "AM" : "PM"
        bar.date = Qt.formatDate(now, "dddd, MMMM d")
    }
    Timer {
        interval: 1000; running: bar.lifecycleActive; repeat: true; triggeredOnStart: true
        onTriggered: bar.refreshClock()
    }

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
        // Automation identity (Lanista): the pills are plain Items made clickable by a child
        // MouseArea — the one shape ui-snapshot's superclass walk documents it cannot detect —
        // so a stable name is what makes mode navigation drivable at all.
        objectName: "modePill_" + label
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
        BackAction {
            // world-root variant: destination label, dim→bright hover (never gold up here)
            visible: bar.activeMedium !== ""
            label: "Home"
            labelSize: 14
            idleColor: theme.inkDim
            hoverColor: theme.ink
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: bar.homeRequested()
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
    // Universal search is retired on home; the slot belongs to the Update glyph
    // there (the release chronicle entry, with a silver availability badge).
    // Worlds keep search — it is wired and functional in WorldPage.qml. The two
    // are gated on activeMedium, the same home/world discriminator BackAction
    // uses above, so only one is ever present in the slot.
    Row {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        spacing: 20
        // Search — worlds only.
        SysIcon {
            objectName: "topBarSearch"
            source: "../assets/icons/search.svg"
            onClicked: bar.searchClicked()
            visible: bar.activeMedium !== ""
        }
        // Update — home only. Takes search's throne; the silver badge signals an
        // available release (pulse on unseen, steady once seen).
        Item {
            width: 22; height: 22
            visible: bar.activeMedium === ""
            opacity: updateMa.containsMouse ? 1.0 : 0.92
            objectName: "colosseumTopbarUpdateButton"
            Accessible.role: Accessible.Button
            Accessible.name: bar.updateAvailable ? "Update available" : "Updates"
            Image {
                anchors.fill: parent
                source: "../assets/icons/update.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                objectName: "colosseumTopbarUpdateBadge"
                visible: bar.updateAvailable
                anchors.top: parent.top; anchors.right: parent.right
                anchors.topMargin: -2; anchors.rightMargin: -2
                width: 9; height: 9; radius: 4.5
                color: "#f2f2ef"; border.width: 1; border.color: "#15151a"
                SequentialAnimation on scale {
                    running: bar.updateUnseen && bar.updateAvailable && !bar.reducedMotion
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 1.22; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                    PauseAnimation { duration: 900 }
                }
            }
            MouseArea {
                id: updateMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: bar.updateClicked()
            }
        }
        // Account identity (Bundle 8C first-light): gold-ringed initial when
        // signed in, quiet outline when not. Sits beside Update + Wallpapers.
        Item {
            width: 22; height: 22
            objectName: "colosseumTopbarAccountButton"
            opacity: accountMa.containsMouse ? 1.0 : 0.92
            Accessible.role: Accessible.Button
            Accessible.name: bar.accountPresent
                ? ("Account: " + bar.accountController.username)
                : "Account"
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: bar.accountPresent ? Qt.rgba(0.94, 0.77, 0.29, 0.16) : "transparent"
                border.width: 1.5
                border.color: bar.accountPresent
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.8)
                    : Qt.rgba(1, 1, 1, 0.38)
                Item {
                    anchors.centerIn: parent
                    width: 14; height: 14
                    visible: !bar.accountPresent
                    opacity: 0.82
                    Rectangle {
                        width: 5; height: 5; radius: 2.5
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 1
                        color: "#ffffff"
                    }
                    Rectangle {
                        width: 10; height: 6; radius: 3
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 7
                        color: "#ffffff"
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: bar.accountPresent
                    text: {
                        const who = bar.accountController ? bar.accountController.username : "";
                        return who.length > 0 ? who.charAt(0).toUpperCase() : "?";
                    }
                    color: "#f0df9a"
                    font.family: "Inter"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            MouseArea {
                id: accountMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.accountClicked()
            }
        }
        // Automation identity (Lanista): the wallpaper control is a production shell
        // action, so capture journeys address this button directly instead of replacing
        // the top bar with a harness-only presentation.
        SysIcon {
            objectName: "topBarWallpaperButton"
            source: "../assets/icons/settings.svg"
            onClicked: bar.wallpaperClicked()
        }
        SysIcon { source: "../assets/icons/minimize.svg"; onClicked: bar.minimizeClicked() }
        // Fullscreen toggle (Hemanth 2026-07-16, supersedes the old never-☐ topbar
        // rule): glyph shows the ACTION — expand while windowed, contract while
        // fullscreen. Drives the same shell flip as the F11 developer door.
        SysIcon {
            source: bar.shellWindowed ? "../assets/icons/fullscreen.svg"
                                      : "../assets/icons/fullscreen-exit.svg"
            onClicked: bar.fullscreenClicked()
        }
        SysIcon { source: "../assets/icons/power.svg";    onClicked: bar.powerClicked() }
    }
}
