// ExtensionsHousePage — native QML port of the 2026-09-27 House mock.
// One universe wallpaper at a time, passive crossfade, glass tray, eight house tiles.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

Item {
    id: root
    objectName: "extensionsHousePage"

    signal sectionRequested(string section)
    signal installRequested(string extensionId, bool installed)
    signal universesRequested()

    Theme { id: theme }

    readonly property string uiFamily: theme.ui
    property int universeIndex: 0
    property var localInstalled: ({})
    property bool prototypeMode: (typeof Extensions === "undefined")
    property var installedRows: []

    function refreshInstalled() {
        if (root.prototypeMode || typeof Extensions === "undefined") {
            root.installedRows = []
            return
        }
        // qmllint disable unqualified
        root.installedRows = Extensions.installed()
        // qmllint enable unqualified
    }

    function isInstalled(extensionId) {
        if (!extensionId.length)
            return true
        if (root.prototypeMode)
            return root.localInstalled[extensionId] === true
        for (var i = 0; i < root.installedRows.length; ++i) {
            var row = root.installedRows[i]
            if (row && String(row.id || "") === extensionId && row.enabled !== false)
                return true
        }
        return false
    }

    function toggleInstall(extensionId) {
        var nextState = !root.isInstalled(extensionId)
        if (root.prototypeMode) {
            var next = {}
            for (var key in root.localInstalled)
                next[key] = root.localInstalled[key]
            next[extensionId] = nextState
            root.localInstalled = next
        }
        root.installRequested(extensionId, nextState)
    }

    function extensionBackground(extensionId, fallback) {
        for (var i = 0; i < root.installedRows.length; ++i) {
            var row = root.installedRows[i]
            if (!row || String(row.id || "") !== extensionId)
                continue
            var manifest = row.manifest || {}
            var background = String(manifest.background || "")
            if (background.length)
                return background
        }
        return fallback
    }

    Component.onCompleted: refreshInstalled()

    Connections {
        // qmllint disable unqualified
        target: (typeof Extensions !== "undefined") ? Extensions : null
        // qmllint enable unqualified
        function onChanged() { root.refreshInstalled() }
    }

    Timer {
        interval: 7500
        running: root.visible
        repeat: true
        onTriggered: root.universeIndex = (root.universeIndex + 1) % 3
    }

    Item {
        id: wallpaperLayer
        anchors.fill: parent

        Item {
            id: starWarsSlide
            anchors.fill: parent
            opacity: root.universeIndex === 0 ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 1050; easing.type: Easing.InOutCubic }
            }

            Image {
                anchors.fill: parent
                source: "../assets/extensions/house/starwars-banner.jpg"
                fillMode: Image.PreserveAspectCrop
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignVCenter
                asynchronous: true
                cache: true
            }

            Image {
                anchors.fill: parent
                source: root.extensionBackground("com.colosseum.universe.starwars",
                    "../assets/extensions/house/starwars-banner.jpg")
                fillMode: Image.PreserveAspectCrop
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignVCenter
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: 420; easing.type: Easing.OutCubic }
                }
            }
        }

        Image {
            anchors.fill: parent
            source: "../assets/extensions/house/dcau.jpg"
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            cache: true
            opacity: root.universeIndex === 1 ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 1050; easing.type: Easing.InOutCubic }
            }
        }

        Image {
            anchors.fill: parent
            source: "../assets/extensions/house/cosmere.jpg"
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            cache: true
            opacity: root.universeIndex === 2 ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 1050; easing.type: Easing.InOutCubic }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(0,0,0,0.24) }
            GradientStop { position: 0.45; color: Qt.rgba(0,0,0,0.04) }
            GradientStop { position: 0.62; color: Qt.rgba(0,0,0,0.10) }
            GradientStop { position: 1.00; color: Qt.rgba(0,0,0,0.45) }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 0
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * 0.16
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0,0,0,0.14) }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * 0.16
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(0,0,0,0.14) }
            }
        }
    }

    Rectangle {
        id: picker
        z: 20
        x: (root.width - width) / 2
        y: 31
        width: 380
        height: 60
        radius: 30
        color: "transparent"

        Glass {
            id: pickerGlass
            anchors.fill: parent
            backdrop: wallpaperLayer
            radius: picker.radius
            tint: 0.08
            scrim: 0.17
            blurAmount: 1.0
            edge: Qt.rgba(1,1,1,0.07)
        }

        Row {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 4

            Rectangle {
                id: chainTab
                width: 118
                height: parent.height
                radius: height / 2
                color: chainMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Chain"
                Text {
                    anchors.centerIn: parent
                    text: "Chain"
                    color: "#ffffff"
                    opacity: 0.70
                    font.family: root.uiFamily
                    font.pixelSize: 17
                }
                MouseArea {
                    id: chainMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.sectionRequested("chain")
                }
                Keys.onReturnPressed: root.sectionRequested("chain")
                Keys.onSpacePressed: root.sectionRequested("chain")
            }

            Rectangle {
                id: houseTab
                width: 118
                height: parent.height
                radius: height / 2
                color: "#efc15a"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "House"
                Text {
                    anchors.centerIn: parent
                    text: "House"
                    color: "#15120b"
                    font.family: root.uiFamily
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: houseTab.forceActiveFocus(Qt.MouseFocusReason)
                }
            }

            Rectangle {
                id: storeTab
                width: 118
                height: parent.height
                radius: height / 2
                color: storeMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : "transparent"
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Store"
                Text {
                    anchors.centerIn: parent
                    text: "Store"
                    color: "#ffffff"
                    opacity: 0.70
                    font.family: root.uiFamily
                    font.pixelSize: 17
                }
                MouseArea {
                    id: storeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.sectionRequested("store")
                }
                Keys.onReturnPressed: root.sectionRequested("store")
                Keys.onSpacePressed: root.sectionRequested("store")
            }
        }
    }

    Rectangle {
        id: tray
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.width < 1250 ? 44 : 64
        width: Math.min(1490, root.width * 0.94)
        height: root.width < 1250 ? 206 : 230
        radius: 34
        color: "transparent"

        Rectangle {
            z: -2
            anchors.fill: parent
            anchors.margins: -16
            radius: tray.radius + 16
            color: Qt.rgba(0,0,0,0.14)
        }

        Glass {
            id: trayGlass
            anchors.fill: parent
            backdrop: wallpaperLayer
            radius: tray.radius
            tint: 0.24
            scrim: 0.00
            blurAmount: 1.0
            blurMax: 64
            blurMultiplier: 0.20
            edge: Qt.rgba(1,1,1,0.34)
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(236/255,229/255,216/255,0.13) }
                GradientStop { position: 1; color: Qt.rgba(220/255,212/255,198/255,0.05) }
            }
        }

        ListModel {
            id: houseModel
            ListElement {
                tileName: "Universes"; extensionId: ""; icon: ""; tileKind: "universes"; downloadable: false
            }
            ListElement {
                tileName: "Grand Database"; extensionId: ""; icon: "../assets/addon-logos/colosseum-grand-database.png"; tileKind: "database"; downloadable: false
            }
            ListElement {
                tileName: "Nyaa"; extensionId: "colosseum.well.nyaa"; icon: "../assets/addon-logos/nyaa.png"; tileKind: "nyaa"; downloadable: true
            }
            ListElement {
                tileName: "Tankoyomi"; extensionId: "colosseum.well.tankoyomi"; icon: "../assets/addon-logos/tankoyomi.png"; tileKind: "tankoyomi"; downloadable: true
            }
            ListElement {
                tileName: "GetComics"; extensionId: "colosseum.well.getcomics.issues"; icon: "../assets/addon-logos/getcomics.png"; tileKind: "getcomics"; downloadable: true
            }
            ListElement {
                tileName: "Tankorent"; extensionId: "colosseum.well.indexers"; icon: "../assets/addon-logos/tankorent.png"; tileKind: "tankorent"; downloadable: true
            }
            ListElement {
                tileName: "LibGen"; extensionId: "colosseum.well.libgen"; icon: "../assets/addon-logos/libgen.ico"; tileKind: "libgen"; downloadable: true
            }
            ListElement {
                tileName: "AudioBookBay"; extensionId: "colosseum.well.audiobookbay"; icon: "../assets/addon-logos/audiobookbay.png"; tileKind: "abb"; downloadable: true
            }
        }

        Row {
            id: tileRow
            anchors.centerIn: parent
            width: 1276
            height: 182
            spacing: 20
            transformOrigin: Item.Center
            scale: Math.min(1, (tray.width - 48) / width)

            Repeater {
                model: houseModel
                delegate: Item {
                    id: tileItem
                    required property string tileName
                    required property string extensionId
                    required property string icon
                    required property string tileKind
                    required property bool downloadable

                    width: 142
                    height: 182

                    Rectangle {
                        id: appTile
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: 134
                        height: 134
                        radius: 22
                        color: Qt.rgba(250/255,250/255,248/255,0.97)
                        scale: tileMouse.containsMouse || activeFocus ? 1.03 : 1
                        y: tileMouse.containsMouse || activeFocus ? -5 : 0
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: tileItem.tileName

                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Qt.rgba(0,0,0, tileMouse.containsMouse || appTile.activeFocus ? 0.42 : 0.28)
                            shadowVerticalOffset: tileMouse.containsMouse || appTile.activeFocus ? 18 : 14
                            shadowBlur: tileMouse.containsMouse || appTile.activeFocus ? 0.95 : 0.82
                            autoPaddingEnabled: true
                        }

                        Loader {
                            anchors.centerIn: parent
                            width: tileItem.tileKind === "getcomics" ? 76
                                  : tileItem.tileKind === "tankoyomi" ? 74
                                  : tileItem.tileKind === "nyaa" ? 68
                                  : tileItem.tileKind === "libgen" ? 68
                                  : tileItem.tileKind === "abb" ? 68
                                  : 71
                            height: width
                            sourceComponent: tileItem.tileKind === "universes" ? universeIcon : imageIcon
                        }

                        Component {
                            id: imageIcon
                            Rectangle {
                                color: "transparent"
                                radius: tileItem.tileKind === "tankoyomi" ? 10 : 0
                                clip: tileItem.tileKind === "tankoyomi"
                                Image {
                                    anchors.fill: parent
                                    source: tileItem.icon
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    cache: true
                                }
                            }
                        }

                        Component {
                            id: universeIcon
                            Image {
                                source: "../assets/icons/universes-atom.svg"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                cache: true
                            }
                        }

                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                appTile.forceActiveFocus(Qt.MouseFocusReason)
                                if (tileItem.tileKind === "universes")
                                    root.universesRequested()
                            }
                        }
                        Keys.onReturnPressed: if (tileItem.tileKind === "universes") root.universesRequested()
                        Keys.onSpacePressed: if (tileItem.tileKind === "universes") root.universesRequested()
                    }

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        height: 34
                        spacing: 7

                        Rectangle {
                            width: Math.min(112, Math.max(70, nameText.implicitWidth + 22))
                            height: 34
                            radius: 12
                            color: Qt.rgba(25/255,26/255,29/255,0.26)
                            border.width: 1
                            border.color: Qt.rgba(1,1,1,0.16)
                            Text {
                                id: nameText
                                anchors.centerIn: parent
                                text: tileItem.tileName
                                color: Qt.rgba(1,1,1,0.84)
                                font.family: root.uiFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            id: downloadButton
                            visible: tileItem.downloadable
                            readonly property bool installedState: root.isInstalled(tileItem.extensionId)
                            width: 34
                            height: 34
                            y: downloadMouse.containsMouse || activeFocus ? -2 : 0
                            radius: 10
                            color: installedState
                                   ? Qt.rgba(87/255,207/255,145/255,0.34)
                                   : Qt.rgba(1,1,1,0.18)
                            border.width: 1
                            border.color: installedState
                                          ? Qt.rgba(159/255,1.0,210/255,0.38)
                                          : Qt.rgba(1,1,1,0.25)
                            activeFocusOnTab: visible
                            Accessible.role: Accessible.Button
                            Accessible.name: (installedState ? "Installed " : "Download ") + tileItem.tileName

                            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 1
                                height: parent.height * 0.48
                                radius: downloadButton.radius - 1
                                color: downloadButton.installedState
                                       ? Qt.rgba(1,1,1,0.07)
                                       : Qt.rgba(1,1,1,0.10)
                            }

                            Image {
                                visible: !downloadButton.installedState
                                anchors.centerIn: parent
                                width: 15
                                height: 15
                                source: "../assets/icons/download.svg"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                cache: true
                            }
                            Text {
                                visible: downloadButton.installedState
                                anchors.centerIn: parent
                                text: "✓"
                                color: "#ffffff"
                                font.family: root.uiFamily
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }

                            MouseArea {
                                id: downloadMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    downloadButton.forceActiveFocus(Qt.MouseFocusReason)
                                    root.toggleInstall(tileItem.extensionId)
                                }
                            }
                            Keys.onReturnPressed: root.toggleInstall(tileItem.extensionId)
                            Keys.onSpacePressed: root.toggleInstall(tileItem.extensionId)
                        }
                    }
                }
            }
        }
    }
}
