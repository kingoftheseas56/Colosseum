// UniversesWorld — the fourth world: every universe that has a working page today, as the
// ratified LEDGER STACK (Hemanth 2026-07-12) re-housed in the shared world shell. Nothing
// here is new design: WorldPage owns the top bar, the ONE page scroller and keyboard
// routing; the pile is the Hall of Worlds' own UniverseLedgerBar, one per
// Universes.worldRoster entry — the installed roster first, then the curated doors whose
// template pages exist (saga · eras · studio · anime). Still never a tile grid (standing
// constraint). A bar's click or Enter emits universeRequested with the roster entry; Main
// routes it (openRosterUniverse: installed → openUniverse, curated → openArchivedUniverse).
import QtQuick
import "Universes.js" as Universes

WorldPage {
    id: universes
    objectName: "universesWorld"          // automation identity, like theatreWorld
    medium: "Universes"
    initialFocusName: "universesLedger"   // the first D-pad arrow lands the pile

    // the shell sets this (Main's world-loader binds it to installedUniverses):
    // { extensionId, name, banner, logo } per installed universe
    property var installed: []
    // a bar → the shell: installed entries carry extensionId, curated ones a page
    signal universeRequested(var entry)

    readonly property var roster: Universes.worldRoster(universes.installed)
    onRosterChanged: {
        universes.hovered = -1
        if (pile.currentIndex >= universes.roster.length)
            pile.currentIndex = universes.roster.length > 0 ? universes.roster.length - 1 : -1
    }

    // the breathe goes STILL while the page moves (the Hall's walking rule, Hemanth
    // 2026-07-13: bars ballooning under a stationary cursor shoved the content around
    // mid-scroll). Any contentY motion closes the open bar and holds the pile flat until
    // the walk settles.
    property int hovered: -1              // which bar breathes (-1 = the pile at rest)
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

    WidgetHeader {
        title: "Universes"
        navigable: false          // the pile below IS the destination; no deeper page
    }

    // ---- THE STACK — the books lie flat, one world under another ----
    Column {
        id: pile
        objectName: "universesLedger"     // one keyboard focus region (the Hall's own law)
        width: parent.width
        spacing: 8

        property int currentIndex: universes.roster.length > 0 ? 0 : -1
        focusPolicy: universes.roster.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => ledgerNav.handle(event)

        Repeater {
            id: ledgerRepeater
            model: universes.roster
            delegate: UniverseLedgerBar {
                id: bar
                required property var modelData
                required property int index
                entry: bar.modelData
                numeral: bar.index
                open: universes.hovered === bar.index
                      || (pile.activeFocus && pile.currentIndex === bar.index)
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
            count: universes.roster.length
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
                if (index >= 0 && index < universes.roster.length)
                    universes.universeRequested(universes.roster[index])
            }
        }
    }
}
