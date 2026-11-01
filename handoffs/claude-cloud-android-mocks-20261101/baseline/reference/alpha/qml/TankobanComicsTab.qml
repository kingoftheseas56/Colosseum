// TankobanComicsTab — the Comics half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the comics rows. Data is passed IN from TankobanWorld (which owns the
// one-time ComicsCatalog.shelf compute + GcApi.explore fetch) so switching tabs never
// re-fetches — the Loader may rebuild this view, but the data is cached upstream and bound
// in reactively. Emits the comics signals the world forwards to the host. No manga knowledge.
import QtQuick

Item {
    id: comicsTab
    Theme { id: theme }
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    readonly property bool phonePortrait: !!(comicsTab.Window.window && comicsTab.Window.window.androidHost
        && comicsTab.Window.window.width < 600 && comicsTab.Window.window.width < comicsTab.Window.window.height)
    readonly property int portraitPosterWidth: Math.max(64, Math.min(104, Math.floor((comicsTab.width - 24) / 3)))
    readonly property real contentLeft: comicsTab.phonePortrait ? 0 : rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 0
    readonly property var rowIndexRows: {
        shelfRepeater.count
        var out = [
            { key: "collection", title: "Your Collection", target: collectionRow },
            { key: "top-comics", title: "Top in Tankoban — Comics", target: topComicsRow }
        ]
        for (var i = 0; i < shelfRepeater.count; i++) {
            var shelf = shelfRepeater.itemAt(i)
            if (shelf && shelf.visible)
                out.push({ key: "shelf-" + i, title: shelf.label, target: shelf })
        }
        out.push({ key: "explore", title: "Explore Comics", target: genreRow })
        return out
    }
    width: comicsTab._availableWidth
    implicitHeight: contentColumn.implicitHeight
    function forceLayout() { contentColumn.forceLayout() }

    RowIndexSidebar {
        id: rowIndex
        parent: comicsTab
        z: 2
        visible: comicsTab.visible && (!comicsTab.phonePortrait || !collapsed)
        compactTouchTargets: comicsTab.phonePortrait
        x: 0
        pageFlick: comicsTab.pageFlick
        flowHost: comicsTab
        backdrop: comicsTab.backdrop
        worldName: "Tankoban"
        automationPrefix: "tankobanRowIndex"
        rows: comicsTab.rowIndexRows
        onRowRequested: (target) => { if (comicsTab.parkRowHandler) comicsTab.parkRowHandler(target) }
        Rectangle {
            anchors.fill: parent
            z: -1
            visible: comicsTab.phonePortrait
            radius: 14
            color: Qt.rgba(0.035, 0.04, 0.06, 0.97)
            MouseArea { anchors.fill: parent }
        }
    }

    property var comicRows: []       // top-comics list (RCO-ranked, from the world)
    property var comicShelves: []    // [{label, rows}] browse shelves
    property var comicBoxes: []      // explore mosaic boxes (GetComics taxonomy)
    property var comicCovers: []     // explore mosaic art pool
    // The world's viewport in this tab's coordinates, bound by TankobanWorld. A height of 0
    // means unknown, and every shelf builds (harnesses, tests).
    property real viewportTop: 0
    property real viewportHeight: 0

    // A newly visible Column child still has y=0 until positioning runs.
    // Coalesce admissions and lay out the column before deciding which shelves
    // are near; otherwise every arriving shelf permanently marks itself built.
    Timer {
        id: shelfBuildTimer
        interval: 0
        onTriggered: {
            comicsTab.forceLayout()
            for (var i = 0; i < shelfRepeater.count; i++) {
                var slot = shelfRepeater.itemAt(i)
                if (slot && slot.visible && slot.near) slot.built = true
            }
        }
    }

    signal westernRequested(string title)
    signal westernExploreRequested(var box)
    signal comicSeriesRequested(var d)
    signal gcdSeriesRequested(var d)
    // Bubbles a tap on a Your Collection tile up to the world (comics-filtered).
    signal collectionOpenRequested(var entry)
    // Task 8: a See-all door on a Comics shelf emits a Discover pin. The world switches to
    // Discover and applies it. Pin shape (spec 3.6): {type,catalogId,filterGroup,filterKey}.
    signal discoverPinRequested(var pin)
    signal contentLeftUpdated(real value)
    onContentLeftChanged: comicsTab.contentLeftUpdated(comicsTab.contentLeft)

    Column {
        id: contentColumn
        objectName: "comicsTabContent"
        x: comicsTab.contentLeft
        width: Math.max(0, comicsTab.width - x)
        spacing: comicsTab.phonePortrait ? 24 : 36
        Rectangle {
            visible: comicsTab.phonePortrait
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
                objectName: "tankobanComicsRowsAction"
                accessibleName: rowIndex.collapsed ? qsTr("Choose catalogue row") : qsTr("Close catalogue rows")
                focusRadius: 12
                onTriggered: rowIndex.collapsed = !rowIndex.collapsed
            }
        }
    ContinueRow {
        id: collectionRow
        title: "Your Collection"
        showSeeAll: false
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "comic" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }

    // "Top in Tankoban — Comics" → Comics / Popular. The Explore wall retired in 2026-07-18
    // for the OLD fetch-all model; Task 8 re-arms it as a Discover pin into the Popular
    // comics catalogue. A tile tap still routes to the LOCG/GCD series door (unchanged).
    TrendingTop10 {
        compactLayout: comicsTab.phonePortrait
        posterWidthOverride: comicsTab.phonePortrait ? comicsTab.portraitPosterWidth : 0
        id: topComicsRow
        title: "Top in Tankoban — Comics"
        items: comicsTab.comicRows.slice(0, 10)
        onItemClicked: (i) => {
            var topComics = comicsTab.comicRows.slice(0, 10)
            var it = topComics[i]
            if (!it) return
            if (it && it.locgId) comicsTab.comicSeriesRequested({ id: it.locgId, title: it.caption, cover: it.cover })
            else comicsTab.westernRequested(it.caption)
        }
        onExploreClicked: comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                                                           filterGroup: "", filterKey: "" })
    }

    // Catalogue shelf rows (browse-landing): Most Stocked, publisher, decade, deep, fan-made.
    // Pinnable shelves (Task 8): Most Stocked → {comics,most-stocked}; Marvel/DC/Image →
    //   {comics,popular,publisher:<lowercase arg>}. Non-pinnable shelves (decade/deep/fanmade)
    //   have no honest Discover filter, so they keep navigable:false and no See-all door.
    // ~15 shelves of up to 24 covers each. Built all at once they froze the first Comics
    // open (~0.8 s, profiled 2026-09-29), so each slot reserves the shelf's exact height and
    // builds its shelf once it comes within a screen of view, then keeps it.
    Repeater {
        id: shelfRepeater
        // Fixed recipe slots survive asynchronous responses. Replacing an array
        // model destroyed every existing shelf (and its image work) on each reply.
        model: comicsTab.comicShelves.length
        delegate: Item {
            id: shelfSlot
            objectName: "tankobanComicsShelf_" + index
            required property int index
            readonly property var modelData: comicsTab.comicShelves[index]
            readonly property string label: modelData ? modelData.label : ""
            width: contentColumn.width
            visible: shelfSlot.modelData.rows.length > 0
            height: shelfLoader.status === Loader.Ready && shelfLoader.item
                ? shelfLoader.item.effectiveRailHeight
                : (rowIndex.phoneLayout ? 48 : 30) + 14
                  + topComicsRow.effectiveStripHeight
            property bool built: false
            readonly property bool near: comicsTab.viewportHeight <= 0
                || (shelfSlot.y < comicsTab.viewportTop + 2 * comicsTab.viewportHeight
                    && shelfSlot.y + shelfSlot.height > comicsTab.viewportTop - comicsTab.viewportHeight)
            onNearChanged: shelfBuildTimer.restart()
            onVisibleChanged: shelfBuildTimer.restart()
            Component.onCompleted: shelfBuildTimer.restart()

            Loader {
                id: shelfLoader
                objectName: "tankobanComicsShelfLoader_" + shelfSlot.index
                width: parent.width
                active: shelfSlot.built && shelfSlot.visible
                asynchronous: true
                sourceComponent: TrendingTop10 {
                    compactLayout: comicsTab.phonePortrait
                    posterWidthOverride: comicsTab.phonePortrait ? comicsTab.portraitPosterWidth : 0
                    readonly property var modelData: shelfSlot.modelData
                    title: modelData.label
                    // Most Stocked and the three publishers are pinnable; everything else is not.
                    navigable: !!modelData.catalogId || modelData.kind === "stocked" || modelData.kind === "publisher"
                    items: modelData.rows
                    onItemClicked: (i) => {
                        var it = modelData.rows[i]
                        if (!it) return
                        if (it.locgId) comicsTab.comicSeriesRequested({ id: it.locgId, title: it.title, cover: it.cover })
                        else comicsTab.gcdSeriesRequested({ gcd: true, gcdId: it.gcdId, title: it.title, cover: it.cover })
                    }
                    onExploreClicked: {
                        if (modelData.catalogId)
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: modelData.catalogId,
                                                            filterGroup: modelData.filterGroup || "",
                                                            filterKey: String(modelData.filterKey || "").toLowerCase() })
                        else if (modelData.kind === "stocked")
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: "most-stocked",
                                                            filterGroup: "", filterKey: "" })
                        else if (modelData.kind === "publisher")
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                                                            filterGroup: "publisher",
                                                            filterKey: String(modelData.arg || "").toLowerCase() })
                    }
                }
            }
        }
    }

    GenreMosaic {
        id: genreRow
        title: "Explore Comics"
        genres: comicsTab.comicBoxes
        covers: comicsTab.comicCovers
        navigable: false
        onGenreClicked: (i) => {
            var box = comicsTab.comicBoxes[i]
            if (box) comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                filterGroup: "genre", filterKey: String(box.name || "").toLowerCase() })
        }
    }
    }
}
