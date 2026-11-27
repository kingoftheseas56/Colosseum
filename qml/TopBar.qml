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

    required property Item backdrop          // wallpaper to composite the pills' glass over
    property string activeMedium: ""         // "" = home / no selection
    property bool compactBrowserChrome: false
    readonly property bool compactChrome: compactBrowserChrome || width < (androidHost ? 1100 : 1200)
    // Android uses the outer header space to keep every group in one row.
    property bool reserveHomeBackSpace: false
    readonly property real leftOtherWidth:
        (backSlot.visible ? backSlot.width : 0)
        + (leftTitle.visible ? leftTitle.implicitWidth : 0)
    readonly property int leftOtherCount: (backSlot.visible ? 1 : 0) + (leftTitle.visible ? 1 : 0)
    readonly property real clockFreeSpace: width - systemChrome.implicitWidth
        - (pillsRow.implicitWidth + 14)
        - 24 - leftOtherWidth - leftOtherCount * (bar.androidHost ? inputMetrics.chromeGap : 18)
    readonly property bool clockFits: !backOnly && width > 0 && clockBlock.implicitWidth <= clockFreeSpace
    property bool backOnly: false            // Extensions: only Back, no clock, pills or system icons
    // World roots keep the historical Home destination; nested surfaces can name their
    // owning world without inventing a second Back control.
    property string backLabel: "Home"
    // With backOnly, the page names itself beside the arrow (as Your Colosseum does).
    property string pageTitle: ""
    property string pageTitleFont: ""
    property string pageSubtitle: ""
    // Retained world pages stay instantiated for state preservation, but hidden bars must not
    // keep their live clock timer waking the GUI every second.
    property bool lifecycleActive: true
    // Host capability is independent of the desktop-shaped presentation on Android.
    property bool androidHost: Qt.platform.os === "android"
    AdaptiveLayout { id: inputMetrics; objectName: "topBarInputMetrics"; androidHost: bar.androidHost; viewportWidth: bar.width }
    readonly property real touchTargetSize: inputMetrics.touchTargetSize
    readonly property real systemGlyphSize: bar.androidHost ? 26 : 22
    readonly property real touchHorizontalInset: bar.touchTargetSize > 0
        ? Math.min((bar.touchTargetSize - bar.systemGlyphSize) / 2, Math.floor(systemChrome.spacing / 2)) : 0
    // Wallpaper is available from the desktop app picker.
    property bool wallpaperEnabled: false
    property string clock: "8:29"
    property string ampm: "PM"
    property string date: "Wednesday, June 24"
    property var accountController:
        typeof AccountController !== "undefined" ? AccountController : null
    readonly property bool accountPresent: accountController
        && (accountController.mode === "signedIn"
            || accountController.mode === "offline")
    // No-account edition gate: the account medallion is the only shell entry
    // point to the account flyout/center, so hiding it removes sign-in,
    // registration and account-sync surfaces wholesale. Stub controllers
    // without the property (harness fakes) keep the button for back-compat.
    readonly property bool accountEntryAvailable: accountController
        && accountController.accountFeaturesAvailable !== false
    Keys.onPressed: (event) => spatialNav.handle(event)

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal dlnaClicked(real anchorRight, real anchorBottom)
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
    property bool utilityActionsInPicker: false
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

    // DLNA sharing door ("Share on your Wi-Fi"). `dlnaSharing` is a context property that
    // only exists when the native feature is compiled in, so every read is guarded.
    readonly property var dlnaState: typeof dlnaSharing !== "undefined" ? dlnaSharing : null
    readonly property bool dlnaAvailable: !!bar.dlnaState
    readonly property bool dlnaRunning: bar.dlnaAvailable && bar.dlnaState.running === true
    readonly property bool dlnaVisible: bar.dlnaAvailable && bar.lifecycleActive
        && (bar.activeMedium === "" || bar.dlnaRunning)

    // Update availability flags from the shell: the silver badge pulses until seen.
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
        property string accessibleName: ""
        property bool showLabel: false
        property bool lit: false      // full-strength (e.g. a running state), regardless of hover
        property real glyphSize: 22
        signal clicked()
        // On Android the glyph rides a touch-height host that shares the neighbouring gaps.
        width: showLabel ? 82 : glyphSize
        height: Math.max(glyphSize, bar.touchTargetSize)
        Image {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: sysRoot.glyphSize; height: sysRoot.glyphSize
            source: sysRoot.source
            sourceSize.width: sysRoot.glyphSize; sourceSize.height: sysRoot.glyphSize
            fillMode: Image.PreserveAspectFit
            opacity: (sysRoot.lit || input.interactionActive) ? 1.0 : 0.72
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
            anchors.leftMargin: -bar.touchHorizontalInset
            anchors.rightMargin: -bar.touchHorizontalInset
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
        readonly property bool active: !pill.comingSoon && bar.activeMedium === pill.label
        readonly property bool hot: pillInput.interactionActive && !pill.comingSoon
        implicitWidth: Math.max(bar.androidHost ? inputMetrics.touchTargetSize : 0,
            pillContent.implicitWidth + (bar.androidHost ? inputMetrics.chromePillPadding
            : (bar.width < 900 ? 16 : (bar.width < 1100 ? 24 : 34))))
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
            spacing: bar.androidHost ? inputMetrics.chromePillContentGap : 6
            Text {
                text: pill.label
                color: pill.active ? "#1a1408" : (pillInput.interactionActive && !pill.comingSoon ? theme.ink : theme.inkDim)
                opacity: pill.comingSoon ? 0.6 : 1.0
                font.family: theme.ui; font.pixelSize: bar.androidHost ? inputMetrics.chromePillFontSize : 14
                font.weight: pill.active ? Font.DemiBold : Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {   // Original inline "SOON" marker — no world yet
                visible: pill.comingSoon
                anchors.verticalCenter: parent.verticalCenter
                radius: 4; height: 15; width: soonText.implicitWidth + 10
                color: Qt.rgba(1,1,1,0.10)
                Text {
                    id: soonText; anchors.centerIn: parent; text: "SOON"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: bar.androidHost ? inputMetrics.chromeSoonFontSize : 8; font.letterSpacing: 0.8
                }
            }
        }
        KeyboardAction {
            id: pillInput
            objectName: "modePillFocus_" + pill.label
            anchors.fill: parent
            anchors.topMargin: -inputMetrics.targetInset(parent.height)
            anchors.bottomMargin: -inputMetrics.targetInset(parent.height)
            enabled: !pill.comingSoon
            accessibleName: pill.label + (pill.comingSoon ? ", Coming Soon" : "")
            focusRadius: pill.height / 2
            onTriggered: bar.mediumSelected(pill.label)
        }
    }

    // ---- left: "‹ Home" (world only) + clock/date ----
    Row {
        id: leftChrome
        objectName: "topBarLeftChrome"
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        spacing: bar.androidHost ? inputMetrics.chromeGap : 18
        Item {
            id: backSlot
            visible: bar.activeMedium !== "" || (bar.androidHost && bar.reserveHomeBackSpace && !bar.backOnly)
            width: backAction.implicitWidth
            height: backAction.implicitHeight
            anchors.verticalCenter: parent.verticalCenter
            BackAction {
                id: backAction
                visible: bar.activeMedium !== ""
                label: bar.compactChrome || (bar.backOnly && bar.pageTitle.length) ? "" : bar.backLabel
                tip: label.length === 0 ? bar.backLabel : ""
                labelSize: 14
                idleColor: theme.inkDim
                hoverColor: theme.ink
                onTriggered: bar.homeRequested()
            }
        }
        Column {
            id: leftTitle
            visible: bar.backOnly && bar.pageTitle.length > 0
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            Text {
                objectName: "topBarPageTitle"
                text: bar.pageTitle
                color: theme.ink
                font.family: bar.pageTitleFont || theme.display
                font.pixelSize: 24
                font.weight: bar.pageTitleFont ? Font.DemiBold : Font.Normal
                Accessible.role: Accessible.Heading
            }
            Text {
                visible: bar.pageSubtitle.length > 0
                text: bar.pageSubtitle; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
            }
        }
        Column {
            id: clockBlock
            objectName: "topBarClock"
            readonly property real textWidth: Math.max(clockTime.implicitWidth + 5 + clockPeriod.implicitWidth,
                                                       clockDate.implicitWidth)
            readonly property real leftWidth: textWidth + (backAction.visible ? backAction.width + leftChrome.spacing : 0)
            visible: bar.androidHost ? !bar.backOnly
                : !bar.backOnly && !bar.compactBrowserChrome
                     && leftWidth + 24 + worldPicker.width <= systemChrome.x
            spacing: 3
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 5
                Text { id: clockTime; text: bar.clock; color: theme.ink; font.family: theme.display; font.pixelSize: bar.androidHost ? inputMetrics.chromeClockSize : 32 }
                Text { id: clockPeriod; text: bar.ampm; color: theme.inkDim; font.family: theme.ui; font.pixelSize: bar.androidHost ? inputMetrics.chromeClockPeriodSize : 16
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4 }
            }
            Text {
                id: clockDate
                text: bar.date; color: theme.inkDim; font.family: theme.ui
                font.pixelSize: bar.androidHost ? inputMetrics.chromeDateSize : 13
                width: bar.androidHost ? Math.max(clockTime.implicitWidth + 5 + clockPeriod.implicitWidth,
                    Math.min(implicitWidth, bar.clockFreeSpace)) : implicitWidth
                fontSizeMode: bar.androidHost ? Text.Fit : Text.FixedSize
                minimumPixelSize: inputMetrics.chromeMinimumDateSize
            }
        }
    }

    // ---- center: library pills in a glass capsule ----
    Glass {
        id: worldPicker
        objectName: "topBarWorldPicker"
        visible: !bar.backOnly
        backdrop: bar.backdrop
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(leftChrome.width + 12,
                    Math.min((bar.width - width) / 2, systemChrome.x - width - 12))
        radius: 999
        tint: theme.glassTint.a
        edge: theme.edge
        scrim: 0
        width: pillsRow.implicitWidth + 14; height: 46
        Row {
            id: pillsRow
            anchors.centerIn: parent
            spacing: bar.androidHost ? inputMetrics.chromePillGap : 4
            Pill { label: "Tankoban" }
            Pill { label: "Biblio" }
            Pill { label: "Theatre" }
            Pill {
                id: vinylPill
                label: "Vinyl"
                readonly property bool qualificationEnabled: typeof VinylLiveEnabled !== "undefined" && VinylLiveEnabled
                comingSoon: !qualificationEnabled
                objectName: bar.visible && bar.lifecycleActive ? (qualificationEnabled ? "worldPillVinyl" : "modePill_Vinyl") : ""
                Item {
                    readonly property bool qualificationEnabled: vinylPill.qualificationEnabled
                    anchors.fill: parent
                    objectName: (bar.visible && bar.lifecycleActive && vinylPill.qualificationEnabled) ? "modePill_Vinyl" : ""
                }
            }
            Pill { label: "Universes" }
            Pill {
                label: "Feria"
                comingSoon: !bar.Window.window || bar.Window.window.feriaComingSoon !== false
            }
        }
    }

    // ---- right: system icons ----
    // Desktop shares a slot between Home Update and world Search. Android
    // keeps Update beside Account even in worlds that also offer Search.
    Row {
        id: systemChrome
        objectName: "topBarSystemChrome"
        visible: !bar.backOnly
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        // Preserve the Android hit width by sharing the space between smaller glyphs.
        spacing: bar.androidHost
            ? Math.max(inputMetrics.chromeSystemGap, (inputMetrics.tightAndroidChrome ? 36 : 46) - bar.systemGlyphSize)
            : (bar.width < 1000 ? 12 : (bar.width < 1200 ? 20 : 24))
        // Search — worlds only, except Universes (no search behaviour there yet).
        SysIcon {
            // Retained worlds keep their TopBars alive together. Give each
            // non-Theatre search a world-specific name so Lanista can target
            // the visible Theatre search without matching a hidden sibling.
            objectName: bar.activeMedium === "Theatre" ? "topBarSearch"
                       : bar.activeMedium === "" ? "" : "topBarSearch_" + bar.activeMedium
            source: "../assets/icons/search.svg"
            glyphSize: bar.systemGlyphSize
            accessibleName: "Search"
            onClicked: bar.searchClicked()
            visible: bar.activeMedium !== "" && bar.activeMedium !== "Universes"
        }
        // The silver badge signals an available release (pulse on unseen, steady once seen).
        Item {
            y: bar.androidHost ? (parent.height - height) / 2 : 0
            width: bar.systemGlyphSize; height: width
            visible: bar.androidHost || bar.activeMedium === ""
            opacity: updateInput.interactionActive ? 1.0 : 0.92
            objectName: "colosseumTopbarUpdateButton"
            Image {
                anchors.fill: parent
                source: "../assets/icons/update.svg"
                sourceSize.width: parent.width; sourceSize.height: parent.height
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
                        && (!bar.androidHost || bar.visible)
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
                anchors.topMargin: -inputMetrics.targetInset(parent.height)
                anchors.bottomMargin: anchors.topMargin
                anchors.leftMargin: -bar.touchHorizontalInset
                anchors.rightMargin: anchors.leftMargin
                accessibleName: bar.updateAvailable ? "Update available" : "Updates"
                focusRadius: 6
                onTriggered: bar.updateClicked()
            }
        }
        // Third-party trackers — every world and home (Stremio lives inside Trackers);
        // on home it follows the Update glyph.
        Item {
            id: trackersButton
            y: bar.androidHost ? (parent.height - height) / 2 : 0
            // Home and every retained world own a TopBar; suffix world copies so automation
            // never resolves a hidden sibling.
            objectName: !bar.lifecycleActive ? ""
                        : bar.activeMedium === "" ? "topBarTrackersButton"
                        : "topBarTrackersButton_" + bar.activeMedium
            width: 22; height: 22
            visible: !bar.androidHost && !bar.utilityActionsInPicker && bar.trackersEnabled && bar.lifecycleActive
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
            y: bar.androidHost ? (parent.height - height) / 2 : 0
            width: bar.systemGlyphSize
            height: width
            objectName: "colosseumTopbarAccountButton"
            visible: bar.accountEntryAvailable
            opacity: accountInput.interactionActive ? 1.0 : 0.92
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: bar.accountPresent
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.16)
                    : "transparent"
                border.width: 1.5
                border.color: bar.accountPresent
                    ? Qt.rgba(0.94, 0.77, 0.29, 0.8)
                    : Qt.rgba(1, 1, 1, 0.38)
                Item {
                    objectName: "colosseumTopbarAccountGlyph"
                    anchors.centerIn: parent
                    width: 14; height: 14
                    scale: accountButton.width / 22
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
                    font.pixelSize: Math.round(accountButton.width / 2)
                    font.weight: Font.DemiBold
                }
            }
            KeyboardAction {
                id: accountInput
                objectName: "colosseumTopbarAccountInput"
                anchors.fill: parent
                anchors.topMargin: -inputMetrics.targetInset(parent.height)
                anchors.bottomMargin: anchors.topMargin
                anchors.leftMargin: -bar.touchHorizontalInset
                anchors.rightMargin: anchors.leftMargin
                focusOnPointer: false
                accessibleName: bar.accountPresent
                    ? qsTr("Account: %1").arg(bar.accountController.username) : qsTr("Account Centre")
                focusRadius: width / 2
                onTriggered: {
                    const anchor = accountButton.mapToItem(null, accountButton.width, accountButton.height)
                    bar.accountClicked(anchor.x, anchor.y)
                }
            }
        }
        // Share on your Wi-Fi (DLNA). Home always; inside worlds only while sharing runs, so
        // the gold glyph tells you the library is still visible to TVs. Gold = running.
        SysIcon {
            id: dlnaButton
            // Retained worlds each own a TopBar; only the live copy carries the bare name.
            objectName: !bar.lifecycleActive ? ""
                        : bar.activeMedium === "" ? "topBarDlnaButton"
                        : "topBarDlnaButton_" + bar.activeMedium
            visible: !bar.utilityActionsInPicker && bar.dlnaVisible
            lit: bar.dlnaRunning
            source: bar.dlnaRunning ? "../assets/icons/access-point-gold.svg"
                                    : "../assets/icons/access-point.svg"
            accessibleName: "Share on your Wi-Fi"
            onClicked: {
                const anchor = dlnaButton.mapToItem(null, dlnaButton.width, dlnaButton.height)
                bar.dlnaClicked(anchor.x, anchor.y)
            }
        }
        // Automation identity (Lanista): the wallpaper control is a production shell
        // action, so capture journeys address this button directly instead of replacing
        // the top bar with a harness-only presentation.
        SysIcon {
            objectName: "topBarWallpaperButton"
            visible: bar.wallpaperEnabled
            source: "../assets/icons/settings.svg"
            accessibleName: "Wallpaper"
            onClicked: bar.wallpaperClicked()
        }
        SysIcon { visible: !bar.androidHost; source: "../assets/icons/minimize.svg"; accessibleName: "Minimize"; onClicked: bar.minimizeClicked() }
        // Fullscreen toggle (Hemanth 2026-07-16, supersedes the old never-☐ topbar
        // rule): glyph shows the ACTION — expand while windowed, contract while
        // fullscreen. Drives the same shell flip as the F11 developer door.
        SysIcon {
            visible: !bar.androidHost
            source: bar.shellWindowed ? "../assets/icons/fullscreen.svg"
                                      : "../assets/icons/fullscreen-exit.svg"
            accessibleName: bar.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
            onClicked: bar.fullscreenClicked()
        }
        SysIcon { visible: !bar.androidHost; source: "../assets/icons/power.svg"; accessibleName: "Quit Colosseum"; onClicked: bar.powerClicked() }
    }
}
