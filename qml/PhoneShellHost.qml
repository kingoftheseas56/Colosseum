// PhoneShellHost.qml — Android phone/tablet/TV shell seam extracted from Main.qml (A02).
// Exists on all platforms so the desktop `--phone-shell` developer switch and viewport
// harnesses continue to work unchanged while keeping Main.qml to 1-line hooks per seam.
import QtQuick
import QtQuick.Window
import QtCore
import "PhoneOverlayRegistry.js" as PhoneOverlays

Item {
    id: host
    objectName: "phoneShellHost"
    anchors.fill: parent
    z: 55
    QtObject {
        id:overlayChanges
        property int revision:0
        Component.onCompleted:PhoneOverlays.subscribe(host.window,overlayChanges)
        Component.onDestruction:PhoneOverlays.unsubscribe(overlayChanges)
    }

    required property Window window
    required property Item worldStack
    required property Item topbar
    required property Flickable page
    required property Item contentCol
    required property Item hero
    required property Item hallDoor
    required property Loader homeBookshelfLoader
    required property Loader homeTheatreStripLoader
    required property Loader homeReadingDeskLoader
    required property Loader homeVaultWidgetLoader
    required property Loader downloadsLayer
    required property Loader extensionsLayer
    required property Loader historyStatsLayer
    required property Loader syncCenterLayer
    required property Item stremioPanel
    required property Item accountFlyout
    required property Item wall
    required property QtObject escapeCommand
    property bool fullWindowPageOpen: false

    // W07 presentation seam. W02 may expose native UiMode/LEANBACK detection as
    // PlatformRuntime.androidTelevision; until then every existing desktop/phone/tablet path stays false.
    readonly property bool televisionMode: (typeof PlatformRuntime !== "undefined")
                                           && PlatformRuntime
                                           && PlatformRuntime["androidTelevision"] === true

    // Developer switch: `colosseum.exe qml/Main.qml --phone-shell` (or `--phone-shell=360x700`)
    // draws the Android phone shell in a plain desktop window, so phone layout can be judged
    // without an APK. Layout only: readers, the player and fonts still need a real device.
    readonly property string phoneShellArg: {
        var args = Qt.application.arguments
        for (var i = 2; i < args.length; ++i)
            if (String(args[i]).indexOf("--phone-shell") === 0) return String(args[i])
        return ""
    }
    property bool androidHost: Qt.platform.os === "android" || phoneShellArg.length > 0
    AdaptiveLayout {
        id: adaptive
        page:host
        touch:host.androidHost
        viewportWidth:host.width
        viewportHeight:host.height
    }
    readonly property bool worldRailFits:adaptive.canFitRail(wideWorldRail.width)

    // Android shell (decision 19 / Slice S2): there is no Home on Android. The app opens in the
    // world it was last in, Theatre the first time.
    Settings {
        id: androidShellSettings
        category: "androidShell"
        property string lastWorld: ""
    }

    // Slice S3: the Profile tab is a plain list of doors to pages the app already has.
    readonly property var profileRows: [
        // The same doors the desktop has, under the names the app already uses for them
        // (Hemanth, 2026-10-10): the account page, Your Colosseum, trackers, and Stremio.
        { key: "downloads",  label: "Downloads",      note: "Everything saved on this device" },
        { key: "extensions", label: "Extensions",     note: "Add-ons and sources for every world" },
        { key: "colosseum",  label: "Your Colosseum", note: "History, highlights and stats" },
        { key: "trackers",   label: "Trackers",       note: "Connections to other libraries" },
        { key: "stremio",    label: "Stremio",        note: "Bring your Stremio library across" },
        { key: "account",    label: "Account",        note: "Sign in, devices and security" }
    ]

    readonly property bool profileActive: profileLayer.active
    // Read on this full-window Item: SafeArea cannot attach to the Binding elements below.
    readonly property var safeMargins: SafeArea.margins
    readonly property bool phoneImeVisible: host.androidHost && Qt.inputMethod.visible
    readonly property var librarySelectionOwner: host.window ? host.window["phoneLibrarySelectionOwner"] : null
    function clearLibrarySelection() {
        if (librarySelectionOwner && librarySelectionOwner.selection)
            librarySelectionOwner.selection.clear()
    }
    onFullWindowPageOpenChanged: if (fullWindowPageOpen) clearLibrarySelection()
    onProfileActiveChanged: if (profileActive) clearLibrarySelection()
    readonly property bool phoneWorldNavigationVisible:
        host.androidHost && host.window && !host.worldRailFits
        && !host.window.immersiveSurfaceOpen && !host.phoneImeVisible && !host.fullWindowPageOpen
        && !host.librarySelectionOwner
    readonly property bool wideWorldNavigationVisible:
        host.androidHost && host.window && host.worldRailFits
        && !host.window.immersiveSurfaceOpen && !host.phoneImeVisible && !host.fullWindowPageOpen
        && !host.librarySelectionOwner
    readonly property real wideWorldNavigationLeftInset:
        host.wideWorldNavigationVisible ? wideWorldRail.width : 0
    readonly property real contentBottomInset:
        host.librarySelectionOwner ? host.librarySelectionOwner.reservedHeight
            : phoneWorldTabs.visible ? phoneWorldTabs.height : 0

    readonly property var openMediaNameFilters: Qt.platform.os === "android" ? ["All files (*)"] : [
        "Media files (*.cbz *.cbr *.epub *.mp4 *.mkv *.avi *.mov *.webm *.m4v)",
        "Comics (*.cbz *.cbr)",
        "Books (*.epub)",
        "Video (*.mp4 *.mkv *.avi *.mov *.webm *.m4v)",
        "All files (*)"
    ]

    function openProfilePage() { profileLayer.active = true }
    function closeProfilePage() { profileLayer.active = false }
    // leaving the Profile tab for a world closes the list and whichever of its pages is open
    function closeProfileStack() {
        profileLayer.active = false
        if (host.downloadsLayer.active) host.window.closeDownloadsPage()
        if (host.extensionsLayer.active) host.window.closeExtensionsPage()
        if (host.historyStatsLayer.active) host.window.closeHistoryStatsPage()
        if (host.syncCenterLayer.active) host.window.closeSyncCenterPage()
        host.stremioPanel.shown = false
        if (host.accountFlyout.visible) host.accountFlyout.close()
    }
    function openProfileRow(key) {
        switch (key) {
        case "downloads":  host.window.openDownloadsPage(); return
        case "extensions": host.window.openExtensionsPage(); return
        case "colosseum":  host.window.openHistoryStatsPage(); return
        case "trackers":   host.window.openSyncCenterPage(); return
        case "stremio":    host.window.openStremioSyncPanel(); return
        // the account door the top bar's pill opens: signed out it offers Sign in
        case "account":    host.accountFlyout.toggleAt(host.window.width - 16, 76); return
        }
    }
    function enterAndroidStartWorld() {
        if (!host.androidHost || host.worldStack.current !== "") return
        var want = androidShellSettings.lastWorld
        var known = false
        for (var i = 0; i < host.window.worldDefinitions.length; ++i)
            if (host.window.worldDefinitions[i].name === want) { known = true; break }
        host.window.openWorld(known ? want : "Theatre")
    }
    function recordWorldOpened(medium) {
        if (host.androidHost) androidShellSettings.lastWorld = medium
    }
    function initializeWindow(windowModeObj) {
        if (host.phoneShellArg.length > 0) {
            var phoneSize = host.phoneShellArg.split("=")[1] || "412x700"
            host.window.flags = Qt.Window
            host.window.width = parseInt(phoneSize.split("x")[0]) || 412
            host.window.height = parseInt(phoneSize.split("x")[1]) || 700
            host.window.show()
            return true
        }
        return false
    }
    function attachHostedItem(item) {
        if (item && item.contentBottomInset !== undefined)
            item.contentBottomInset = Qt.binding(function() { return host.contentBottomInset })
    }

    // API34+ gestures preview through the native bridge; commit uses the same shell ladder.
    BackGestureIndicator {
        parent: host.window.contentItem
        anchors.fill: parent
        z: 100000
        gestureActive: host.androidHost && typeof PlatformRuntime !== "undefined"
            && PlatformRuntime.backGestureActive === true
        progress: gestureActive ? PlatformRuntime.backGestureProgress : 0
        edge: gestureActive ? PlatformRuntime.backGestureEdge : 0
    }

    // Legacy Back keys still use the same owner. Modern callbacks do not synthesize keys.
    Shortcut {
        sequence: "Back"
        enabled: {
            overlayChanges.revision
            return host.androidHost && PhoneOverlays.current(host.window)===null
        }
        onActivated: host.escapeCommand.invoke("androidBack")
    }

    // A Back press nothing claimed reaches Qt as a request to close this window, and the app
    // exits (seen on the Nokia, 2026-10-10: Back while a book was still loading, Android's exit
    // record EXIT_SELF). On Android that request is treated as Back; the app never quits on it.
    Connections {
        target: host.window
        function onImmersiveSurfaceOpenChanged() {
            if (host.window.immersiveSurfaceOpen) host.clearLibrarySelection()
        }
        function onClosing(close) {
            if (Qt.platform.os !== "android") return
            close.accepted = false
            host.escapeCommand.invoke("androidClose")
        }
    }

    // Responsive overrides applied when androidHost or televisionMode is active.
    Binding {
        target: host.topbar
        property: "x"
        value: host.wideWorldNavigationLeftInset + Math.max(adaptive.pageMargin, host.safeMargins.left)
        when: host.androidHost
    }
    Binding {
        target: host.topbar
        property: "y"
        value: Math.max(adaptive.topInset, host.safeMargins.top)
        when: host.androidHost
    }
    Binding {
        target: host.topbar
        property: "width"
        value: host.window.width - host.wideWorldNavigationLeftInset
            - Math.max(adaptive.pageMargin, host.safeMargins.left) - Math.max(adaptive.pageMargin, host.safeMargins.right)
        when: host.androidHost
    }
    Binding {
        target: host.page
        property: "y"
        value: host.topbar.y + host.topbar.height + (host.televisionMode ? 18 : (adaptive.compactChrome ? 8 : 10))
        when: host.androidHost || host.televisionMode
    }
    Binding {
        target: host.contentCol
        property: "x"
        value: Math.max(adaptive.pageMargin, host.safeMargins.left)
        when: host.androidHost
    }
    Binding {
        target: host.contentCol
        property: "width"
        value: host.page.width - Math.max(adaptive.pageMargin, host.safeMargins.left)
            - Math.max(adaptive.pageMargin, host.safeMargins.right)
        when: host.androidHost
    }
    Binding {
        target: host.contentCol
        property: "spacing"
        value: adaptive.homeSectionSpacing
        when: host.androidHost
    }
    Binding {
        target: host.hero
        property: "height"
        value: adaptive.heroHeight
        when: host.androidHost
    }
    Binding {
        target: host.hero
        property: "radius"
        value: adaptive.phone ? 16 : 20
        when: host.androidHost
    }
    Binding {
        target: host.hallDoor
        property: "visible"
        value: !adaptive.phone
        when: host.androidHost
    }
    Binding {
        target: host.homeBookshelfLoader
        property: "height"
        value: adaptive.phone ? 690 : 400
        when: host.androidHost
    }
    Binding {
        target: host.homeTheatreStripLoader
        property: "height"
        value: adaptive.phone ? 340 : 400
        when: host.androidHost
    }
    Binding {
        target: host.homeReadingDeskLoader
        property: "height"
        value: adaptive.phone ? 560 : 400
        when: host.androidHost
    }
    Binding {
        target: host.homeVaultWidgetLoader
        property: "height"
        value: adaptive.phone ? 440 : 520
        when: host.androidHost
    }

    // ---- Profile tab (Android shell, Slice S3): the list of doors. Its pages (z 56) open over it,
    // so closing one returns to the list. Above every browse/detail page, below the taskbar pages.
    Loader {
        id: profileLayer
        objectName: "profileLayer"
        anchors.fill: parent
        anchors.leftMargin: host.wideWorldNavigationLeftInset
        active: false
        visible: active
        source: "PhoneProfilePage.qml"
        onLoaded: {
            item.rows = Qt.binding(function() { return host.profileRows })
            item.contentBottomInset = Qt.binding(function() { return host.contentBottomInset })
            item.rowRequested.connect(host.openProfileRow)
        }
    }

    // Android phone shell. S1-B owns the opaque bar/inset; S1-C suppresses that browsing
    // chrome whenever the existing immersive-surface authority owns the whole canvas.
    PhoneWorldTabBar {
        id: phoneWorldTabs
        objectName: "phoneWorldTabBar"
        z: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        worlds: host.window ? host.window.worldDefinitions : []
        activeTab: profileLayer.active ? "Profile" : host.worldStack.current
        visible: host.phoneWorldNavigationVisible
        onWorldRequested: (world) => {
            host.closeProfileStack()
            host.window.openWorld(world)
        }
        onProfileRequested: host.openProfilePage()
    }

    // Wide Android shell (landscape tablets / open foldables). It uses the exact same
    // registry and route handlers as the narrow bar; rotation merely changes which shell
    // presentation is visible at the 840 dp breakpoint.
    WideWorldRail {
        id: wideWorldRail
        z: 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        worlds: host.window ? host.window.worldDefinitions : []
        activeTab: profileLayer.active ? "Profile" : host.worldStack.current
        visible: host.wideWorldNavigationVisible
        onWorldRequested: (world) => {
            host.closeProfileStack()
            host.window.openWorld(world)
        }
        onProfileRequested: host.openProfilePage()
    }
}
