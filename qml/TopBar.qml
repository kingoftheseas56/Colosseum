// TopBar — the shared Colosseum shell chrome: clock/date · library pills · system icons.
// ONE source for the top bar across the home AND every world page.
//   activeMedium == ""   → HOME: no pill is selected (the no-selection rule).
//   activeMedium == "X"  → WORLD: pill X carries the gold selected accent, and a "‹ Home"
//                          affordance appears at the left.
// Emits intent signals; the host (home / world) decides what navigation happens.

import QtQuick
import QtQuick.Window

Item {
    id: bar

    required property Item backdrop          // wallpaper to composite the pills' glass over
    property string activeMedium: ""         // "" = home / no selection
    // Retained world pages stay instantiated for state preservation, but hidden bars must not
    // keep their live clock timer waking the GUI every second.
    property bool lifecycleActive: true
    // Android keeps the shared shell, but desktop window chrome has no meaning there.
    // This is writable so the viewport harness can exercise Android geometry on desktop CI.
    property bool androidHost: Qt.platform.os === "android"
    AdaptiveLayout { id: adaptive; viewportWidth: bar.width }
    readonly property bool compactLayout: adaptive.compactChrome
    readonly property string layoutClass: adaptive.layoutClass
    readonly property bool desktopWindowControlsVisible: !bar.androidHost
    property string clock: "8:29"
    property string ampm: "PM"
    property string date: "Wednesday, June 24"
    property var accountController:
        typeof AccountController !== "undefined" ? AccountController : null
    readonly property bool accountPresent: accountController
        && (accountController.mode === "signedIn"
            || accountController.mode === "offline")
    readonly property bool localDevice: accountController
        && accountController.mode === "localOnly"
    readonly property bool televisionMode: {
        const w = bar.Window.window
        return !!(w && w["televisionMode"] === true)
    }

    function isInside(item) {
        var p = item
        while (p) {
            if (p === bar) return true
            p = p.parent
        }
        return false
    }
    function focusFromActive(forward, stayInside) {
        const w = bar.Window.window
        const from = w ? w.activeFocusItem : null
        if (!from || !bar.isInside(from)) return false
        var target = from.nextItemInFocusChain(forward)
        var guard = 0
        while (target && target !== from && guard++ < 48) {
            if (target.visible && target.enabled && target.activeFocusOnTab) {
                if (stayInside && !bar.isInside(target)) return false
                target.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
                return true
            }
            target = target.nextItemInFocusChain(forward)
        }
        return false
    }
    function moveHorizontalFocus(forward) { return bar.focusFromActive(forward, true) }
    function moveVerticalFocus(forward) { return bar.focusFromActive(forward, false) }
    function focusFirst() {
        var target = bar.nextItemInFocusChain(true)
        var guard = 0
        while (target && target !== bar && guard++ < 32) {
            if (bar.isInside(target) && target.visible && target.enabled && target.activeFocusOnTab) {
                target.forceActiveFocus(Qt.TabFocusReason)
                return true
            }
            target = target.nextItemInFocusChain(true)
        }
        return false
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => {
        if (!bar.televisionMode) return
        if (event.key === Qt.Key_Left) event.accepted = bar.moveHorizontalFocus(false)
        else if (event.key === Qt.Key_Right) event.accepted = bar.moveHorizontalFocus(true)
        else if (event.key === Qt.Key_Up) event.accepted = bar.moveVerticalFocus(false)
        else if (event.key === Qt.Key_Down) event.accepted = bar.moveVerticalFocus(true)
    }

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal accountClicked(real anchorRight, real anchorBottom)
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

    implicitHeight: bar.televisionMode ? 68 : adaptive.topBarHeight

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
        property string accessibleName: ""
        signal clicked()
        width: bar.televisionMode ? 30 : 22; height: width
        Image {
            anchors.fill: parent
            source: sysRoot.source
            sourceSize.width: bar.televisionMode ? 30 : 22; sourceSize.height: sourceSize.width
            fillMode: Image.PreserveAspectFit
            opacity: input.interactionActive ? 1.0 : 0.72
        }
        KeyboardAction {
            id: input
            anchors.fill: parent
            accessibleName: sysRoot.accessibleName
            focusRadius: bar.televisionMode ? 9 : 6
            onTriggered: sysRoot.clicked()
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
        readonly property bool hot: pillInput.interactionActive && !pill.comingSoon
        implicitWidth: pillContent.implicitWidth + (bar.televisionMode ? 44 : (bar.compactLayout ? 18 : 34))
        implicitHeight: bar.televisionMode ? 42 : (bar.compactLayout ? 32 : 34)

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
                color: pill.active ? "#1a1408" : (pillInput.interactionActive && !pill.comingSoon ? theme.ink : theme.inkDim)
                opacity: pill.comingSoon ? 0.6 : 1.0
                font.family: theme.ui; font.pixelSize: bar.televisionMode ? 16 : (bar.compactLayout ? 12 : 14)
                font.weight: pill.active ? Font.DemiBold : Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {   // "SOON" marker — placeholder mode, no world yet
                visible: pill.comingSoon && !bar.compactLayout
                anchors.verticalCenter: parent.verticalCenter
                radius: 4; height: 15; width: soonText.implicitWidth + 10
                color: Qt.rgba(1,1,1,0.10)
                Text {
                    id: soonText; anchors.centerIn: parent; text: "SOON"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 0.8
                }
            }
        }
        KeyboardAction {
            id: pillInput
            anchors.fill: parent
            enabled: !pill.comingSoon
            accessibleName: pill.label
            focusRadius: pill.height / 2
            onTriggered: bar.mediumSelected(pill.label)
        }
    }

    // ---- left: "‹ Home" (world only) + clock/date ----
    Row {
        id: leftCluster
        x: 0
        y: bar.compactLayout ? 7 : Math.round((bar.height - height) / 2)
        spacing: bar.compactLayout ? 10 : 18
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
                Text { text: bar.clock; color: theme.ink; font.family: theme.display; font.pixelSize: bar.televisionMode ? 36 : (bar.compactLayout ? 19 : 32) }
                Text { text: bar.ampm; color: theme.inkDim; font.family: theme.ui; font.pixelSize: bar.televisionMode ? 18 : (bar.compactLayout ? 10 : 16)
                    anchors.bottom: parent.bottom; anchors.bottomMargin: bar.televisionMode ? 4 : (bar.compactLayout ? 2 : 4) }
            }
            Text { visible: bar.televisionMode || !bar.compactLayout; text: bar.date; color: theme.inkDim; font.family: theme.ui; font.pixelSize: bar.televisionMode ? 15 : 13 }
        }
    }

    // ---- center: library pills in a glass capsule ----
    Glass {
        backdrop: bar.backdrop
        x: Math.round((bar.width - width) / 2)
        y: bar.compactLayout ? bar.height - height : Math.round((bar.height - height) / 2)
        radius: 999
        width: Math.min(bar.width, pillsRow.implicitWidth + (bar.televisionMode ? 18 : (bar.compactLayout ? 8 : 14)))
        height: bar.televisionMode ? 56 : (bar.compactLayout ? 42 : 46)
        Row {
            id: pillsRow
            anchors.centerIn: parent
            spacing: bar.compactLayout ? 2 : 4
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
        id: rightCluster
        x: bar.width - width
        y: bar.televisionMode ? Math.round((bar.height - height) / 2) : (bar.compactLayout ? 7 : Math.round((bar.height - height) / 2))
        spacing: bar.televisionMode ? 24 : (bar.compactLayout ? 12 : 20)
        // Search — worlds only.
        SysIcon {
            objectName: "topBarSearch"
            source: "../assets/icons/search.svg"
            accessibleName: "Search"
            onClicked: bar.searchClicked()
            visible: bar.activeMedium !== ""
        }
        // Update — home only. Takes search's throne; the silver badge signals an
        // available release (pulse on unseen, steady once seen).
        Item {
            width: 22; height: 22
            visible: bar.activeMedium === ""
            opacity: updateInput.interactionActive ? 1.0 : 0.92
            objectName: "colosseumTopbarUpdateButton"
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
            KeyboardAction {
                id: updateInput
                anchors.fill: parent
                accessibleName: bar.updateAvailable ? "Update available" : "Updates"
                focusRadius: 6
                onTriggered: bar.updateClicked()
            }
        }
        // Account identity (Bundle 8C first-light): gold-ringed initial when
        // signed in, quiet outline when not. Sits beside Update + Wallpapers.
        Item {
            id: accountButton
            width: bar.localDevice ? Math.max(58, localDeviceText.implicitWidth + 18) : 22
            height: 22
            objectName: "colosseumTopbarAccountButton"
            opacity: accountInput.interactionActive ? 1.0 : 0.92
            Rectangle {
                anchors.fill: parent
                radius: bar.localDevice ? 11 : width / 2
                color: bar.accountPresent
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.16)
                    : (bar.localDevice ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                border.width: 1.5
                border.color: bar.accountPresent
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.8)
                    : Qt.rgba(1, 1, 1, 0.38)
                Item {
                    anchors.centerIn: parent
                    width: 14; height: 14
                    visible: !bar.accountPresent && !bar.localDevice
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
                    id: localDeviceText
                    objectName: "colosseumTopbarDeviceLabel"
                    anchors.centerIn: parent
                    visible: bar.localDevice
                    text: qsTr("Device")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
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
            KeyboardAction {
                id: accountInput
                anchors.fill: parent
                accessibleName: bar.localDevice
                    ? qsTr("Device")
                    : (bar.accountPresent
                        ? ("Account: " + bar.accountController.username)
                        : "Account")
                focusRadius: bar.localDevice ? 11 : width / 2
                onTriggered: {
                    const anchor = accountButton.mapToItem(null, accountButton.width, accountButton.height)
                    bar.accountClicked(anchor.x, anchor.y)
                }
            }
        }
        // Automation identity (Lanista): the wallpaper control is a production shell
        // action, so capture journeys address this button directly instead of replacing
        // the top bar with a harness-only presentation.
        SysIcon {
            objectName: "topBarWallpaperButton"
            source: "../assets/icons/settings.svg"
            accessibleName: "Wallpaper"
            onClicked: bar.wallpaperClicked()
        }
        SysIcon { visible: bar.desktopWindowControlsVisible; source: "../assets/icons/minimize.svg"; accessibleName: "Minimize"; onClicked: bar.minimizeClicked() }
        // Fullscreen toggle (Hemanth 2026-07-16, supersedes the old never-☐ topbar
        // rule): glyph shows the ACTION — expand while windowed, contract while
        // fullscreen. Drives the same shell flip as the F11 developer door.
        SysIcon {
            visible: bar.desktopWindowControlsVisible
            source: bar.shellWindowed ? "../assets/icons/fullscreen.svg"
                                      : "../assets/icons/fullscreen-exit.svg"
            accessibleName: bar.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
            onClicked: bar.fullscreenClicked()
        }
        SysIcon { visible: bar.desktopWindowControlsVisible; source: "../assets/icons/power.svg"; accessibleName: "Quit Colosseum"; onClicked: bar.powerClicked() }
    }
}
