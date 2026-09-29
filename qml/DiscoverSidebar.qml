// DiscoverSidebar — the rail on the left of every Discover wall, drawn exactly like the
// retractable concept's world nav (colosseum-theatre-sidebar-concept (1).html, `#worldnav`),
// holding the current type's catalogues grouped by source (Hemanth, 2026-09-29: "mock look,
// catalogues in rail"). One click switches the wall. The type selector stays above the wall.
// Everything comes from the browser's adapter: no per-world code lives here.
//
// Concept geometry: 184 px open / 52 px closed (width eased over 240 ms); shell padding
// 22/16/18/0 with a 1 px fading line on its right edge; "WORLD" kicker + world name; items
// 46 px tall, 12 px radius, icon 19 px + label 14/600 inkDim, the current one lit with a
// .09 plate and a 3 px gold bar with glow. Closed: the heading fades, items become 44 px icon
// buttons (one per source). The round 28 px toggle straddles the right edge at top 18.
// Every Discover starts closed; the choice is not remembered.
//
// Pinned: in page flow the rail tracks the world page's viewport (the concept's rail sits 12
// below the board's top edge), and it rides up with the end of the wall. It is never a second
// scroller for the wall; only its own list scrolls when taller than the window.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: rail

    required property DiscoverBrowser host
    property Item backdrop: null

    objectName: rail.host.automationPrefix.length ? rail.host.automationPrefix + "DiscoverSidebar" : ""

    Theme { id: theme }

    // ── open / closed ──
    property bool collapsed: true
    readonly property int openWidth: 184
    readonly property int closedWidth: 52
    width: collapsed ? closedWidth : openWidth
    Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.7, 0.2, 1, 1, 1] } }
    // the shell's right padding (16 open, 4 closed); the list's own right padding (16 / 0)
    readonly property int _shellPadRight: collapsed ? 4 : 16
    readonly property int _listPadRight: collapsed ? 0 : 16

    // ── pinned geometry (against the world page viewport) ──
    // The concept's rail top is 12 below the board's top edge; the docked tab bar moves right
    // of it (WorldPage.dockContentLeft).
    readonly property real pinLine: 12
    // It stops 12 above the taskbar's Colosseum button (Taskbar.qml: 16 gap + 64 button),
    // which sits in the bottom-left corner the rail shares (the concept has no taskbar).
    readonly property real bottomGap: 16 + 64 + 12
    readonly property bool _flow: rail.host.pageFlow
    readonly property real _browserTop: {
        if (!_flow) return 0
        rail.host.pageFlick.contentY; rail.host.pageFlick.contentHeight; rail.host.y   // dependencies
        return rail.host.mapToItem(rail.host.pageFlick, 0, 0).y
    }
    // Fixed height (the pinned height); before it pins, its lower part sits below the fold.
    readonly property real pinnedHeight: _flow ? Math.max(160, rail.host.pageFlick.height - pinLine - bottomGap) : 0
    readonly property real _wantedY: _flow ? Math.max(0, pinLine - _browserTop) : 0
    // How tall the rail wants to be, independent of the browser's height (the browser grows to
    // at least this, so a short wall never crops the catalogue list).
    readonly property real naturalHeight: _flow ? Math.min(pinnedHeight, list.y + list.contentHeight + 18) : 0

    x: 0
    height: _flow ? Math.min(pinnedHeight, rail.host.height) : rail.host.height
    y: _flow ? Math.max(0, Math.min(_wantedY, rail.host.height - height)) : 0
    // On its pinned line: the browser's top has scrolled above it and the rail is held there.
    readonly property bool automationSidebarPinned: _flow && _wantedY > 0.5 && Math.abs(y - _wantedY) < 0.5

    // ── model: the current type's catalogues grouped by source ──
    // Extension catalogues group under their addon (attribution); built-ins under their section.
    readonly property var groups: {
        var _ = rail.host.adapterRev
        var cats = (rail.host.adapter && rail.host.currentType.length) ? rail.host.adapter.catalogs(rail.host.currentType) : []
        var order = [], byGroup = {}
        for (var i = 0; i < cats.length; i++) {
            var c = cats[i]
            var g = (c.sourceKind === "extension" ? c.attribution : c.section) || c.attribution || c.section || ""
            if (!byGroup[g]) { byGroup[g] = []; order.push(g) }
            byGroup[g].push({ key: c.key, title: c.title, group: g })
        }
        var out = []
        for (var o = 0; o < order.length; o++) out.push({ name: order[o], rows: byGroup[order[o]] })
        return out
    }
    // Flat rows for the open list: { header } then { key, title, group } per catalogue.
    readonly property var rows: {
        var out = []
        for (var g = 0; g < groups.length; g++) {
            out.push({ header: groups[g].name })
            for (var r = 0; r < groups[g].rows.length; r++) out.push(groups[g].rows[r])
        }
        return out
    }

    // The rows as one string for the bridge (it cannot read a JS array): "#Source" per header,
    // the catalogue key per row, newline-separated.
    readonly property string automationRows: {
        var out = []
        for (var i = 0; i < rows.length; i++)
            out.push(rows[i].header !== undefined ? "#" + rows[i].header : rows[i].key)
        return out.join("\n")
    }

    readonly property string worldName: {
        var p = rail.host.automationPrefix
        return p.length ? p.charAt(0).toUpperCase() + p.substring(1) : ""
    }

    function _sanitise(key) { return String(key).replace(/[^A-Za-z0-9]/g, "_") }
    function _groupHoldsCurrent(group) {
        for (var i = 0; i < group.rows.length; i++)
            if (group.rows[i].key === rail.host.currentCatalogKey) return true
        return false
    }
    // Open the rail at one source's group (a logo in the closed strip).
    function openAt(groupName) {
        rail.collapsed = false
        Qt.callLater(function() {
            for (var i = 0; i < rail.rows.length; i++) {
                var it = openRepeater.itemAt(i)
                if (it && rail.rows[i].header === groupName) {
                    list.contentY = Math.max(0, Math.min(it.y, list.contentHeight - list.height))
                    return
                }
            }
        })
    }

    // the shell's right edge: a 1 px line fading in and out (.worldnav-shell:after)
    Rectangle {
        x: rail.width - 1
        y: 2
        width: 1
        height: rail.height - 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.10; color: Qt.rgba(1, 1, 1, 0.18) }
            GradientStop { position: 0.86; color: Qt.rgba(1, 1, 1, 0.10) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // ── heading: "WORLD" kicker + the world's name (fades out when closed) ──
    Item {
        id: brand
        x: 12
        y: 22
        width: rail.openWidth - 16 - 12 - 16
        height: rail.collapsed ? 38 : brandColumn.implicitHeight + 2 + 18
        clip: true
        opacity: rail.collapsed ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column {
            id: brandColumn
            y: 2
            spacing: 5
            Text {
                text: "WORLD"
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2.8
            }
            Text {
                text: rail.worldName
                color: theme.ink
                font.family: theme.display; font.pixelSize: 27; font.letterSpacing: -0.54
            }
        }
    }

    Flickable {
        id: list
        x: 0
        y: brand.y + brand.height
        width: rail.width - rail._shellPadRight
        height: rail.height - y - 18
        contentHeight: rail.collapsed ? strip.implicitHeight : col.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: list; visible: list.interactive && !rail.collapsed }

        // ── closed: one 44 px icon button per source; the source holding the current catalogue is lit ──
        Column {
            id: strip
            visible: rail.collapsed
            width: 44
            spacing: 4
            Repeater {
                model: rail.groups
                delegate: Item {
                    id: src
                    required property var modelData
                    readonly property bool current: rail._groupHoldsCurrent(src.modelData)
                    objectName: rail.host.automationPrefix.length
                                ? rail.host.automationPrefix + "DiscoverSidebarSource_" + rail._sanitise(src.modelData.name) : ""
                    width: 44
                    height: 46
                    NavItemPlate { anchors.fill: parent; on: src.current; hot: srcAction.interactionActive; barX: -2 }
                    AddonLogo {
                        anchors.centerIn: parent
                        size: 19
                        radius: 5
                        opacity: src.current ? 1 : 0.78
                        addonName: src.modelData.name
                    }
                    KeyboardAction {
                        id: srcAction
                        anchors.fill: parent
                        accessibleName: "Open the sidebar at " + src.modelData.name
                        focusRadius: 12
                        focusOnPointer: false
                        onTriggered: rail.openAt(src.modelData.name)
                    }
                }
            }
        }

        // ── open: a small source heading, then one item per catalogue ──
        Column {
            id: col
            visible: !rail.collapsed
            opacity: rail.width > rail.openWidth - 24 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            width: list.width - rail._listPadRight
            spacing: 4

            Text {
                visible: rail.rows.length === 0
                width: parent.width
                leftPadding: 12
                text: rail.host.textNoCatalogue
                wrapMode: Text.WordWrap
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; lineHeight: 1.45
            }

            Repeater {
                id: openRepeater
                model: rail.rows
                delegate: Item {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool isHeader: entry.modelData.header !== undefined
                    readonly property bool current: !entry.isHeader && entry.modelData.key === rail.host.currentCatalogKey
                    objectName: (entry.isHeader || !rail.host.automationPrefix.length) ? ""
                                : rail.host.automationPrefix + "DiscoverCatalog_" + rail._sanitise(entry.modelData.key)
                    width: col.width
                    height: entry.isHeader ? (entry.index === 0 ? 18 : 30) : 46

                    Text {                                    // source heading
                        visible: entry.isHeader
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                        text: entry.isHeader ? entry.modelData.header : ""
                        elide: Text.ElideRight
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 10
                        font.letterSpacing: 2.8; font.capitalization: Font.AllUppercase
                    }

                    // the concept's .worldnav-item: plate, gold bar, icon 19 + label 14/600
                    NavItemPlate { visible: !entry.isHeader; anchors.fill: parent; on: entry.current; hot: rowAction.interactionActive }
                    Row {
                        visible: !entry.isHeader
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.right: parent.right; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        AddonLogo {
                            anchors.verticalCenter: parent.verticalCenter
                            size: 19
                            radius: 5
                            opacity: entry.current ? 1 : 0.78
                            addonName: entry.isHeader ? "" : entry.modelData.group
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 19 - 12
                            text: entry.isHeader ? "" : entry.modelData.title
                            elide: Text.ElideRight
                            color: (entry.current || rowAction.interactionActive) ? theme.ink : theme.inkDim
                            Behavior on color { ColorAnimation { duration: 160 } }
                            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                    }
                    KeyboardAction {
                        id: rowAction
                        visible: !entry.isHeader
                        anchors.fill: parent
                        accessibleName: entry.isHeader ? ""
                            : "Catalogue " + entry.modelData.title + ", " + entry.modelData.group
                              + (entry.current ? ", selected" : "")
                        focusRadius: 12
                        focusOnPointer: false   // the gold bar marks the choice; no ring on a click
                        onTriggered: if (!entry.current) rail.host.selectCatalog(entry.modelData.key)
                    }
                }
            }
        }
    }

    // ── the edge toggle (.worldnav-toggle): 28 px round, right:-14, top:18, blurred dark plate ──
    Item {
        id: toggle
        objectName: rail.host.automationPrefix.length ? rail.host.automationPrefix + "DiscoverSidebarCollapse" : ""
        x: rail.width - 14
        y: 18
        width: 28; height: 28
        opacity: toggleAction.interactionActive ? 1 : 0.74
        Glass {
            anchors.fill: parent
            backdrop: rail.backdrop
            radius: 14
            tint: toggleAction.interactionActive ? 0.10 : 0.0
            scrim: toggleAction.interactionActive ? 0.0 : 0.46
            blurMax: 16
            edge: Qt.rgba(1, 1, 1, 0.16)
            track: rail._flow ? rail.host.pageFlick.contentY : 0
        }
        // chevron pointing left (retract); it turns 180° when the rail is closed
        Item {
            anchors.centerIn: parent
            width: 15; height: 15
            rotation: rail.collapsed ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 240; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.7, 0.2, 1, 1, 1] } }
            Rectangle { x: 5; y: 6.6; width: 6; height: 1.8; radius: 0.9; rotation: -45; transformOrigin: Item.Left
                        color: toggleAction.interactionActive ? theme.ink : theme.inkDim; antialiasing: true }
            Rectangle { x: 5; y: 6.6; width: 6; height: 1.8; radius: 0.9; rotation: 45; transformOrigin: Item.Left
                        color: toggleAction.interactionActive ? theme.ink : theme.inkDim; antialiasing: true }
        }
        KeyboardAction {
            id: toggleAction
            anchors.fill: parent
            accessibleName: rail.collapsed ? "Expand sidebar" : "Retract sidebar"
            focusRadius: 14
            focusOnPointer: false
            onTriggered: rail.collapsed = !rail.collapsed
        }
    }

    // one plate for both item kinds: hover .07, current .09 with the gold bar and its glow
    component NavItemPlate: Item {
        id: plate
        property bool on: false
        property bool hot: false
        property real barX: -1
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: plate.on ? Qt.rgba(1, 1, 1, 0.09) : plate.hot ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
            Behavior on color { ColorAnimation { duration: 160 } }
        }
        Rectangle {                                          // glow (box-shadow 0 0 18px gold .32)
            visible: plate.on
            x: plate.barX - 6; y: 4; width: 15; height: plate.height - 8; radius: 7
            color: Qt.rgba(240/255, 196/255, 74/255, 0.14)
        }
        Rectangle {                                          // the gold bar
            visible: plate.on
            x: plate.barX; y: 10; width: 3; height: plate.height - 20; radius: 4
            color: theme.gold
        }
    }
}
