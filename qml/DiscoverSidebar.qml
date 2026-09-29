// DiscoverSidebar — the pinned glass rail on the left of every Discover wall (Discover sidebar
// spec 2026-09-29, plan Slice 2). It lists the world's types (a segmented switch, hidden when
// there is only one) and the current type's catalogues grouped by source; one click switches
// the wall. Everything comes from the browser's adapter: no per-world code lives here.
//
// Pinned: in page flow the rail tracks the world page's viewport, its top held just under the
// docked tab bar while the wall scrolls past, and it rides up with the end of the wall. It is
// never a second scroller for the wall; only its own list scrolls when taller than the window.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: rail

    required property DiscoverBrowser host
    property Item backdrop: null

    objectName: rail.host.automationPrefix.length ? rail.host.automationPrefix + "DiscoverSidebar" : ""

    Theme { id: theme }

    // ── pinned geometry (against the world page viewport, like WorldPage's dock) ──
    // The docked tab bar sits at viewport y 4 and is 50 tall; the rail pins 12 below it.
    readonly property real pinLine: 4 + 50 + 12
    // It stops 12 above the taskbar's Colosseum button (Taskbar.qml: 16 gap + 64 button),
    // which always sits in the bottom-left corner the rail shares.
    readonly property real bottomGap: 16 + 64 + 12
    readonly property bool _flow: rail.host.pageFlow
    readonly property real _browserTop: {
        if (!_flow) return 0
        rail.host.pageFlick.contentY; rail.host.pageFlick.contentHeight; rail.host.y   // dependencies
        return rail.host.mapToItem(rail.host.pageFlick, 0, 0).y
    }
    // Fixed height (the pinned height) so the glass is not re-allocated on every scroll frame;
    // before it pins, its lower part simply sits below the fold.
    readonly property real pinnedHeight: _flow ? Math.max(160, rail.host.pageFlick.height - pinLine - bottomGap) : 0
    readonly property real _wantedY: _flow ? Math.max(0, pinLine - _browserTop) : 0
    // How tall the rail wants to be, independent of the browser's height (the browser grows to
    // at least this, so a short wall never crops the catalogue list).
    readonly property real naturalHeight: _flow ? Math.min(pinnedHeight, list.contentHeight + 2 * pad) : 0

    x: 0
    width: 184
    height: _flow ? Math.min(pinnedHeight, rail.host.height) : rail.host.height
    y: _flow ? Math.max(0, Math.min(_wantedY, rail.host.height - height)) : 0
    // On its pinned line: the browser's top has scrolled above it and the rail is held there.
    readonly property bool automationSidebarPinned: _flow && _wantedY > 0.5 && Math.abs(y - _wantedY) < 0.5

    readonly property int pad: 10

    // ── model: the type list, and the current type's catalogues grouped by source ──
    readonly property var types: {
        var _ = rail.host.adapterRev
        return rail.host.adapter ? rail.host.adapter.types() : []
    }
    // Flat rows for one Repeater: { header, logoName } then { key, title, group } per catalogue.
    // Extension catalogues group under their addon (attribution); built-ins under their section.
    readonly property var rows: {
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
        for (var o = 0; o < order.length; o++) {
            out.push({ header: order[o] })
            for (var r = 0; r < byGroup[order[o]].length; r++) out.push(byGroup[order[o]][r])
        }
        return out
    }

    // The rows as one string for the bridge (it cannot read a JS array): "#Source" per header,
    // the catalogue key per row, newline-separated.
    readonly property string automationRows: {
        var out = []
        for (var i = 0; i < rows.length; i++)
            out.push(rows[i].header !== undefined ? "#" + rows[i].header : rows[i].key)
        return out.join("
")
    }

    function _sanitise(key) { return String(key).replace(/[^A-Za-z0-9]/g, "_") }

    Glass {
        anchors.fill: parent
        backdrop: rail.backdrop
        radius: 18
        track: rail._flow ? rail.host.pageFlick.contentY : 0
    }

    Flickable {
        id: list
        anchors.fill: parent
        anchors.margins: rail.pad
        contentHeight: col.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: list; visible: list.interactive }

        Column {
            id: col
            width: list.width
            spacing: 4

            // ── type switch: a segmented row (hidden when the world has one type) ──
            Rectangle {
                id: typeSwitch
                visible: rail.types.length > 1
                width: parent.width
                height: 36
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    Repeater {
                        model: rail.types
                        delegate: Rectangle {
                            id: seg
                            required property var modelData
                            readonly property bool current: rail.host.currentType === seg.modelData.key
                            objectName: rail.host.automationPrefix.length
                                        ? rail.host.automationPrefix + "DiscoverType_" + seg.modelData.key : ""
                            width: (typeSwitch.width - 6) / Math.max(1, rail.types.length)
                            height: parent.height
                            radius: 9
                            color: seg.current ? Qt.rgba(1, 1, 1, 0.14)
                                 : segAction.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: seg.modelData.label
                                color: seg.current ? theme.ink : (segAction.interactionActive ? theme.ink : theme.inkDim)
                                font.family: theme.ui; font.pixelSize: 13
                                font.weight: seg.current ? Font.DemiBold : Font.Normal
                            }
                            KeyboardAction {
                                id: segAction
                                anchors.fill: parent
                                accessibleName: seg.modelData.label + (seg.current ? ", selected" : "")
                                focusRadius: 9
                                focusOnPointer: false   // the plate marks the choice; no ring on a click
                                onTriggered: rail.host.selectType(seg.modelData.key)
                            }
                        }
                    }
                }
            }
            Item { visible: typeSwitch.visible; width: 1; height: 8 }

            Text {
                visible: rail.rows.length === 0
                width: parent.width
                topPadding: 8; leftPadding: 12; rightPadding: 12
                text: rail.host.textNoCatalogue
                wrapMode: Text.WordWrap
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
            }

            // ── catalogues, grouped by source: a logo header, then one row per catalogue ──
            Repeater {
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
                    height: entry.isHeader ? (entry.index === 0 ? 28 : 38) : 46

                    // group header: the source's logo (or its initial on a round plate) and name
                    Row {
                        visible: entry.isHeader
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                        spacing: 8
                        AddonLogo {
                            size: 18
                            radius: 9
                            addonName: entry.isHeader ? entry.modelData.header : ""
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: col.width - 46
                            text: entry.isHeader ? entry.modelData.header : ""
                            elide: Text.ElideRight
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold
                            font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
                        }
                    }

                    // catalogue row: the concept's worldnav item (46 px, 12 px radius, gold bar when on)
                    Rectangle {
                        visible: !entry.isHeader
                        anchors.fill: parent
                        radius: 12
                        color: entry.current ? Qt.rgba(1, 1, 1, 0.09)
                             : rowAction.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                        Behavior on color { ColorAnimation { duration: 160 } }
                        Rectangle {                              // glow under the gold bar
                            visible: entry.current
                            x: -5; y: 6; width: 11; height: parent.height - 12; radius: 6
                            color: Qt.rgba(240/255, 196/255, 74/255, 0.16)
                        }
                        Rectangle {                              // the gold active bar
                            visible: entry.current
                            x: -1; y: 10; width: 3; height: parent.height - 20; radius: 4
                            color: theme.gold
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 12
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.isHeader ? "" : entry.modelData.title
                            elide: Text.ElideRight
                            color: (entry.current || rowAction.interactionActive) ? theme.ink : theme.inkDim
                            Behavior on color { ColorAnimation { duration: 160 } }
                            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        KeyboardAction {
                            id: rowAction
                            anchors.fill: parent
                            enabled: !entry.isHeader
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
    }
}
