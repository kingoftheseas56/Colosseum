// ExtensionsHouseHero — the Store's top card (agents/colosseum-extensions-world-mock.html): the
// universe banners crossfade behind a glass tray of Colosseum's own House apps. Universes opens the
// Hall, Tankoyomi opens its settings, each House source has its add / on button; Search sits in
// the corner (the Store's top bar carries only Back).
// Tile data and toggles come from ExtensionsHousePage.qml (the full-page House it grew out of).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Window
import ".."

Item {
    id: hero

    property int revision: 0                 // bump when Extensions changes
    signal universesRequested()
    signal searchRequested()
    signal configureRequested(string extensionId)

    readonly property bool compact: width < 1250
    readonly property bool phoneLayout: {
        const w = hero.Window.window
        return !!(w && w.androidHost && w.width < 840)
    }
    readonly property var houseApps: [
        { name: "Universes", id: "", icon: "../../assets/icons/universes-atom.svg", kind: "universes" },
        { name: "Grand Database", id: "", icon: "../../assets/addon-logos/colosseum-grand-database.png", kind: "builtin" },
        { name: "Nyaa", id: "colosseum.well.nyaa", icon: "../../assets/addon-logos/nyaa.png", kind: "source" },
        { name: "Tankoyomi", id: "colosseum.well.tankoyomi", icon: "../../assets/addon-logos/tankoyomi.png", kind: "source" },
        { name: "GetComics", id: "colosseum.well.getcomics.issues", icon: "../../assets/addon-logos/getcomics.png", kind: "source" },
        { name: "Tankorent", id: "colosseum.well.indexers", icon: "../../assets/addon-logos/tankorent.png", kind: "source" },
        { name: "LibGen", id: "colosseum.well.libgen", icon: "../../assets/addon-logos/libgen.ico", kind: "source" },
        { name: "AudioBookBay", id: "colosseum.well.audiobookbay", icon: "../../assets/addon-logos/audiobookbay.png", kind: "source" }
    ]
    readonly property real sourceToggleMinimumTouchTarget: 48
    property int universeIndex: 0

    width: parent ? parent.width : 1400
    height: phoneLayout ? mobileHouseList.y + mobileHouseList.implicitHeight + 8 : 330

    Theme { id: theme }

    function rowFor(id) {
        if (!id.length || typeof Extensions === "undefined") return null
        var rows = Extensions.installed()
        for (var i = 0; i < rows.length; ++i)
            if (rows[i] && String(rows[i].id || "") === id) return rows[i]
        return null
    }
    function isInstalled(id) { hero.revision; var r = hero.rowFor(id); return !!r && r.enabled !== false }
    function isPresent(id) { hero.revision; return hero.rowFor(id) !== null }
    function toggle(id) {
        if (hero.isPresent(id)) Extensions.setEnabled(id, !hero.isInstalled(id))
    }

    Timer {
        interval: 7500; repeat: true
        running: hero.visible && !hero.phoneLayout
        onTriggered: hero.universeIndex = (hero.universeIndex + 1) % banners.count
    }

    Rectangle {
        id: frame
        visible: !hero.phoneLayout
        anchors.fill: parent
        radius: 28
        color: "#0b0d12"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: frameMask
        }

        Item {
            id: wallpaperLayer
            anchors.fill: parent
            Repeater {
                id: banners
                model: ["../../assets/extensions/house/starwars-banner.jpg",
                        "../../assets/extensions/house/dcau.jpg",
                        "../../assets/extensions/house/cosmere.jpg"]
                delegate: Image {
                    required property string modelData
                    required property int index
                    anchors.fill: parent
                    source: modelData
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    opacity: hero.universeIndex === index ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 1050; easing.type: Easing.InOutCubic } }
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.00; color: Qt.rgba(0, 0, 0, 0.30) }
                GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.05) }
                GradientStop { position: 0.62; color: Qt.rgba(0, 0, 0, 0.14) }
                GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.52) }
            }
        }
    }
    Item {
        id: frameMask
        anchors.fill: frame
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: 28; color: "black" }
    }

    // ---- Search: the Store's add-on search ----
    Rectangle {
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 26; anchors.topMargin: hero.phoneLayout ? 6 : 20
        width: searchRow.implicitWidth + 36; height: 48
        radius: 24
        color: Qt.rgba(15 / 255, 16 / 255, 22 / 255, 0.78)
        border.width: 1
        border.color: searchAction.interactionActive ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
        Row {
            id: searchRow
            anchors.centerIn: parent
            spacing: 8
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 15; height: 15
                source: "../../assets/icons/search.svg"
                sourceSize.width: 30; sourceSize.height: 30
                opacity: searchAction.interactionActive ? 1 : 0.78
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Search"
                color: searchAction.interactionActive ? theme.ink : Qt.rgba(1, 1, 1, 0.78)
                font.family: theme.ui; font.pixelSize: theme.fontSize(14)
            }
        }
        KeyboardAction {
            id: searchAction
            objectName: "extensionsSearchButton"
            anchors.fill: parent
            accessibleName: "Search add-ons"
            focusRadius: 17
            onTriggered: hero.searchRequested()
        }
    }

    // Narrow host: readable rows, with the switch isolated from the row action.
    Column {
        id: mobileHouseList
        visible: hero.phoneLayout
        y: 58
        width: parent.width
        spacing: 0

        Repeater {
            model: hero.houseApps
            delegate: Item {
                id: mobileRow
                required property var modelData
                readonly property bool hasToggle: modelData.kind === "source"
                readonly property bool installed: hasToggle && hero.isInstalled(modelData.id)
                width: mobileHouseList.width
                height: 60

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.12)
                }
                KeyboardAction {
                    objectName: "houseRow_" + (mobileRow.modelData.id || mobileRow.modelData.kind)
                    anchors.fill: parent
                    enabled: mobileRow.modelData.kind === "universes"
                             || mobileRow.modelData.id === "colosseum.well.tankoyomi"
                    accessibleName: mobileRow.modelData.kind === "universes"
                                    ? "Open Universes" : mobileRow.modelData.name + " settings"
                    focusRadius: 8
                    onTriggered: mobileRow.modelData.kind === "universes"
                                 ? hero.universesRequested()
                                 : hero.configureRequested(mobileRow.modelData.id)
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 2
                    spacing: 12
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44; height: 44; radius: 10
                        color: "#f0efeb"
                        Image {
                            anchors.centerIn: parent
                            width: 32; height: 32
                            source: mobileRow.modelData.icon
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, mobileRow.width - 44 - 24 - (mobileRow.hasToggle ? 60 : 0))
                        text: mobileRow.modelData.name
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: theme.fontSize(16); font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                    }
                    Item {
                        visible: mobileRow.hasToggle
                        width: visible ? 48 : 0
                        height: parent.height
                        Rectangle {
                            id: mobileToggle
                            objectName: "houseToggle_" + mobileRow.modelData.id
                            readonly property bool checked: mobileRow.installed
                            anchors.centerIn: parent
                            width: 42; height: 28; radius: 14
                            color: mobileRow.installed ? theme.gold : "#454953"
                            opacity: hero.isPresent(mobileRow.modelData.id) ? 1 : 0.5
                            Rectangle {
                                x: mobileRow.installed ? parent.width - width - 4 : 4
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20; height: 20; radius: 10
                                color: mobileRow.installed ? "#0b0d12" : "white"
                            }
                        }
                        KeyboardAction {
                            anchors.fill: parent
                            accessibleName: (mobileRow.installed ? "Turn off " : "Turn on ")
                                            + mobileRow.modelData.name
                            enabled: hero.isPresent(mobileRow.modelData.id)
                            focusRadius: 14
                            onTriggered: hero.toggle(mobileRow.modelData.id)
                        }
                    }
                }
            }
        }
    }

    // ---- the glass tray of House apps ----
    Item {
        id: tray
        visible: !hero.phoneLayout
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 22
        width: parent.width * 0.94
        height: 174

        Glass {
            anchors.fill: parent
            backdrop: wallpaperLayer
            radius: 31
            tint: 0.0; scrim: 0.0
            blurAmount: 1.0; blurMax: 34; blurMultiplier: 0.0
            edge: Qt.rgba(1, 1, 1, 0.30)
        }
        Rectangle {
            anchors.fill: parent
            radius: 31
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(236 / 255, 229 / 255, 216 / 255, 0.32) }
                GradientStop { position: 1; color: Qt.rgba(220 / 255, 212 / 255, 198 / 255, 0.13) }
            }
        }

        Row {
            id: appsRow
            anchors.centerIn: parent
            spacing: hero.compact ? 8 : 15
            scale: Math.min(1, (tray.width - 40) / Math.max(1, implicitWidth))

            Repeater {
                model: hero.houseApps
                delegate: Item {
                    id: tile
                    required property var modelData
                    readonly property bool installed: tile.modelData.kind !== "source" || hero.isInstalled(tile.modelData.id)
                    readonly property bool lit: tileAction.interactionActive
                    width: hero.compact ? 122 : 136
                    height: 140

                    Rectangle {
                        id: appTile
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: tile.lit ? -4 : 0
                        width: hero.compact ? 82 : 98; height: width
                        radius: 19
                        color: Qt.rgba(250 / 255, 250 / 255, 248 / 255, 0.97)
                        scale: tile.lit ? 1.03 : 1
                        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Qt.rgba(0, 0, 0, tile.lit ? 0.35 : 0.25)
                            shadowVerticalOffset: tile.lit ? 20 : 12
                            shadowBlur: 0.85
                            autoPaddingEnabled: true
                        }
                        Image {
                            anchors.centerIn: parent
                            width: parent.width * 0.58; height: width
                            source: tile.modelData.icon
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        KeyboardAction {
                            id: tileAction
                            objectName: "houseTile_" + (tile.modelData.id || tile.modelData.kind)
                            anchors.fill: parent
                            // Universes opens the Hall; Tankoyomi opens its own settings page.
                            enabled: tile.modelData.kind === "universes" || tile.modelData.id === "colosseum.well.tankoyomi"
                            accessibleName: tile.modelData.kind === "universes" ? tile.modelData.name
                                                                                 : tile.modelData.name + " settings"
                            focusRadius: 19
                            onTriggered: tile.modelData.kind === "universes" ? hero.universesRequested()
                                                                             : hero.configureRequested(tile.modelData.id)
                        }
                    }
                    // Name chip, and for a House source its own add / on button beside it, with room.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        spacing: 8
                        Rectangle {
                            width: Math.min(tile.modelData.kind === "source" ? 100 : 112, label.implicitWidth + 20); height: 28
                            radius: 11
                            color: Qt.rgba(25 / 255, 26 / 255, 29 / 255, 0.28)
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.16)
                            Text {
                                id: label
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - 16)
                                text: tile.modelData.name
                                color: Qt.rgba(1, 1, 1, 0.86)
                                font.family: theme.ui; font.pixelSize: theme.fontSize(11)
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            objectName: tile.modelData.kind === "source" ? "houseToggle_" + tile.modelData.id : ""
                            readonly property bool checked: tile.installed   // automation reads this
                            visible: tile.modelData.kind === "source"
                            width: 28; height: 28; radius: 14
                            y: toggleAction.interactionActive ? -2 : 0
                            color: tile.installed ? Qt.rgba(40 / 255, 120 / 255, 82 / 255, 0.82) : Qt.rgba(18 / 255, 19 / 255, 24 / 255, 0.72)
                            border.width: 1
                            border.color: tile.installed ? Qt.rgba(159 / 255, 1, 210 / 255, 0.55) : Qt.rgba(1, 1, 1, 0.34)
                            opacity: hero.isPresent(tile.modelData.id) ? 1 : 0.5
                            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            Text {
                                anchors.centerIn: parent
                                text: tile.installed ? "✓" : "+"
                                color: "white"
                                font.family: theme.ui; font.pixelSize: theme.fontSize(14); font.weight: Font.DemiBold
                            }
                            KeyboardAction {
                                id: toggleAction
                                anchors.centerIn: parent
                                width: Qt.platform.os === "android"
                                       ? Math.max(parent.width, hero.sourceToggleMinimumTouchTarget / Math.max(0.01, appsRow.scale))
                                       : parent.width
                                height: Qt.platform.os === "android"
                                        ? Math.max(parent.height, hero.sourceToggleMinimumTouchTarget / Math.max(0.01, appsRow.scale))
                                        : parent.height
                                focusInset: Qt.platform.os === "android" ? (width - parent.width) / 2 - 2 : -2
                                enabled: hero.isPresent(tile.modelData.id)
                                accessibleName: (tile.installed ? "Turn off " : "Add ") + tile.modelData.name
                                focusRadius: 14
                                onTriggered: hero.toggle(tile.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
