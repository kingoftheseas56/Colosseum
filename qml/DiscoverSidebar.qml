// DiscoverSidebar — the rail on the left of every Discover wall, drawn exactly like the
// retractable concept's world nav (colosseum-theatre-sidebar-concept (1).html, `#worldnav`).
// It lists the SOURCES only — the addons (and built-in sections) that own the current type's
// catalogues — by icon and name (Hemanth, 2026-09-30: "the sidebar should just be a list of
// addon extensions"). A source's categories live in the category picker (top right) and a
// category's genres/years/languages in the filter picker. One click on a source switches the
// wall to its first category. Everything comes from the browser: no per-world code lives here.
//
// Concept geometry: 240 px open (the concept's 184, widened) / 52 px closed (width eased over 240 ms); shell padding
// 22/16/18/0 with a 1 px fading line on its right edge; "WORLD" kicker + world name; items
// 46 px tall, 12 px radius, icon 19 px + label 14/600 inkDim, the current one lit with a
// .09 plate and a 3 px gold bar with glow. Closed: the heading and labels fade, items become
// 44 px icon buttons. The round 28 px toggle straddles the right edge at top 18.
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
    // 240 open (the concept's 184, widened by Hemanth 2026-09-30 so addon names fit)
    readonly property int openWidth: 240
    readonly property int closedWidth: 52
    width: collapsed ? closedWidth : openWidth
    Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.7, 0.2, 1, 1, 1] } }
    // the shell's right padding (16 open, 4 closed); the list's own right padding (16 / 0)
    readonly property int _shellPadRight: collapsed ? 4 : 16
    readonly property int _listPadRight: collapsed ? 0 : 16
    readonly property bool _labelsShown: width > openWidth - 24

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
    // at least this, so a short wall never crops the list).
    readonly property real naturalHeight: _flow ? Math.min(pinnedHeight, list.y + list.contentHeight + 18) : 0

    x: 0
    height: _flow ? Math.min(pinnedHeight, rail.host.height) : rail.host.height
    y: _flow ? Math.max(0, Math.min(_wantedY, rail.host.height - height)) : 0
    // On its pinned line: the browser's top has scrolled above it and the rail is held there.
    readonly property bool automationSidebarPinned: _flow && _wantedY > 0.5 && Math.abs(y - _wantedY) < 0.5

    // The source names as one string for the bridge (it cannot read a JS array), newline-separated.
    readonly property string automationRows: {
        var out = [], srcs = rail.host.sources
        for (var i = 0; i < srcs.length; i++) out.push(srcs[i].name)
        return out.join("\n")
    }

    readonly property string worldName: {
        var p = rail.host.automationPrefix
        return p.length ? p.charAt(0).toUpperCase() + p.substring(1) : ""
    }

    function _sanitise(key) { return String(key).replace(/[^A-Za-z0-9]/g, "_") }

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
        contentHeight: col.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: list; visible: list.interactive && !rail.collapsed }

        // ── the sources: one .worldnav-item each (icon + name; icon only when closed) ──
        Column {
            id: col
            width: list.width - rail._listPadRight
            spacing: 4

            Text {
                visible: rail.host.sources.length === 0 && !rail.collapsed
                width: parent.width
                leftPadding: 12
                text: rail.host.textNoCatalogue
                wrapMode: Text.WordWrap
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; lineHeight: 1.45
            }

            Repeater {
                model: rail.host.sources
                delegate: Item {
                    id: src
                    required property var modelData
                    readonly property bool current: src.modelData.name === rail.host.currentSource
                    objectName: rail.host.automationPrefix.length
                                ? rail.host.automationPrefix + "DiscoverSource_" + rail._sanitise(src.modelData.name) : ""
                    width: rail.collapsed ? 44 : col.width
                    height: 46

                    // plate: hover .07, current .09 with the gold bar and its glow
                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        color: src.current ? Qt.rgba(1, 1, 1, 0.09)
                             : srcAction.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                        Behavior on color { ColorAnimation { duration: 160 } }
                    }
                    Rectangle {                                  // glow (box-shadow 0 0 18px gold .32)
                        visible: src.current
                        x: (rail.collapsed ? -2 : -1) - 6; y: 4; width: 15; height: parent.height - 8; radius: 7
                        color: Qt.rgba(240/255, 196/255, 74/255, 0.14)
                    }
                    Rectangle {                                  // the gold bar
                        visible: src.current
                        x: rail.collapsed ? -2 : -1; y: 10; width: 3; height: parent.height - 20; radius: 4
                        color: theme.gold
                    }
                    Row {
                        x: rail.collapsed ? (44 - 19) / 2 : 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        // the addon's real icon: bundled logo, else its manifest logo, else its initial
                        AddonLogo {
                            anchors.verticalCenter: parent.verticalCenter
                            size: 19
                            radius: 5
                            opacity: src.current ? 1 : 0.78
                            addonId: src.modelData.addonId
                            addonName: src.modelData.name
                            manifestLogo: src.modelData.logo
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: rail._labelsShown
                            width: col.width - 12 - 19 - 12 - 12
                            // every entry is an addon: "The Movie Database Addon" reads "The Movie Database"
                            text: String(src.modelData.name).replace(/\s+add-?on$/i, "")
                            elide: Text.ElideRight
                            color: (src.current || srcAction.interactionActive) ? theme.ink : theme.inkDim
                            Behavior on color { ColorAnimation { duration: 160 } }
                            font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                    }
                    KeyboardAction {
                        id: srcAction
                        anchors.fill: parent
                        accessibleName: "Source " + src.modelData.name + (src.current ? ", selected" : "")
                        focusRadius: 12
                        focusOnPointer: false   // the gold bar marks the choice; no ring on a click
                        onTriggered: rail.host.selectSource(src.modelData.name)
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
}
