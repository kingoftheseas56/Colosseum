// ExtensionsHouseHero — the Store's top card (agents/colosseum-extensions-world-mock.html): the
// universe banners crossfade behind a glass tray of Colosseum's own House apps. Universes opens the
// Hall; each House source has its install toggle; Manage opens the previous Extensions page.
// Tile data and toggles come from ExtensionsHousePage.qml (the full-page House it grew out of).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

Item {
    id: hero

    property int revision: 0                 // bump when Extensions changes
    signal universesRequested()
    signal manageRequested()

    readonly property bool compact: width < 1250
    property int universeIndex: 0

    width: parent ? parent.width : 1400
    height: 330

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
        running: hero.visible
        onTriggered: hero.universeIndex = (hero.universeIndex + 1) % banners.count
    }

    Rectangle {
        id: frame
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
                model: ["../assets/extensions/house/starwars-banner.jpg",
                        "../assets/extensions/house/dcau.jpg",
                        "../assets/extensions/house/cosmere.jpg"]
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

    // ---- Manage: the previous Extensions page (source ranking, Tankoyomi setup, links) ----
    Rectangle {
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 26; anchors.topMargin: 20
        width: manageText.implicitWidth + 36; height: 34
        radius: 17
        color: Qt.rgba(15 / 255, 16 / 255, 22 / 255, 0.78)
        border.width: 1
        border.color: manageAction.interactionActive ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
        Text {
            id: manageText
            anchors.centerIn: parent
            text: "Manage"
            color: manageAction.interactionActive ? theme.ink : Qt.rgba(1, 1, 1, 0.78)
            font.family: theme.ui; font.pixelSize: 14
        }
        KeyboardAction {
            id: manageAction
            objectName: "extensionsManageButton"
            anchors.fill: parent
            accessibleName: "Manage extensions"
            focusRadius: 17
            onTriggered: hero.manageRequested()
        }
    }

    // ---- the glass tray of House apps ----
    Item {
        id: tray
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
            anchors.centerIn: parent
            spacing: hero.compact ? 8 : 15
            scale: Math.min(1, (tray.width - 40) / Math.max(1, implicitWidth))

            Repeater {
                model: [
                    { name: "Universes", id: "", icon: "../assets/icons/universes-atom.svg", kind: "universes" },
                    { name: "Grand Database", id: "", icon: "../assets/addon-logos/colosseum-grand-database.png", kind: "builtin" },
                    { name: "Nyaa", id: "colosseum.well.nyaa", icon: "../assets/addon-logos/nyaa.png", kind: "source" },
                    { name: "Tankoyomi", id: "colosseum.well.tankoyomi", icon: "../assets/addon-logos/tankoyomi.png", kind: "source" },
                    { name: "GetComics", id: "colosseum.well.getcomics.issues", icon: "../assets/addon-logos/getcomics.png", kind: "source" },
                    { name: "Tankorent", id: "colosseum.well.indexers", icon: "../assets/addon-logos/tankorent.png", kind: "source" },
                    { name: "LibGen", id: "colosseum.well.libgen", icon: "../assets/addon-logos/libgen.ico", kind: "source" },
                    { name: "AudioBookBay", id: "colosseum.well.audiobookbay", icon: "../assets/addon-logos/audiobookbay.png", kind: "source" }
                ]
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
                            anchors.fill: parent
                            enabled: tile.modelData.kind === "universes"
                            accessibleName: tile.modelData.name
                            focusRadius: 19
                            onTriggered: hero.universesRequested()
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
                                font.family: theme.ui; font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            visible: tile.modelData.kind === "source"
                            width: 28; height: 28; radius: 14
                            y: toggleAction.interactionActive ? -2 : 0
                            color: tile.installed ? Qt.rgba(87 / 255, 207 / 255, 145 / 255, 0.34) : Qt.rgba(1, 1, 1, 0.18)
                            border.width: 1
                            border.color: tile.installed ? Qt.rgba(159 / 255, 1, 210 / 255, 0.38) : Qt.rgba(1, 1, 1, 0.25)
                            opacity: hero.isPresent(tile.modelData.id) ? 1 : 0.5
                            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            Text {
                                anchors.centerIn: parent
                                text: tile.installed ? "✓" : "+"
                                color: "white"
                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            }
                            KeyboardAction {
                                id: toggleAction
                                anchors.fill: parent
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
