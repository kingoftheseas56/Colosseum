// WorldPage — the REUSABLE world-page shell (the doctrine's "lean board of distinct OS-widgets").
// A mode owner (A1 Comics/Manga · A2 Books · A3 Video/Music · A4 Theatre) instantiates THIS and
// declares their own widgets as children; the shell supplies the wallpaper hookup, the top bar
// (this medium's pill selected), page margins, and the vertical scroll. Widgets float as a board
// on the SAME persistent wallpaper — never a self-skinned sub-app.
//
//   WorldPage {
//       medium: "Comics"; backdrop: wall
//       FeaturedCarousel { ... }
//       ContinueRow      { ... }
//       TrendingTop10    { ... }
//       GenreMosaic      { ... }
//   }
//
// Content-row discipline lives with the instantiator: cap at ~two rows (Continue + Trending);
// vary widget TYPES instead of stacking rows.

import QtQuick
import QtQuick.Controls

Item {
    id: world

    // Shared world-shell D-pad router. Child rails/grids keep first claim on arrows;
    // when they hit a boundary, the unaccepted key bubbles here and moves to the
    // nearest visible focus region in that direction (including the pinned TopBar).
    KeyboardSpatialNavigator { id: spatialNav; root: world }
    Keys.onPressed: function(event) {
        // A routed world can receive its first arrow while the world root owns
        // focus. Use that ordinary key to land the declared entry region; the
        // next arrow then follows the region's own collection/scroll contract.
        if (spatialNav.isDirectionalKey(event.key)
                && spatialNav.activeItem() === world
                && world.initialFocusName.length > 0
                && spatialNav.focusNamed(world.initialFocusName)) {
            event.accepted = true
            return
        }
        event.accepted = spatialNav.handle(event)
    }
    Keys.onReleased: function(event) { spatialNav.handleRelease(event) }
    KeyboardSectionCoordinator { id: keyboardSections }

    property Item backdrop                    // the persistent wallpaper (set post-load by the host; Glass is null-safe)
    readonly property var keyboardSectionCoordinator: keyboardSections
    readonly property string automationFocusedObject: spatialNav.automationActiveFocusIdentity
    readonly property bool automationFocusedObjectFullyVisible: spatialNav.automationActiveFocusFullyVisible
    onLifecycleActiveChanged: {
        if (!lifecycleActive)
            keyboardSections.clear()
        else if (visible)
            Qt.callLater(function() { if (world.visible && world.lifecycleActive) world.forceActiveFocus(Qt.TabFocusReason) })
    }
    onVisibleChanged: {
        if (!visible)
            keyboardSections.clear()
        else if (lifecycleActive)
            Qt.callLater(function() { if (world.visible && world.lifecycleActive) world.forceActiveFocus(Qt.TabFocusReason) })
    }
    onEnabledChanged: if (!enabled) keyboardSections.clear()
    property string medium: ""               // which library pill reads as selected
    property string initialFocusName: ""
    // Main binds this to the current world. Bare page harnesses keep the default true, while
    // retained hidden worlds can stop timers, paging and refresh work without being destroyed.
    property bool lifecycleActive: true
    // A world is an ordinary keyboard entry point after the TopBar route opens it.
    // Claim focus on the next event-loop turn so the first D-pad key reaches the
    // world spatial router instead of dying on Main's hidden ignition item.
    focus: visible && lifecycleActive

    Timer {
        id: initialFocusTimer
        interval: 50
        repeat: true
        running: world.visible && world.lifecycleActive && world.initialFocusName.length > 0
        onTriggered: {
            if (spatialNav.focusNamed(world.initialFocusName))
                stop()
        }
    }
    // The global Explicit Content preference, threaded in by Main's world-loader onLoaded
    // (Task 7 Step 4). Worlds that own a Discover wall (Tankoban now; Theatre/Biblio via
    // Task 9) read this to drive the sexually-explicit-only gate. Default false so a bare
    // construct (the page harness, a cold world) stays conservative.
    property bool showExplicitContent: false
    default property alias content: board.data

    signal homeRequested()
    signal mediumSelected(string medium)     // tapped another pill → host switches world
    signal seriesRequested(string title)     // tapped a title tile → host opens its detail page
    signal bookRequested(var book)           // Biblio: tapped a book → host opens the BiblioBook detail
    signal genreRequested(string genreName)  // tapped a genre tile opens its GenrePage
    signal genreIndexRequested()             // tapped a genre widget's "Explore" → host opens the full genre index
    signal continueResumeRequested(var item) // Continue tile's center icon → host resumes the content
    signal continueDetailRequested(var item) // Continue tile elsewhere → host opens its detail view
    signal continueSeeAllRequested()         // Continue row's "See all ›" → host opens the scoped backlog page
    signal searchClicked()
    signal settingsClicked()
    signal accountClicked(real anchorRight, real anchorBottom) // topbar account control -> anchored flyout
    signal wallpaperClicked()
    signal fullscreenClicked()               // topbar fullscreen toggle → host flips the shell (same door as F11)
    signal minimizeClicked()
    signal powerClicked()

    Theme { id: theme }

    // absorb stray clicks so the home underneath never receives them
    MouseArea { anchors.fill: parent }

    // ---- pinned top bar (this medium selected; "‹ Home" shown) ----
    TopBar {
        id: topbar
        backdrop: world.backdrop
        activeMedium: world.medium
        lifecycleActive: world.lifecycleActive
        x: theme.margin; y: 30
        width: world.width - theme.margin * 2
        onHomeRequested: world.homeRequested()
        onMediumSelected: (m) => world.mediumSelected(m)
        onSearchClicked: world.searchClicked()
        onSettingsClicked: world.settingsClicked()
        onAccountClicked: (anchorRight, anchorBottom) =>
            world.accountClicked(anchorRight, anchorBottom)
        onWallpaperClicked: world.wallpaperClicked()
        onFullscreenClicked: world.fullscreenClicked()
        onMinimizeClicked: world.minimizeClicked()
        onPowerClicked: world.powerClicked()
    }

    // Read-only viewport seam for viewport-aware lazy shelves (LazyPosterShelf). These expose the
    // existing page Flickable's scroll offset and height WITHOUT adding a second vertical scroller or
    // touching the scroll controller — WorldPage stays the only vertical scroll owner.
    readonly property real viewportContentY: page.contentY
    readonly property real viewportHeight: page.height

    // ---- the widget board (scrolls vertically) ----
    Flickable {
        id: page
        // Automation identity (Lanista): the production world board is the one vertical
        // scroll owner. World-qualified naming keeps Theatre/Biblio/Tankoban journeys
        // addressable without introducing a second scroller or a presentation shell.
        objectName: world.medium.length > 0 ? world.medium.toLowerCase() + "WorldScroll" : "worldPageScroll"
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: world.height - 96
        contentWidth: width
        contentHeight: board.implicitHeight + 50
        clip: true
        pixelAligned: false
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: board
            x: theme.margin
            width: world.width - theme.margin * 2
            topPadding: 12; bottomPadding: 24
            spacing: 36
        }
    }

    ScrollGlide { id: pageGlide; flick: page }
    // The world shell is the only vertical viewport for its board. Register
    // its keyboard face so nested collection boundaries can spend their
    // directional budget on this viewport before exporting to another route.
    KeyboardScrollController { id: pageKeys; flick: page; glide: pageGlide }
}
