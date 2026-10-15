// ExtensionsChainPage — native QML port of the 2026-09-27 Chain mock.
// The photograph is a fixed full-page stage; the chain scrolls independently above it.
// Medium endpoints awaken local light pools rather than globally brightening the wallpaper.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Shapes

Item {
    id: root
    objectName: "extensionsChainPage"

    property Item backdrop: null
    property bool showExplicit: false
    property url backgroundSource: "../assets/extensions/colosseum-night-cc0-1920x1080.jpg"
    property bool prototypeMode: (typeof Extensions === "undefined")
    property var installedRows: []
    property var prototypeInstalled: ({})

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal sectionRequested(string section)
    signal addonToggleRequested(string extensionId, bool enabled)

    Theme { id: theme }
    FontLoader { id: frauncesFont; source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { id: interFont; source: "../assets/fonts/Inter-Regular.otf" }

    readonly property color gold: theme.gold
    readonly property color ivory: "#f7f7f5"
    readonly property color dimInk: "#aaa59b"
    readonly property string displayFamily: frauncesFont.status === FontLoader.Ready ? frauncesFont.name : theme.display
    readonly property string uiFamily: interFont.status === FontLoader.Ready ? interFont.name : theme.ui
    readonly property real designWidth: Math.max(chainScroll.width, 1180)
    readonly property real designHeight: 1060

    function takeKeyboardFocus() {
        chainTab.forceActiveFocus(Qt.TabFocusReason)
    }

    function requestEscape() {
        return false
    }

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
        if (root.prototypeMode)
            return root.prototypeInstalled[extensionId] === true
        for (var i = 0; i < root.installedRows.length; ++i) {
            var row = root.installedRows[i]
            if (row && String(row.id || "") === extensionId && row.enabled !== false)
                return true
        }
        return false
    }

    function setPrototypeInstalled(extensionId, enabled) {
        var next = {}
        for (var key in root.prototypeInstalled)
            next[key] = root.prototypeInstalled[key]
        next[extensionId] = enabled
        root.prototypeInstalled = next
    }

    function rowFor(extensionId) {
        for (var i = 0; i < root.installedRows.length; ++i) {
            var row = root.installedRows[i]
            if (row && String(row.id || "") === extensionId)
                return row
        }
        return null
    }

    function toggleAddon(extensionId) {
        var enabled = !root.isInstalled(extensionId)
        if (root.prototypeMode) {
            root.setPrototypeInstalled(extensionId, enabled)
        } else if (root.rowFor(extensionId)) {
            // qmllint disable unqualified
            Extensions.setEnabled(extensionId, enabled)
            // qmllint enable unqualified
        }
        root.addonToggleRequested(extensionId, enabled)
    }

    function mediumUnlocked(key) {
        switch (key) {
        case "comics":
            return root.isInstalled("colosseum.well.getcomics.issues")
        case "manga":
            return root.isInstalled("colosseum.well.tankoyomi")
                    || root.isInstalled("colosseum.well.indexers")
        case "books":
            return root.isInstalled("colosseum.well.indexers")
                    || root.isInstalled("colosseum.well.libgen")
        case "audiobook":
            return root.isInstalled("colosseum.well.audiobookbay")
        case "tv":
        case "movies":
            return root.isInstalled("com.stremio.torrentio.addon")
        default:
            return false
        }
    }

    Component.onCompleted: refreshInstalled()

    Connections {
        // qmllint disable unqualified
        target: (typeof Extensions !== "undefined") ? Extensions : null
        // qmllint enable unqualified
        function onChanged() { root.refreshInstalled() }
    }

    component GoldIcon: Item {
        id: goldIcon
        property url source
        property color ink: root.gold
        Image {
            id: goldIconSource
            anchors.fill: parent
            source: goldIcon.source
            fillMode: Image.PreserveAspectFit
            sourceSize.width: Math.max(2, Math.round(width * 2))
            sourceSize.height: Math.max(2, Math.round(height * 2))
            smooth: true
            cache: true
            opacity: 0.28
        }
        MultiEffect {
            anchors.fill: goldIconSource
            source: goldIconSource
            colorization: 1.0
            colorizationColor: goldIcon.ink
        }
    }

    component LightPool: Item {
        id: lightPool
        required property string mediumKey
        required property real centerRatio
        property bool active: root.mediumUnlocked(mediumKey)

        anchors.fill: parent
        opacity: active ? 0.92 : 0
        Behavior on opacity {
            NumberAnimation { duration: 760; easing.type: Easing.OutCubic }
        }

        Image {
            id: lightPoolPhoto
            anchors.fill: parent
            source: root.backgroundSource
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            asynchronous: true
            cache: true
            visible: false
        }

        Item {
            id: lightPoolMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: 0
                    fillGradient: RadialGradient {
                        centerX: lightPool.width * lightPool.centerRatio
                        centerY: lightPool.height * 0.58
                        centerRadius: Math.min(lightPool.width * 0.22, lightPool.height * 0.50)
                        focalX: lightPool.width * lightPool.centerRatio
                        focalY: lightPool.height * 0.58
                        GradientStop { position: 0.00; color: "#ffffffff" }
                        GradientStop { position: 0.36; color: "#f0ffffff" }
                        GradientStop { position: 0.72; color: "#70ffffff" }
                        GradientStop { position: 1.00; color: "#00ffffff" }
                    }
                    startX: 0
                    startY: 0
                    PathLine { x: lightPool.width; y: 0 }
                    PathLine { x: lightPool.width; y: lightPool.height }
                    PathLine { x: 0; y: lightPool.height }
                }
            }
        }

        MultiEffect {
            anchors.fill: parent
            source: lightPoolPhoto
            brightness: 0.20
            saturation: 0.12
            contrast: 0.06
            maskEnabled: true
            maskSource: lightPoolMask
        }

        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: 0
                fillGradient: RadialGradient {
                    centerX: lightPool.width * lightPool.centerRatio
                    centerY: lightPool.height * 0.60
                    centerRadius: Math.min(lightPool.width * 0.20, lightPool.height * 0.44)
                    focalX: lightPool.width * lightPool.centerRatio
                    focalY: lightPool.height * 0.60
                    GradientStop { position: 0.00; color: Qt.rgba(1.0, 0.78, 0.25, 0.22) }
                    GradientStop { position: 0.48; color: Qt.rgba(1.0, 0.64, 0.14, 0.10) }
                    GradientStop { position: 1.00; color: Qt.rgba(1.0, 0.64, 0.14, 0.0) }
                }
                startX: 0
                startY: 0
                PathLine { x: lightPool.width; y: 0 }
                PathLine { x: lightPool.width; y: lightPool.height }
                PathLine { x: 0; y: lightPool.height }
            }
        }
    }

    component ChainWire: Item {
        id: wire
        required property real startX
        required property real startY
        required property real endX
        required property real endY
        property real controlY1: startY + (endY - startY) * 0.45
        property real controlY2: startY + (endY - startY) * 0.55
        property bool active: false
        property bool core: false
        property bool energizing: false

        anchors.fill: parent

        function energize() {
            if (!wire.active)
                return
            wire.energizing = true
            wirePulse.restart()
        }

        onActiveChanged: {
            if (active)
                energize()
            else
                energizing = false
        }

        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: wire.core || wire.active
                             ? Qt.rgba(240/255, 196/255, 74/255, wire.core ? 0.92 : 0.90)
                             : Qt.rgba(213/255, 209/255, 199/255, 0.28)
                strokeWidth: wire.core || wire.active ? 3.0 : 2.2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: wire.startX
                startY: wire.startY
                PathCubic {
                    x: wire.endX
                    y: wire.endY
                    control1X: wire.startX
                    control1Y: wire.controlY1
                    control2X: wire.endX
                    control2Y: wire.controlY2
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: wire.energizing
            ShapePath {
                id: pulsePath
                strokeColor: "#ffe477"
                strokeWidth: 4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                dashOffset: 0
                startX: wire.startX
                startY: wire.startY
                PathCubic {
                    x: wire.endX
                    y: wire.endY
                    control1X: wire.startX
                    control1Y: wire.controlY1
                    control2X: wire.endX
                    control2Y: wire.controlY2
                }
            }
        }

        SequentialAnimation {
            id: wirePulse
            loops: 1
            NumberAnimation {
                target: pulsePath
                property: "dashOffset"
                from: 0
                to: -24
                duration: 820
                easing.type: Easing.Linear
            }
            ScriptAction { script: wire.energizing = false }
        }
    }

    component ChainNode: Rectangle {
        id: chainNode
        required property string title
        property string subtitle: ""
        property string statusText: ""
        property url iconSource: ""
        property bool rawIcon: false
        property bool optional: false
        property bool shared: false
        property string extensionId: ""
        property bool installed: false
        property bool rootNode: false
        property bool showLetterIcon: false
        property string letterIcon: ""

        radius: optional ? 18 : 20
        color: {
            if (optional && installed)
                return "#241c0e"
            if (rootNode)
                return "#201b12"
            return optional ? "#090a0d" : "#101216"
        }
        border.width: rootNode ? 2 : 1
        border.color: optional
                      ? (installed ? Qt.rgba(240/255,196/255,74/255,0.78)
                                   : Qt.rgba(1,1,1,0.12))
                      : Qt.rgba(240/255,196/255,74/255, rootNode ? 0.78 : 0.48)

        activeFocusOnTab: optional
        Accessible.role: optional ? Accessible.Button : Accessible.StaticText
        Accessible.name: optional
                         ? ((installed ? "Installed " : "Install ") + title)
                         : title

        Rectangle {
            z: -1
            visible: chainNode.installed || chainNode.rootNode || chainNode.activeFocus
            anchors.fill: parent
            anchors.margins: -7
            radius: chainNode.radius + 7
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(240/255,196/255,74/255, chainNode.installed || chainNode.rootNode ? 0.18 : 0.10)
        }

        Rectangle {
            id: iconPlate
            width: chainNode.optional ? 54 : Math.min(parent.height, 76)
            height: chainNode.optional ? 54 : Math.min(parent.height, 76)
            radius: chainNode.optional ? 12 : 14
            anchors.left: chainNode.optional ? undefined : parent.left
            anchors.leftMargin: chainNode.optional ? 0 : 10
            anchors.horizontalCenter: chainNode.optional ? parent.horizontalCenter : undefined
            anchors.top: chainNode.optional ? parent.top : undefined
            anchors.topMargin: chainNode.optional ? 13 : 0
            anchors.verticalCenter: chainNode.optional ? undefined : parent.verticalCenter
            color: chainNode.optional
                   ? Qt.rgba(1,1,1,0.04)
                   : Qt.rgba(1,1,1,0.055)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.08)

            Loader {
                anchors.centerIn: parent
                width: parent.width * 0.72
                height: parent.height * 0.72
                sourceComponent: chainNode.showLetterIcon ? letterComponent
                                 : chainNode.rawIcon ? rawComponent : goldComponent
            }
        }

        Component {
            id: goldComponent
            GoldIcon {
                source: chainNode.iconSource
                ink: root.gold
            }
        }

        Component {
            id: rawComponent
            Image {
                source: chainNode.iconSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: true
            }
        }

        Component {
            id: letterComponent
            Text {
                text: chainNode.letterIcon
                color: root.gold
                font.family: root.displayFamily
                font.pixelSize: 30
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Column {
            visible: !chainNode.optional
            anchors.left: iconPlate.right
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            Text {
                width: parent.width
                text: chainNode.title
                color: root.ivory
                font.family: root.displayFamily
                font.pixelSize: chainNode.rootNode ? 34 : 26
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: chainNode.subtitle
                color: root.dimInk
                font.family: root.uiFamily
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Text {
            visible: chainNode.optional
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: iconPlate.bottom
            anchors.topMargin: 7
            width: parent.width - 14
            text: chainNode.title
            color: chainNode.installed ? root.ivory : root.dimInk
            font.family: root.displayFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 10
            anchors.topMargin: 7
            text: chainNode.statusText
            color: chainNode.installed || !chainNode.optional
                   ? Qt.rgba(240/255,196/255,74/255,0.82)
                   : Qt.rgba(1,1,1,0.40)
            font.family: root.uiFamily
            font.pixelSize: 9
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.1
        }

        Rectangle {
            visible: chainNode.shared
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 5
            width: sharedLabel.implicitWidth + 18
            height: 20
            radius: 10
            color: "#111216"
            border.width: 1
            border.color: Qt.rgba(240/255,196/255,74/255,0.52)
            Text {
                id: sharedLabel
                anchors.centerIn: parent
                text: "SHARED"
                color: root.gold
                font.family: root.uiFamily
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
            }
        }

        Rectangle {
            visible: chainNode.activeFocus
            anchors.fill: parent
            anchors.margins: -4
            radius: chainNode.radius + 4
            color: "transparent"
            border.width: 2
            border.color: root.gold
        }

        MouseArea {
            anchors.fill: parent
            enabled: chainNode.optional
            hoverEnabled: true
            cursorShape: chainNode.optional ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                chainNode.forceActiveFocus(Qt.MouseFocusReason)
                root.toggleAddon(chainNode.extensionId)
            }
        }

        Keys.onReturnPressed: if (optional) root.toggleAddon(extensionId)
        Keys.onSpacePressed: if (optional) root.toggleAddon(extensionId)
    }

    component EndpointCard: Rectangle {
        id: endpoint
        required property string label
        required property string mediumKey
        required property url iconSource
        property bool unlocked: root.mediumUnlocked(mediumKey)

        radius: 4
        color: unlocked ? "#4f3710" : "#0c0e11"
        border.width: unlocked ? 2 : 1
        border.color: unlocked ? "#ffe477" : Qt.rgba(1,1,1,0.18)

        Rectangle {
            z: -2
            visible: endpoint.unlocked
            anchors.fill: parent
            anchors.margins: -18
            radius: 16
            color: Qt.rgba(240/255,196/255,74/255,0.08)
            border.width: 2
            border.color: Qt.rgba(240/255,196/255,74/255,0.22)
        }

        Rectangle {
            z: -1
            visible: endpoint.unlocked
            anchors.fill: parent
            anchors.margins: -8
            radius: 10
            color: Qt.rgba(240/255,196/255,74/255,0.08)
            border.width: 2
            border.color: Qt.rgba(1.0,0.88,0.44,0.34)
        }

        Rectangle {
            visible: endpoint.unlocked
            anchors.horizontalCenter: parent.horizontalCenter
            y: -7
            width: 14
            height: 14
            radius: 7
            color: "#ffe477"
            border.width: 2
            border.color: "#fff0af"
        }

        GoldIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 15
            width: 58
            height: 58
            source: endpoint.iconSource
            ink: endpoint.unlocked ? "#ffe477" : "#9d9991"
            opacity: endpoint.unlocked ? 1 : 0.82
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 13
            width: parent.width - 16
            text: endpoint.label
            color: endpoint.unlocked ? "#ffe477" : "#a6a199"
            font.family: root.displayFamily
            font.pixelSize: 18
            font.weight: Font.DemiBold
            font.letterSpacing: 1.0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Image {
        id: basePhoto
        anchors.fill: parent
        source: root.backgroundSource
        fillMode: Image.PreserveAspectCrop
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        asynchronous: true
        cache: true
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.015, 0.02, 0.03, 0.66)
    }

    LightPool { mediumKey: "comics"; centerRatio: 0.05 }
    LightPool { mediumKey: "manga"; centerRatio: 0.22 }
    LightPool { mediumKey: "books"; centerRatio: 0.41 }
    LightPool { mediumKey: "audiobook"; centerRatio: 0.59 }
    LightPool { mediumKey: "tv"; centerRatio: 0.78 }
    LightPool { mediumKey: "movies"; centerRatio: 0.96 }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.02,0.025,0.04,0.34) }
            GradientStop { position: 0.45; color: Qt.rgba(0.02,0.025,0.04,0.08) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02,0.025,0.04,0.38) }
        }
    }

    Flickable {
        id: chainScroll
        objectName: "extensionsChainScroll"
        anchors.fill: parent
        contentWidth: root.designWidth
        contentHeight: root.designHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 2800

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Item {
            id: stage
            width: chainScroll.contentWidth
            height: chainScroll.contentHeight

            Rectangle {
                id: picker
                x: (stage.width - width) / 2
                y: 31
                width: 380
                height: 60
                radius: 30
                color: Qt.rgba(0.28,0.29,0.31,0.90)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.08)

                Row {
                    anchors.fill: parent
                    anchors.margins: 7
                    spacing: 4

                    Rectangle {
                        id: chainTab
                        width: 118
                        height: parent.height
                        radius: height / 2
                        color: root.gold
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "Chain"
                        Text {
                            anchors.centerIn: parent
                            text: "Chain"
                            color: "#15120b"
                            font.family: root.uiFamily
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chainTab.forceActiveFocus(Qt.MouseFocusReason)
                        }
                    }

                    Rectangle {
                        id: houseTab
                        width: 118
                        height: parent.height
                        radius: height / 2
                        color: houseTabMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : "transparent"
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "House"
                        Text {
                            anchors.centerIn: parent
                            text: "House"
                            color: root.ivory
                            opacity: 0.72
                            font.family: root.uiFamily
                            font.pixelSize: 17
                        }
                        MouseArea {
                            id: houseTabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.sectionRequested("house")
                        }
                        Keys.onReturnPressed: root.sectionRequested("house")
                        Keys.onSpacePressed: root.sectionRequested("house")
                    }

                    Rectangle {
                        width: 118
                        height: parent.height
                        radius: height / 2
                        color: "transparent"
                        opacity: 0.72
                        Accessible.role: Accessible.Button
                        Accessible.name: "Store unavailable"
                        Text {
                            anchors.centerIn: parent
                            text: "Store"
                            color: root.ivory
                            opacity: 0.70
                            font.family: root.uiFamily
                            font.pixelSize: 17
                        }
                    }
                }
            }

            ChainWire {
                startX: stage.width * 0.50; startY: 182
                endX: stage.width * 0.185; endY: 245
                core: true
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 182
                endX: stage.width * 0.50; endY: 245
                core: true
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 182
                endX: stage.width * 0.815; endY: 245
                core: true
            }
            ChainWire {
                startX: stage.width * 0.185; startY: 330
                endX: stage.width * 0.185; endY: 385
                core: true
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 330
                endX: stage.width * 0.50; endY: 385
                core: true
            }
            ChainWire {
                startX: stage.width * 0.815; startY: 330
                endX: stage.width * 0.815; endY: 385
                core: true
            }

            ChainWire {
                startX: stage.width * 0.185; startY: 470
                endX: stage.width * 0.0875; endY: 585
                active: root.isInstalled("colosseum.well.getcomics.issues")
            }
            ChainWire {
                startX: stage.width * 0.185; startY: 470
                endX: stage.width * 0.2495; endY: 585
                active: root.isInstalled("colosseum.well.tankoyomi")
            }
            ChainWire {
                startX: stage.width * 0.185; startY: 470
                endX: stage.width * 0.42; endY: 585
                active: root.isInstalled("colosseum.well.indexers")
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 470
                endX: stage.width * 0.42; endY: 585
                active: root.isInstalled("colosseum.well.indexers")
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 470
                endX: stage.width * 0.5795; endY: 585
                active: root.isInstalled("colosseum.well.libgen")
            }
            ChainWire {
                startX: stage.width * 0.50; startY: 470
                endX: stage.width * 0.7415; endY: 585
                active: root.isInstalled("colosseum.well.audiobookbay")
            }
            ChainWire {
                startX: stage.width * 0.815; startY: 470
                endX: stage.width * 0.9035; endY: 585
                active: root.isInstalled("com.stremio.torrentio.addon")
            }

            ChainWire {
                startX: stage.width * 0.0875; startY: 677
                endX: stage.width * 0.080; endY: 790
                active: root.isInstalled("colosseum.well.getcomics.issues")
            }
            ChainWire {
                startX: stage.width * 0.2495; startY: 677
                endX: stage.width * 0.248; endY: 790
                active: root.isInstalled("colosseum.well.tankoyomi")
            }
            ChainWire {
                startX: stage.width * 0.42; startY: 677
                endX: stage.width * 0.248; endY: 790
                active: root.isInstalled("colosseum.well.indexers")
            }
            ChainWire {
                startX: stage.width * 0.42; startY: 677
                endX: stage.width * 0.416; endY: 790
                active: root.isInstalled("colosseum.well.indexers")
            }
            ChainWire {
                startX: stage.width * 0.5795; startY: 677
                endX: stage.width * 0.416; endY: 790
                active: root.isInstalled("colosseum.well.libgen")
            }
            ChainWire {
                startX: stage.width * 0.7415; startY: 677
                endX: stage.width * 0.584; endY: 790
                active: root.isInstalled("colosseum.well.audiobookbay")
            }
            ChainWire {
                startX: stage.width * 0.9035; startY: 677
                endX: stage.width * 0.752; endY: 790
                active: root.isInstalled("com.stremio.torrentio.addon")
            }
            ChainWire {
                startX: stage.width * 0.9035; startY: 677
                endX: stage.width * 0.920; endY: 790
                active: root.isInstalled("com.stremio.torrentio.addon")
            }

            ChainNode {
                x: stage.width * 0.29
                y: 112
                width: stage.width * 0.42
                height: 70
                title: "Colosseum App"
                subtitle: "The root layer"
                statusText: "ROOT"
                rootNode: true
                iconSource: "../assets/icons/colosseum.svg"
            }

            ChainNode {
                x: stage.width * 0.05; y: 245
                width: stage.width * 0.27; height: 85
                title: "Tankoban"; subtitle: "Comics & manga"; statusText: "BUILT-IN"
                iconSource: "../assets/icons/manga.svg"
            }
            ChainNode {
                x: stage.width * 0.365; y: 245
                width: stage.width * 0.27; height: 85
                title: "Biblio"; subtitle: "Books & audiobooks"; statusText: "BUILT-IN"
                iconSource: "../assets/icons/books.svg"
            }
            ChainNode {
                x: stage.width * 0.68; y: 245
                width: stage.width * 0.27; height: 85
                title: "Theatre"; subtitle: "Film, shows & anime"; statusText: "BUILT-IN"
                iconSource: "../assets/icons/projector-theatre.svg"
            }

            ChainNode {
                x: stage.width * 0.05; y: 385
                width: stage.width * 0.27; height: 85
                title: "Colosseum Database"; subtitle: "Tankoban foundation"; statusText: "BUILT-IN"
                iconSource: "../assets/addon-logos/colosseum-grand-database.png"
                rawIcon: true
            }
            ChainNode {
                x: stage.width * 0.365; y: 385
                width: stage.width * 0.27; height: 85
                title: "Apple Books"; subtitle: "Biblio foundation"; statusText: "BUILT-IN"
                iconSource: "../assets/addon-logos/applebooks.ico"
                rawIcon: true
            }
            ChainNode {
                x: stage.width * 0.68; y: 385
                width: stage.width * 0.27; height: 85
                title: "Cinemeta"; subtitle: "Theatre catalogue"; statusText: "BUILT-IN"
                showLetterIcon: true
                letterIcon: "C"
            }

            ListModel {
                id: addonModel
                ListElement {
                    modelExtensionId: "colosseum.well.getcomics.issues"
                    modelTitle: "GetComics"
                    modelIcon: "../assets/addon-logos/getcomics.png"
                    xPct: 0.01
                    isShared: false
                }
                ListElement {
                    modelExtensionId: "colosseum.well.tankoyomi"
                    modelTitle: "Tankoyomi"
                    modelIcon: "../assets/addon-logos/tankoyomi.png"
                    xPct: 0.172
                    isShared: false
                }
                ListElement {
                    modelExtensionId: "colosseum.well.indexers"
                    modelTitle: "Tankorent"
                    modelIcon: "../assets/addon-logos/tankorent.png"
                    xPct: 0.34
                    isShared: true
                }
                ListElement {
                    modelExtensionId: "colosseum.well.libgen"
                    modelTitle: "LibGen"
                    modelIcon: "../assets/addon-logos/libgen.ico"
                    xPct: 0.502
                    isShared: false
                }
                ListElement {
                    modelExtensionId: "colosseum.well.audiobookbay"
                    modelTitle: "AudioBookBay"
                    modelIcon: "../assets/addon-logos/audiobookbay.png"
                    xPct: 0.664
                    isShared: false
                }
                ListElement {
                    modelExtensionId: "com.stremio.torrentio.addon"
                    modelTitle: "Torrentio"
                    modelIcon: "../assets/addon-logos/torrentio.png"
                    xPct: 0.826
                    isShared: false
                }
            }

            Repeater {
                model: addonModel
                delegate: ChainNode {
                    required property string modelExtensionId
                    required property string modelTitle
                    required property string modelIcon
                    required property real xPct
                    required property bool isShared

                    x: stage.width * xPct
                    y: 585
                    width: stage.width * (isShared ? 0.16 : 0.155)
                    height: 92
                    optional: true
                    rawIcon: true
                    extensionId: modelExtensionId
                    title: modelTitle
                    shared: isShared
                    iconSource: modelIcon
                    statusText: installed ? "INSTALLED" : "INSTALL"
                    installed: root.isInstalled(modelExtensionId)
                }
            }

            ListModel {
                id: endpointModel
                ListElement {
                    key: "comics"
                    modelLabel: "COMICS"
                    modelIcon: "../assets/icons/comics.svg"
                    xPct: 0.016
                }
                ListElement {
                    key: "manga"
                    modelLabel: "MANGA"
                    modelIcon: "../assets/icons/manga.svg"
                    xPct: 0.184
                }
                ListElement {
                    key: "books"
                    modelLabel: "BOOKS"
                    modelIcon: "../assets/icons/books.svg"
                    xPct: 0.352
                }
                ListElement {
                    key: "audiobook"
                    modelLabel: "AUDIOBOOK"
                    modelIcon: "../assets/icons/music.svg"
                    xPct: 0.520
                }
                ListElement {
                    key: "tv"
                    modelLabel: "TV"
                    modelIcon: "../assets/icons/feria-tv.svg"
                    xPct: 0.688
                }
                ListElement {
                    key: "movies"
                    modelLabel: "MOVIES"
                    modelIcon: "../assets/icons/movies.svg"
                    xPct: 0.856
                }
            }

            Repeater {
                model: endpointModel
                delegate: EndpointCard {
                    required property string key
                    required property string modelLabel
                    required property string modelIcon
                    required property real xPct

                    x: stage.width * xPct
                    y: 790
                    width: stage.width * 0.128
                    height: 138
                    mediumKey: key
                    label: modelLabel
                    iconSource: modelIcon
                }
            }
        }
    }
}
