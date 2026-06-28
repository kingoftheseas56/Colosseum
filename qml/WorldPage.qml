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

Item {
    id: world

    property Item backdrop                    // the persistent wallpaper (set post-load by the host; Glass is null-safe)
    property string medium: ""               // which library pill reads as selected
    default property alias content: board.data

    signal homeRequested()
    signal mediumSelected(string medium)     // tapped another pill → host switches world
    signal seriesRequested(string title)     // tapped a title tile → host opens its detail page
    signal bookRequested(var book)           // Biblio: tapped a book → host opens the BiblioBook detail
    signal genreRequested(string genreName)  // tapped a genre tile opens its GenrePage
    signal genreIndexRequested()             // tapped a genre widget's "Explore" → host opens the full genre index
    signal continueResumeRequested(var item) // Continue tile's center icon → host resumes the content
    signal continueDetailRequested(var item) // Continue tile elsewhere → host opens its detail view
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
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
        x: theme.margin; y: 30
        width: world.width - theme.margin * 2
        onHomeRequested: world.homeRequested()
        onMediumSelected: (m) => world.mediumSelected(m)
        onSearchClicked: world.searchClicked()
        onSettingsClicked: world.settingsClicked()
        onWallpaperClicked: world.wallpaperClicked()
        onMinimizeClicked: world.minimizeClicked()
        onPowerClicked: world.powerClicked()
    }

    // ---- the widget board (scrolls vertically) ----
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: world.height - 96
        contentWidth: width
        contentHeight: board.implicitHeight + 50
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: board
            x: theme.margin
            width: world.width - theme.margin * 2
            topPadding: 12; bottomPadding: 24
            spacing: 36
        }
    }
}
