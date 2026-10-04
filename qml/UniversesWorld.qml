// UniversesWorld — the fourth world. Unlike the other three it is not an endless catalogue:
// the set is finite and curated, so the page is two things (Hemanth 2026-10-04):
//   · a carousel of TRIBUTES — the hand-made art pages;
//   · every universe that has a working page, as a large full-width BANNER (never a tile),
//     under one row of filter chips (by what a universe holds). No tabs: a short curated
//     list split across tabs only looks emptier.
// WorldPage owns the top bar, the ONE page scroller and keyboard routing. A banner's click
// or Enter emits universeRequested with the roster entry; Main routes it to that universe's
// standard page (openRosterUniverse). The carousel emits tributeRequested for the art page.
import QtQuick
import "Universes.js" as Universes

WorldPage {
    id: universes
    objectName: "universesWorld"          // automation identity, like theatreWorld
    medium: "Universes"
    initialFocusName: "universesLedger"   // the first D-pad arrow lands the pile

    Theme { id: theme }

    // the shell sets this (Main's world-loader binds it to installedUniverses):
    // { extensionId, name, banner, logo } per installed universe
    property var installed: []
    // a banner → the shell: installed entries carry extensionId, curated ones a page
    signal universeRequested(var entry)
    // the carousel → the shell: the hand-made art page of an installed universe
    signal tributeRequested(var entry)

    readonly property var tributes: Universes.tributes(universes.installed)

    readonly property var roster: Universes.worldRoster(universes.installed)
    // the filter: "" shows everything; otherwise only universes that hold that form
    property string facet: ""
    readonly property var facets: Universes.rosterFacets(universes.roster)
    readonly property var shown: Universes.filterRoster(universes.roster, universes.facet)
    onShownChanged: {
        universes.hovered = -1
        pile.currentIndex = universes.shown.length > 0 ? 0 : -1
    }

    // hover goes STILL while the page moves (the Hall's walking rule, Hemanth 2026-07-13):
    // a stationary cursor must not light banner after banner as the page scrolls under it.
    property int hovered: -1              // which banner the pointer is on (-1 = none)
    property bool walking: false
    Timer { id: walkSettle; interval: 260; onTriggered: universes.walking = false }
    Connections {
        target: universes.pageFlickable
        function onContentYChanged() {
            universes.walking = true
            universes.hovered = -1
            walkSettle.restart()
        }
    }

    // ---- TRIBUTES — the art pages; the second button goes to the same universe's page ----
    FeaturedCarousel {
        objectName: "universesTributeCarousel"
        automationId: "universesTributeCarouselView"
        visible: universes.tributes.length > 0
        kicker: "Tribute"
        primaryLabel: "Enter"
        secondaryLabel: "Universe page"
        slides: universes.tributes.map(function(t) {
            return { id: t.extensionId, title: t.name, blurb: t.blurb, ghost: "",
                     c1: "#14161d", c2: "#0c0e11", art: t.banner }
        })
        onPrimaryClicked: (i) => universes.tributeRequested(universes.tributes[i])
        onSecondaryClicked: (i) => {
            for (let r = 0; r < universes.roster.length; ++r)
                if (universes.roster[r].extensionId === universes.tributes[i].extensionId)
                    universes.universeRequested(universes.roster[r])
        }
    }

    WidgetHeader {
        title: "Universes"
        navigable: false          // the banners below ARE the destination; no deeper page
    }

    // ---- THE FILTER — one row of chips: All, then each form some universe holds ----
    Flow {
        id: chips
        objectName: "universesFilter"
        width: parent.width
        spacing: 8
        visible: universes.facets.length > 1
        Repeater {
            model: [{ key: "", label: "All" }].concat(universes.facets)
            delegate: Rectangle {
                id: chip
                required property var modelData
                readonly property bool active: universes.facet === chip.modelData.key
                objectName: "universesFilter_" + (chip.modelData.key || "all")
                width: chipText.implicitWidth + 32; height: 34; radius: 17
                // a dark pill, like the capsule Back: it sits straight on the wallpaper and must
                // read over a bright pick as well as a dark one
                color: chip.active ? theme.ink : Qt.rgba(0, 0, 0, chipKey.hovered ? 0.58 : 0.42)
                border.width: 1
                border.color: chip.active ? "transparent" : theme.edge
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.modelData.label
                    color: chip.active ? "#090c13" : (chipKey.hovered ? theme.ink : theme.inkDim)
                    font.family: theme.ui; font.pixelSize: 13
                    font.weight: chip.active ? Font.DemiBold : Font.Medium
                }
                KeyboardAction {
                    id: chipKey
                    anchors.fill: parent
                    focusRadius: 17
                    accessibleName: chip.modelData.key ? "Show universes with " + chip.modelData.label
                                                       : "Show all universes"
                    onTriggered: universes.facet = chip.modelData.key
                }
            }
        }
    }

    // ---- THE UNIVERSES — one large banner each, alphabetical ----
    Column {
        id: pile
        objectName: "universesLedger"     // one keyboard focus region (the Hall's own law)
        width: parent.width
        spacing: 12

        property int currentIndex: universes.shown.length > 0 ? 0 : -1
        focusPolicy: universes.shown.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => ledgerNav.handle(event)

        Repeater {
            id: ledgerRepeater
            model: universes.shown
            delegate: UniverseBanner {
                id: bar
                required property var modelData
                required property int index
                entry: bar.modelData
                hot: universes.hovered === bar.index
                keyboardSelected: pile.activeFocus && pile.currentIndex === bar.index
                onHoverEntered: if (!universes.walking && !ledgerNav.keyboardRecentlyMoved)
                                    universes.hovered = bar.index
                onHoverExited: if (universes.hovered === bar.index) universes.hovered = -1
                onActivated: {
                    pile.currentIndex = bar.index
                    universes.universeRequested(bar.modelData)
                }
            }
        }

        // Up/Down walk the pile, Enter opens; an arrow at either end stays unaccepted so
        // WorldPage's spatial router can move focus off the list (the Hall's own contract).
        KeyboardCollectionController {
            id: ledgerNav
            view: pile
            orientation: "vertical"
            count: universes.shown.length
            // the pile scrolls with WorldPage's ONE page scroller — keep the landed bar
            // inside it (the Hall's own reveal math, mapped into the page's contentItem)
            positionIndexFn: function(index) {
                const item = ledgerRepeater.itemAt(index)
                if (!item) return
                const flick = universes.pageFlickable
                const top = item.mapToItem(flick.contentItem, 0, 0).y
                const bottom = top + item.height
                const maxY = Math.max(0, flick.contentHeight - flick.height)
                if (top < flick.contentY) flick.contentY = Math.max(0, top)
                else if (bottom > flick.contentY + flick.height)
                    flick.contentY = Math.min(maxY, bottom - flick.height)
            }
            onActivated: (index) => {
                if (index >= 0 && index < universes.shown.length)
                    universes.universeRequested(universes.shown[index])
            }
        }
    }
}
