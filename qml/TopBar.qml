// TopBar — the shared Colosseum shell chrome: clock/date · library pills · system icons.
// ONE source for the top bar across the home AND every world page.
//   activeMedium == ""   → HOME: no pill is selected (the no-selection rule).
//   activeMedium == "X"  → WORLD: pill X carries the gold selected accent, and a "‹ Home"
//                          affordance appears at the left.
// Emits intent signals; the host (home / world) decides what navigation happens.

import QtQuick

Item {
    id: bar

    // Arrow keys behave like a D-pad inside the visible chrome. At a directional
    // boundary the event stays unaccepted so the owning page can move into content.
    KeyboardSpatialNavigator {
        id: spatialNav
        root: bar
        onBoundaryRequested: (key, fromItem) => bar.boundaryArrowRequested(key, fromItem)
    }
    Keys.onPressed: function(event) { spatialNav.handle(event) }

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
    readonly property bool localDevice: accountController
        && accountController.mode === "localOnly"

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal stremioClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal accountClicked(real anchorRight, real anchorBottom)
    signal fullscreenClicked()
    signal minimizeClicked()
    signal powerClicked()
    signal updateClicked()
    signal trackersClicked()
    signal boundaryArrowRequested(int key, Item fromItem)

    // Third-party trackers door (AniList, MAL, Trakt, Simkl...). Unlike Stremio it is not one
    // provider and it serves every world, so it gets a provider-neutral mark in all of them.
    // Hosts that wire trackersClicked opt in; other TopBar hosts (reader, player) stay unchanged.
    property bool trackersEnabled: false
    property bool trackersActive: false
    // Aggregate sync state -> dot colour. No dot when nothing is connected, so the icon
    // never nags someone who doesn't use trackers.
    readonly property string trackersDotState: {
        if (typeof TrackerSyncCenter === "undefined" || !TrackerSyncCenter)
            return ""
        var state = TrackerSyncCenter.aggregateState || ({})
        var status = String(state.status || "Empty")
        if (status === "Attention" || status === "Owner unavailable")
            return "error"
        if (status === "Empty" && Number(state.connectedCount || 0) === 0)
            return ""
        if (status === "Healthy")
            return "ok"
        return "pending"
    }
    function focusTrackersButton() {
        if (trackersButton.visible)
            trackersInput.forceActiveFocus(Qt.TabFocusReason)
    }

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
    function focusStremioButton() {
        if (stremioButton.visible)
            stremioInput.forceActiveFocus(Qt.PopupFocusReason)
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
        property bool showLabel: false
        signal clicked()
        width: showLabel ? 82 : 22; height: 22
        Image {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22
            source: sysRoot.source
            sourceSize.width: 22; sourceSize.height: 22
            fillMode: Image.PreserveAspectFit
            opacity: input.interactionActive ? 1.0 : 0.72
        }
        Text {
            visible: sysRoot.showLabel
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.verticalCenter: parent.verticalCenter
            text: sysRoot.accessibleName
            color: input.interactionActive ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
        }
        KeyboardAction {
            id: input
            anchors.fill: parent
            accessibleName: sysRoot.accessibleName
            focusRadius: 6
            onTriggered: sysRoot.clicked()
        }
    }

    // ---- inline: a library pill (selected when its label == activeMedium).
    //      Clean centered TEXT — icons return later with proper active/inactive tinting.
    //      comingSoon → a placeholder mode: muted "SOON" tag, not navigable. ----
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
                color: pill.active ? "#1a1408" : (pillInput.interactionActive && !pill.comingSoon ? theme.ink : theme.inkDim)
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
        KeyboardAction {
            id: pillInput
            objectName: "modePillFocus_" + pill.label
            anchors.fill: parent
            enabled: !pill.comingSoon
            accessibleName: pill.label
            focusRadius: pill.height / 2
            onTriggered: bar.mediumSelected(pill.label)
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
            Pill { label: "Tankoban" }
            Pill { label: "Biblio" }
            Pill { label: "Theatre" }
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
            // Retained worlds keep their TopBars alive together. Give each
            // non-Theatre search a world-specific name so Lanista can target
            // the visible Theatre search without matching a hidden sibling.
            objectName: bar.activeMedium === "Theatre" ? "topBarSearch"
                       : bar.activeMedium === "" ? "" : "topBarSearch_" + bar.activeMedium
            source: "../assets/icons/search.svg"
            accessibleName: "Search"
            onClicked: bar.searchClicked()
            visible: bar.activeMedium !== ""
        }
        // Theatre's provider door. The official artwork remains untouched;
        // only the small external badge communicates an actionable state.
        Item {
            id: stremioButton
            // Retained worlds each own a TopBar. Name only Theatre's copy so
            // accessibility/automation never resolves a hidden sibling.
            objectName: bar.activeMedium === "Theatre" ? "topBarStremioButton" : ""
            width: 22; height: 22
            visible: bar.activeMedium === "Theatre" && bar.lifecycleActive
            opacity: stremioInput.interactionActive ? 1.0 : 0.92
            Image {
                objectName: "topBarStremioOfficialAsset"
                anchors.fill: parent
                source: "../assets/icons/stremio-official.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                objectName: "topBarStremioAttentionBadge"
                visible: typeof stremioSyncState !== "undefined"
                    && stremioSyncState
                    && (stremioSyncState.status === "reconnectRequired"
                        || stremioSyncState.status === "syncFailed")
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.rightMargin: -3; anchors.bottomMargin: -3
                width: 8; height: 8; radius: 4
                color: theme.gold
                border.width: 1; border.color: "#15151a"
            }
            KeyboardAction {
                id: stremioInput
                objectName: "topBarStremioInput"
                anchors.fill: parent
                accessibleName: qsTr("Stremio Sync")
                focusRadius: 6
                onTriggered: bar.stremioClicked()
            }
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
        // Third-party trackers — every world and home. Theatre shows it beside Stremio;
        // on home it follows the Update glyph.
        Item {
            id: trackersButton
            // Home and every retained world own a TopBar; suffix world copies so automation
            // never resolves a hidden sibling.
            objectName: !bar.lifecycleActive ? ""
                        : bar.activeMedium === "" ? "topBarTrackersButton"
                        : "topBarTrackersButton_" + bar.activeMedium
            width: 22; height: 22
            visible: bar.trackersEnabled && bar.lifecycleActive
            opacity: trackersInput.interactionActive || bar.trackersActive ? 1.0 : 0.92
            Image {
                anchors.fill: parent
                source: "../assets/icons/trackers.svg"
                sourceSize.width: 44; sourceSize.height: 44
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                objectName: "topBarTrackersStatusDot"
                visible: bar.trackersDotState !== ""
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.rightMargin: -3; anchors.bottomMargin: -3
                width: 8; height: 8; radius: 4
                color: bar.trackersDotState === "ok" ? "#5fd18b"
                     : bar.trackersDotState === "error" ? "#ef6a5a" : theme.gold
                border.width: 1; border.color: "#15151a"
            }
            KeyboardAction {
                id: trackersInput
                objectName: bar.activeMedium === "" ? "topBarTrackersInput" : ""
                anchors.fill: parent
                accessibleName: bar.trackersDotState === "error"
                                ? qsTr("Trackers, attention required") : qsTr("Trackers")
                focusRadius: 6
                onTriggered: bar.trackersClicked()
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
                objectName: "colosseumTopbarAccountInput"
                anchors.fill: parent
                focusOnPointer: false
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
        SysIcon { source: "../assets/icons/minimize.svg"; accessibleName: "Minimize"; onClicked: bar.minimizeClicked() }
        // Fullscreen toggle (Hemanth 2026-07-16, supersedes the old never-☐ topbar
        // rule): glyph shows the ACTION — expand while windowed, contract while
        // fullscreen. Drives the same shell flip as the F11 developer door.
        SysIcon {
            source: bar.shellWindowed ? "../assets/icons/fullscreen.svg"
                                      : "../assets/icons/fullscreen-exit.svg"
            accessibleName: bar.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
            onClicked: bar.fullscreenClicked()
        }
        SysIcon { source: "../assets/icons/power.svg"; accessibleName: "Quit Colosseum"; onClicked: bar.powerClicked() }
    }
}
