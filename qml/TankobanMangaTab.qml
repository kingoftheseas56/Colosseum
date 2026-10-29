// TankobanMangaTab — the Manga half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the manga rows, sourcing static manga data from Catalog.js and
// emitting the same signals the world forwards to the host. No comics knowledge.
import QtQuick
import "Catalog.js" as Catalog

Item {
    id: mangaTab
    Theme { id: theme }
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    readonly property bool phonePortrait: !!(mangaTab.Window.window && mangaTab.Window.window.androidHost
        && mangaTab.Window.window.width < 600 && mangaTab.Window.window.width < mangaTab.Window.window.height)
    readonly property int portraitPosterWidth: Math.max(64, Math.min(104, Math.floor((mangaTab.width - 24) / 3)))
    readonly property real contentLeft: mangaTab.phonePortrait ? 0 : rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 0
    readonly property var rowIndexRows: [
        { key: "collection", title: "Your Collection", target: collectionRow },
        { key: "top-manga", title: "Top in Tankoban — Manga", target: topMangaRow },
        { key: "genres", title: "Explore by Genre — Manga", target: genreRow }
    ]
    width: mangaTab._availableWidth
    implicitHeight: contentColumn.implicitHeight
    function forceLayout() { contentColumn.forceLayout() }

    RowIndexSidebar {
        id: rowIndex
        parent: mangaTab
        z: 2
        visible: mangaTab.visible && (!mangaTab.phonePortrait || !collapsed)
        compactTouchTargets: mangaTab.phonePortrait
        x: 0
        pageFlick: mangaTab.pageFlick
        flowHost: mangaTab
        backdrop: mangaTab.backdrop
        worldName: "Tankoban"
        automationPrefix: "tankobanRowIndex"
        rows: mangaTab.rowIndexRows
        onRowRequested: (target) => { if (mangaTab.parkRowHandler) mangaTab.parkRowHandler(target) }
        Rectangle {
            anchors.fill: parent
            z: -1
            visible: mangaTab.phonePortrait
            radius: 14
            color: Qt.rgba(0.035, 0.04, 0.06, 0.97)
            MouseArea { anchors.fill: parent }
        }
    }

    signal seriesRequested(string title)
    signal genreRequested(string name)
    signal genreIndexRequested()
    // Bubbles a tap on a Your Collection tile up to the world (manga-filtered).
    signal collectionOpenRequested(var entry)
    // Task 8: a See-all door on a Manga shelf emits a Discover pin. The world switches to
    // Discover and applies it. Pin shape (spec 3.6): {type,catalogId,filterGroup,filterKey}.
    signal discoverPinRequested(var pin)
    signal contentLeftUpdated(real value)
    onContentLeftChanged: mangaTab.contentLeftUpdated(mangaTab.contentLeft)

    Column {
        id: contentColumn
        objectName: "mangaTabContent"
        x: mangaTab.contentLeft
        width: Math.max(0, mangaTab.width - x)
        spacing: mangaTab.phonePortrait ? 24 : 36
        Rectangle {
            visible: mangaTab.phonePortrait
            width: 80; height: 48; radius: 12
            color: theme.glassTint
            border.width: 1; border.color: theme.edge
            Text {
                anchors.centerIn: parent
                text: qsTr("Rows"); color: theme.ink
                font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold
            }
            KeyboardAction {
                anchors.fill: parent
                objectName: "tankobanMangaRowsAction"
                accessibleName: rowIndex.collapsed ? qsTr("Choose catalogue row") : qsTr("Close catalogue rows")
                focusRadius: 12
                onTriggered: rowIndex.collapsed = !rowIndex.collapsed
            }
        }
    ContinueRow {
        id: collectionRow
        title: "Your Collection"
        showSeeAll: false
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "manga" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }

    // "Top in Tankoban — Manga" → Manga / Popular. The header Explore door opens Discover
    // pinned to the Popular manga catalogue. A tile tap still routes to the series page
    // (the existing direct door is unchanged).
    TrendingTop10 {
        compactLayout: mangaTab.phonePortrait
        posterWidthOverride: mangaTab.phonePortrait ? mangaTab.portraitPosterWidth : 0
        id: topMangaRow
        title: "Top in Tankoban — Manga"
        items: Catalog.topManga
        // World-namespaced automation reach (catalogue-independence Slice 3, 2026-08-20):
        // a Lanista scenario needs a scroll-free click path into a real series page — this
        // rail is reachable without touching the Discover wall's un-named, un-scrollable
        // GridView (the Slice-2 documented bridge gap).
        namePrefix: "tankobanTopMangaTile_"
        onItemClicked: (i) => mangaTab.seriesRequested(Catalog.topManga[i].caption)
        onExploreClicked: mangaTab.discoverPinRequested({ type: "manga", catalogId: "popular",
                                                          filterGroup: "", filterKey: "" })
    }

    GenreMosaic {
        id: genreRow
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        // A genre tile opens the existing GenrePage (unchanged direct route). The genre
        // FILTER pin is resolvable by the adapter (the harness verifies it round-trips),
        // and a future per-genre Discover affordance can emit it; attaching it to the tile
        // click here would double-navigate (GenrePage + Discover switch).
        onGenreClicked: (i) => mangaTab.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: mangaTab.genreIndexRequested()
    }
    }
}
