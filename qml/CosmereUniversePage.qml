// CosmereUniversePage — THE COGNITIVE ATLAS. A newcomer-first book room: three honest
// entry portals, then the connected planetary systems. Curated strings may label the map,
// but an action only becomes live when CosmereApi has resolved a full Biblio book object.
import QtQuick
import QtQuick.Controls
import "CosmereApi.js" as Cosmere
import "Universes.js" as UDB

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    property string universeName: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal bookRequested(var book)

    Theme { id: theme }

    readonly property color voidColor: "#070a12"
    readonly property color cognitiveBlue: "#78cfe3"
    readonly property color stormlight: "#d9e8ff"
    readonly property color copper: "#b8734a"
    property var guide: UDB.configFor(root.universeName)
    property var uni: ({ name: "Cosmere", blurb: "", banner: "", metaline: "",
                         starters: [], worlds: [] })

    function reload() {
        root.guide = UDB.configFor(root.universeName)
        if (!root.universeName.length) return
        Cosmere.loadAtlas(root.universeName, function(atlas) { if (atlas) root.uni = atlas })
    }
    function portalFor(query) {
        var portals = root.uni.starters || []
        for (var i = 0; i < portals.length; i++)
            if (portals[i].query === query) return portals[i]
        return null
    }
    function openBook(book) {
        if (book && typeof book === "object" && book.id && book.title)
            root.bookRequested(book)
    }

    Component.onCompleted: { reload(); reveal.start() }
    onUniverseNameChanged: reload()

    // A solid night instrument. The app wallpaper is only a faint atmospheric reflection.
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.027, 0.039, 0.071, 0.95) }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0b1120" }
                GradientStop { position: 0.48; color: root.voidColor }
                GradientStop { position: 1.0; color: "#04050a" }
            }
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: contentColumn
            width: page.width
            opacity: 0
            spacing: 0

            // ===== THE INVITATION =====
            Item {
                id: hero
                width: parent.width
                height: Math.max(680, root.height)
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.guide.banner || ""
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 0.17 : 0
                    Behavior on opacity { NumberAnimation { duration: 500 } }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.027,0.039,0.071,0.50) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.027,0.039,0.071,0.80) }
                        GradientStop { position: 1.0; color: root.voidColor }
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: theme.margin
                    anchors.top: parent.top
                    anchors.topMargin: 104
                    width: Math.min(600, parent.width * 0.49)
                    spacing: 13
                    Text {
                        text: "THE COSMERE  /  A CONNECTED EPIC"
                        color: root.cognitiveBlue
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 3
                    }
                    Text {
                        text: "Choose a world.\nThe connections can wait."
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 54
                        lineHeight: 0.92
                    }
                    Text {
                        text: root.uni.blurb || root.guide.blurb || ""
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 15
                        lineHeight: 1.35
                        width: parent.width - 20
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: "Each story stands alone. Begin with the kind of journey you want."
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                        topPadding: 4
                    }
                }

                // The page's signature: three routes orbit one shared light instead of a card row.
                Item {
                    id: entryAtlas
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 320

                    Canvas {
                        anchors.fill: parent
                        opacity: 0.62
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var cx = width / 2
                            var cy = 126
                            var points = [ [84 + 138, 156], [width - 84 - 138, 156], [width / 2, 276] ]
                            ctx.lineWidth = 1
                            for (var i = 0; i < points.length; i++) {
                                var g = ctx.createLinearGradient(cx, cy, points[i][0], points[i][1])
                                g.addColorStop(0, "#78cfe3")
                                g.addColorStop(1, "#32566b")
                                ctx.strokeStyle = g
                                ctx.beginPath()
                                ctx.moveTo(cx, cy)
                                ctx.lineTo(points[i][0], points[i][1])
                                ctx.stroke()
                            }
                            ctx.strokeStyle = "#31546a"
                            ctx.beginPath(); ctx.arc(cx, cy, 37, 0, Math.PI * 2); ctx.stroke()
                            ctx.beginPath(); ctx.arc(cx, cy, 62, 0, Math.PI * 2); ctx.stroke()
                        }
                    }
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        x: parent.width / 2 - 6; y: 120
                        color: root.stormlight
                        border.width: 4; border.color: Qt.rgba(0.47,0.81,0.89,0.28)
                    }
                    Text {
                        x: parent.width / 2 - width / 2; y: 78
                        text: "BEGIN HERE"
                        color: root.stormlight
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.letterSpacing: 3
                    }

                    Repeater {
                        model: root.guide.cosmereStarters || []
                        delegate: StarterGate {
                            required property var modelData
                            required property int index
                            guide: modelData
                            portal: root.portalFor(modelData.query)
                            width: 276; height: 116
                            x: index === 0 ? 84 : index === 1 ? entryAtlas.width - width - 84
                               : entryAtlas.width / 2 - width / 2
                            y: index < 2 ? 98 : 218
                            onActivated: root.openBook(portal ? portal.book : null)
                        }
                    }
                }
            }

            // ===== THE MAP =====
            Item {
                id: mapSection
                width: parent.width
                height: 470

                Column {
                    x: theme.margin
                    y: 34
                    spacing: 6
                    Text { text: "THE COGNITIVE ATLAS"; color: theme.gold
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "Six systems. One underlying light."; color: theme.ink
                           font.family: theme.display; font.pixelSize: 31 }
                }

                Item {
                    id: systemMap
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.topMargin: 112
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 24
                    anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin

                    Canvas {
                        anchors.fill: parent
                        opacity: 0.45
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var cx = width / 2, cy = height / 2
                            ctx.strokeStyle = "#355568"; ctx.lineWidth = 1
                            var pts = [ [130,74], [cx,44], [width-130,74],
                                        [130,height-64], [cx,height-42], [width-130,height-64] ]
                            for (var i = 0; i < pts.length; i++) {
                                ctx.beginPath(); ctx.moveTo(cx,cy); ctx.lineTo(pts[i][0],pts[i][1]); ctx.stroke()
                            }
                            ctx.strokeStyle = "#203846"
                            ctx.beginPath(); ctx.arc(cx,cy,85,0,Math.PI*2); ctx.stroke()
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 9; height: 9; radius: 5
                        color: root.cognitiveBlue
                    }
                    Repeater {
                        model: root.guide.cosmereWorlds || []
                        delegate: WorldNode {
                            required property var modelData
                            required property int index
                            world: modelData
                            width: 230; height: 74
                            x: index % 3 === 0 ? 15 : index % 3 === 1 ? systemMap.width / 2 - width / 2
                               : systemMap.width - width - 15
                            y: index < 3 ? 20 : systemMap.height - height - 20
                            onActivated: page.contentY = Math.min(page.contentHeight - page.height,
                                                                   mapSection.y + mapSection.height + index * 278)
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width - theme.margin * 2
                height: 1
                x: theme.margin
                color: Qt.rgba(0.47,0.81,0.89,0.22)
            }

            // ===== THE WORLD GATES =====
            Item {
                width: parent.width; height: 94
                Column {
                    x: theme.margin; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                    Text { text: "WORLD GATES"; color: root.cognitiveBlue
                           font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 3 }
                    Text { text: "Follow a system into Biblio"; color: theme.ink
                           font.family: theme.display; font.pixelSize: 27 }
                }
            }

            Repeater {
                model: root.uni.worlds || []
                delegate: WorldSection {
                    required property var modelData
                    required property int index
                    world: modelData
                    width: contentColumn.width
                    alternate: index % 2 === 1
                    onBookActivated: function(book) { root.openBook(book) }
                }
            }

            Item { width: 1; height: 76 }
        }

        NumberAnimation {
            id: reveal
            target: contentColumn
            property: "opacity"
            from: 0; to: 1
            duration: 460
            easing.type: Easing.OutCubic
        }
    }

    ChromeScrim { z: 16 }
    BackAction { x: theme.margin; y: 28; z: 20; onTriggered: root.backRequested() }
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: powerMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: powerMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
        }
    }

    component StarterGate: FocusScope {
        id: starter
        property var guide: ({})
        property var portal: null
        readonly property bool available: !!portal && !!portal.book
        signal activated()
        activeFocusOnTab: true

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: starter.available ? Qt.rgba(0.035,0.065,0.105,0.94) : Qt.rgba(0.025,0.04,0.065,0.88)
            border.width: starter.activeFocus ? 2 : 1
            border.color: starter.activeFocus ? root.stormlight
                         : starterMa.containsMouse && starter.available ? root.cognitiveBlue
                         : Qt.rgba(0.47,0.81,0.89,0.28)
            Behavior on border.color { ColorAnimation { duration: 140 } }
        }
        Rectangle { x: 0; y: 19; width: 2; height: parent.height - 38
                    color: starter.available ? root.cognitiveBlue : "#344858" }
        Column {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.right: cover.left; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            Text { text: starter.guide.short || "ENTRY"; color: root.cognitiveBlue
                   font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2 }
            Text { text: starter.guide.label || ""; color: theme.ink
                   font.family: theme.display; font.pixelSize: 18; width: parent.width
                   elide: Text.ElideRight }
            Text { text: starter.guide.note || ""; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 10; width: parent.width
                   wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
        }
        Image {
            id: cover
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 50; height: 76
            source: starter.available ? starter.portal.book.cover : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            opacity: status === Image.Ready ? 1 : 0
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            visible: !starter.available
            text: "RESOLVING"
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 2
        }
        MouseArea {
            id: starterMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: starter.available ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (starter.available) starter.activated()
        }
        Keys.onReturnPressed: if (starter.available) starter.activated()
        Keys.onEnterPressed: if (starter.available) starter.activated()
        Keys.onSpacePressed: if (starter.available) starter.activated()
    }

    component WorldNode: FocusScope {
        id: node
        property var world: ({})
        signal activated()
        activeFocusOnTab: true
        Rectangle {
            anchors.fill: parent
            radius: 37
            color: nodeMa.containsMouse || node.activeFocus ? Qt.rgba(0.06,0.10,0.16,0.92)
                                                         : Qt.rgba(0.025,0.045,0.075,0.82)
            border.width: node.activeFocus ? 2 : 1
            border.color: node.world.accent || root.cognitiveBlue
        }
        Rectangle { width: 8; height: 8; radius: 4; color: node.world.accent || root.cognitiveBlue
                    anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter }
        Column {
            anchors.left: parent.left; anchors.leftMargin: 42
            anchors.verticalCenter: parent.verticalCenter; spacing: 3
            Text { text: node.world.name || ""; color: theme.ink
                   font.family: theme.display; font.pixelSize: 18 }
            Text { text: node.world.epithet || ""; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 1 }
        }
        MouseArea { id: nodeMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: node.activated() }
        Keys.onReturnPressed: node.activated()
        Keys.onEnterPressed: node.activated()
        Keys.onSpacePressed: node.activated()
    }

    component WorldSection: Item {
        id: section
        property var world: ({ books: [] })
        property bool alternate: false
        signal bookActivated(var book)
        height: 278

        Rectangle {
            anchors.fill: parent
            color: section.alternate ? Qt.rgba(0.47,0.81,0.89,0.025) : "transparent"
        }
        Rectangle {
            x: theme.margin; y: 28; width: 3; height: 58
            color: section.world.accent || root.cognitiveBlue
        }
        Column {
            x: theme.margin + 18; y: 26; spacing: 5
            Text { text: section.world.name || ""; color: theme.ink
                   font.family: theme.display; font.pixelSize: 29 }
            Text { text: section.world.epithet || ""; color: section.world.accent || root.cognitiveBlue
                   font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2 }
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            y: 39
            text: (section.world.books || []).length + ((section.world.books || []).length === 1 ? " portal" : " portals")
            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
        }
        Flickable {
            id: bookWalk
            x: theme.margin; y: 108
            width: parent.width - theme.margin * 2
            height: 148
            contentWidth: gates.width
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: gates
                spacing: 14
                Repeater {
                    model: section.world.books || []
                    delegate: BookGate {
                        required property var modelData
                        portal: modelData
                        width: 230; height: 138
                        accent: section.world.accent || root.cognitiveBlue
                        onActivated: section.bookActivated(portal.book)
                    }
                }
            }
        }
    }

    component BookGate: FocusScope {
        id: gate
        property var portal: ({ book: ({}) })
        property color accent: root.cognitiveBlue
        signal activated()
        activeFocusOnTab: true
        Rectangle {
            anchors.fill: parent
            radius: 9
            color: gateMa.containsMouse ? Qt.rgba(0.07,0.105,0.16,0.96) : Qt.rgba(0.035,0.055,0.09,0.94)
            border.width: gate.activeFocus ? 2 : 1
            border.color: gate.activeFocus || gateMa.containsMouse ? gate.accent : Qt.rgba(1,1,1,0.12)
            Behavior on border.color { ColorAnimation { duration: 130 } }
        }
        Image {
            id: gateCover
            x: 8; y: 8; width: 78; height: 122
            source: gate.portal.book.cover || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
        }
        Column {
            anchors.left: gateCover.right; anchors.leftMargin: 14
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text { text: gate.portal.label || ""; color: gate.accent
                   font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1
                   width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2 }
            Text { text: gate.portal.book.title || ""; color: theme.ink
                   font.family: theme.display; font.pixelSize: 17
                   width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 3
                   elide: Text.ElideRight }
            Text { text: "OPEN IN BIBLIO  →"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 1 }
        }
        MouseArea { id: gateMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: gate.activated() }
        Keys.onReturnPressed: gate.activated()
        Keys.onEnterPressed: gate.activated()
        Keys.onSpacePressed: gate.activated()
    }
}

