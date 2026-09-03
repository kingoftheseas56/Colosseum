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
import QtQuick.Window

Item {
    id: world

    property Item backdrop                    // the persistent wallpaper (set post-load by the host; Glass is null-safe)
    property string medium: ""               // which library pill reads as selected
    // Main binds this to the current world. Bare page harnesses keep the default true, while
    // retained hidden worlds can stop timers, paging and refresh work without being destroyed.
    property bool lifecycleActive: true
    // Host/layout seam shared by Tankoban, Biblio and Theatre. Writable for desktop viewport tests.
    property bool androidHost: Qt.platform.os === "android"
    AdaptiveLayout { id: adaptive; viewportWidth: world.width }
    readonly property real pageMargin: adaptive.pageMargin
    readonly property string layoutClass: adaptive.layoutClass
    // The global Explicit Content preference, threaded in by Main's world-loader onLoaded
    // (Task 7 Step 4). Worlds that own a Discover wall (Tankoban now; Theatre/Biblio via
    // Task 9) read this to drive the sexually-explicit-only gate. Default false so a bare
    // construct (the page harness, a cold world) stays conservative.
    property bool showExplicitContent: false
    readonly property bool televisionMode: {
        const w = world.Window.window
        return !!(w && w["televisionMode"] === true)
    }
    default property alias content: board.data

    function isInside(item, ancestor) {
        var p = item
        while (p) {
            if (p === ancestor) return true
            p = p.parent
        }
        return false
    }
    function revealFocusedItem(item) {
        if (!item || !world.isInside(item, page)) return
        var point = item.mapToItem(page, 0, 0)
        if (point.y < 18)
            page.contentY = Math.max(0, page.contentY + point.y - 18)
        else if (point.y + item.height > page.height - 18)
            page.contentY = Math.min(Math.max(0, page.contentHeight - page.height),
                                     page.contentY + point.y + item.height - page.height + 18)
    }
    function moveVerticalFocus(forward) {
        const w = world.Window.window
        const from = w ? w.activeFocusItem : null
        if (!from || !world.isInside(from, world)) return false
        var target = from.nextItemInFocusChain(forward)
        var guard = 0
        while (target && target !== from && guard++ < 96) {
            if (target.visible && target.enabled && target.activeFocusOnTab) {
                target.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
                world.revealFocusedItem(target)
                return true
            }
            target = target.nextItemInFocusChain(forward)
        }
        return false
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => {
        if (!world.televisionMode) return
        if (event.key === Qt.Key_Up)
            event.accepted = world.moveVerticalFocus(false)
        else if (event.key === Qt.Key_Down)
            event.accepted = world.moveVerticalFocus(true)
    }

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
        androidHost: world.androidHost
        x: adaptive.pageMargin; y: adaptive.topInset
        width: world.width - adaptive.pageMargin * 2
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

    Component.onCompleted: if (world.televisionMode && world.visible)
        Qt.callLater(function() { topbar.focusFirst() })
    onVisibleChanged: if (world.televisionMode && world.visible)
        Qt.callLater(function() { topbar.focusFirst() })

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
        y: topbar.y + topbar.height + (world.televisionMode ? 18 : (adaptive.compactChrome ? 8 : 10))
        height: world.height - y
        contentWidth: width
        contentHeight: board.implicitHeight + 50
        clip: true
        pixelAligned: false
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: board
            x: adaptive.pageMargin
            width: world.width - adaptive.pageMargin * 2
            topPadding: 12; bottomPadding: 24
            spacing: adaptive.sectionSpacing
        }
    }

    ScrollGlide { flick: page }
}
