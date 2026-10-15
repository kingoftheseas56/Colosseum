import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: home
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX

    function scrollHome() { page.contentY = 0 }
    function revealShelf(index) {
        var target = topHalf.height + index * (25.5 * u) - 4.7 * u
        page.contentY = Math.max(0, Math.min(page.contentHeight - page.height, target))
    }

    Item {
        id: backdropLayer
        anchors.fill: parent

        Image {
            id: ambientArt
            anchors { left:parent.left; right:parent.right; top:parent.top }
            height: parent.height * 0.82
            source: controller.artUrl(controller.featured().item, true)
            fillMode: Image.PreserveAspectCrop
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignTop
            opacity: status === Image.Ready ? 0.42 : 0
            asynchronous: true
            cache: true
            Behavior on opacity { NumberAnimation { duration: 500 } }
        }
        Rectangle {
            anchors.fill: ambientArt
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position:0; color:Qt.rgba(6/255,7/255,11/255,0.86) }
                GradientStop { position:0.62; color:Qt.rgba(6/255,7/255,11/255,0.20) }
                GradientStop { position:1; color:Qt.rgba(6/255,7/255,11/255,0.50) }
            }
        }
        Rectangle {
            anchors { left:ambientArt.left; right:ambientArt.right; bottom:ambientArt.bottom }
            height: ambientArt.height * 0.55
            gradient: Gradient {
                GradientStop { position:0; color:"transparent" }
                GradientStop { position:1; color:controller.night }
            }
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: homeColumn.implicitHeight + 9 * u
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: true

        Column {
            id: homeColumn
            width: page.width
            spacing: 0
            Item {
                id: topHalf
                width: parent.width
                height: Math.max(44 * u, page.height - 12 * u)

                Column {
                    anchors { left:parent.left; right:parent.right; bottom:parent.bottom; leftMargin:m; rightMargin:m; bottomMargin:1.2*u }
                    spacing: 0.9 * u

                    Item {
                        id: feature
                        width: Math.min(52 * u, parent.width)
                        height: 15.3 * u
                        Column {
                            anchors { left:parent.left; right:parent.right; bottom:parent.bottom; bottomMargin:1.1*u }
                            spacing: 0.5 * u
                            Row {
                                spacing: 0.6 * u
                                PorticoCombinedGlyph {
                                    width:1.1*u; height:1.1*u
                                    glyphKey: controller.featured().app || "plus"
                                    tone: controller.mist
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: controller.featured().source
                                    color: controller.mist
                                    font.family: controller.uiFont
                                    font.pixelSize: 0.95 * u
                                }
                            }
                            Text {
                                width: parent.width
                                text: controller.featured().item ? controller.featured().item.t : controller.providerName(controller.selectedApp)
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 4 * u
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Row {
                                spacing: 1.2 * u
                                visible: !!controller.featured().item
                                Text {
                                    text: controller.featured().item ? controller.featured().item.y : ""
                                    color: "#f7f7f5"
                                    font.family: controller.uiFont
                                    font.pixelSize: 0.95 * u
                                }
                                Repeater {
                                    model: controller.featured().item ? controller.featured().item.f : []
                                    delegate: Text {
                                        required property string modelData
                                        text: modelData
                                        color: "#c9c8d0"
                                        font.family: controller.uiFont
                                        font.pixelSize: 0.95 * u
                                    }
                                }
                            }
                            Text {
                                width: Math.min(40*u, parent.width)
                                text: controller.featured().item ? controller.featured().item.s : "Opens " + controller.providerName(controller.selectedApp) + " inside Colosseum."
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 1.02 * u
                                lineHeight: 1.55
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: true
                                opacity: controller.focusArea === "feature" ? 1 : 0
                                width: 7 * u; height: 2.2 * u; radius:0.65*u
                                color: controller.gold
                                Text { anchors.centerIn:parent; text:controller.featured().item ? "Details" : "Open"; color:Qt.rgba(0,0,0,0.86); font.family:controller.uiFont; font.pixelSize:0.85*u; font.weight:Font.Bold }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: controller.focusArea = "feature"
                            onClicked: controller.featured().item ? controller.openTitle(controller.featured().item.id) : controller.openHost(controller.selectedApp,"","home")
                        }
                    }

                    GridLayout {
                        id: appsGrid
                        width: parent.width
                        columns: controller.appColumns()
                        columnSpacing: 1.2 * u
                        rowSpacing: 1.2 * u
                        Repeater {
                            model: controller.activeApps.length + 1
                            delegate: PorticoCombinedParityAppTile {
                                required property int index
                                property string pk: index < controller.activeApps.length ? controller.activeApps[index] : ""
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 0.6
                                unit: u
                                providerKey: pk
                                title: pk ? controller.providerName(pk) : "Apps"
                                numberText: index < 9 && pk ? String(index + 1) : ""
                                selected: controller.focusArea === "app" && controller.focusIndex === index
                                moving: controller.movingApp && selected
                                addMode: !pk
                                displayFont: controller.displayFont
                                ink: controller.ink; mist:controller.mist; slate:controller.slate; gold:controller.gold
                                onEntered: { controller.selectApp(index) }
                                onTriggered: {
                                    controller.selectApp(index)
                                    if (pk) controller.openHost(pk,"","home"); else controller.viewState = "apps"
                                }
                            }
                        }
                    }
                }
            }

            Repeater {
                model: controller.shownShelves()
                delegate: Item {
                    id: shelf
                    required property var modelData
                    required property int index
                    width: homeColumn.width
                    height: 25.5 * u

                    Row {
                        x: m
                        y: 1.3 * u
                        spacing: 1 * u
                        Text {
                            text: modelData.title
                            color: controller.ink
                            font.family: controller.displayFont
                            font.pixelSize: 1.75 * u
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: modelData.chips ? controller.providerName(controller.chipSelection[modelData.id]) + " chart" : (modelData.src || "")
                            color: controller.slate
                            font.family: controller.uiFont
                            font.pixelSize: 0.85 * u
                        }
                    }

                    Row {
                        visible: !!modelData.chips
                        anchors { right:parent.right; rightMargin:m; top:parent.top; topMargin:1.15*u }
                        spacing: 0.4 * u
                        Repeater {
                            model: modelData.chips ? Object.keys(modelData.chips).filter(function(pk){ return controller.activeApps.indexOf(pk)>=0 }) : []
                            delegate: PorticoCombinedPill {
                                required property string modelData
                                unit: u
                                compact: true
                                label: controller.providerName(modelData)
                                selected: controller.chipSelection[shelf.modelData.id] === modelData
                                onTriggered: {
                                    var next = ({ albums:controller.chipSelection.albums, artists:controller.chipSelection.artists })
                                    next[shelf.modelData.id] = modelData
                                    controller.chipSelection = next
                                }
                            }
                        }
                    }

                    ListView {
                        id: rail
                        anchors { left:parent.left; right:parent.right; top:parent.top; topMargin:4.7*u }
                        height: 20.8 * u
                        orientation: ListView.Horizontal
                        spacing: 1.25 * u
                        leftMargin: m
                        rightMargin: m
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: controller.shelfItems(modelData)
                        delegate: PorticoCombinedParityMediaCard {
                            required property string modelData
                            required property int index
                            property var it: controller.titleObj(modelData)
                            unit: u
                            width: it && it.k === "album" ? 13*u : (it && it.k === "artist" ? 10.5*u : 9.25*u)
                            title: it ? it.t : ""
                            sub: it ? (it.k === "artist" ? it.f[0] : (it.k === "album" ? it.by : it.y + "   " + Data.KIND[it.k])) : ""
                            artSource: controller.artUrl(it, false)
                            shape: controller.shapeFor(it)
                            rankText: shelf.modelData.rank ? String(index + 1) : ""
                            selected: controller.focusArea === "shelf" && controller.shelfIndex === shelf.index && controller.shelfCardIndex === index
                            toneA: controller.toneFor(modelData)[0]
                            toneB: controller.toneFor(modelData)[1]
                            displayFont: controller.displayFont
                            ink:controller.ink; mist:controller.mist; slate:controller.slate; gold:controller.gold
                            onEntered: { controller.focusArea="shelf"; controller.shelfIndex=shelf.index; controller.shelfCardIndex=index }
                            onTriggered: controller.openTitle(modelData)
                        }
                        Connections {
                            target: controller
                            function onShelfCardIndexChanged() {
                                if (controller.focusArea === "shelf" && controller.shelfIndex === shelf.index)
                                    rail.positionViewAtIndex(controller.shelfCardIndex, ListView.Contain)
                            }
                        }
                    }
                }
            }

            Text {
                x: m
                width: parent.width - 2*m
                topPadding: 2*u
                bottomPadding: 4*u
                text: "Film and series shelves come from the Streaming Catalogs addon for Stremio; album and artist charts from Spotify and YouTube Music; reading shelves from their stores. Every listing here is a sample. A service decides what your region and plan include."
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.82 * u
                lineHeight: 1.5
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        anchors { left:parent.left; right:parent.right; top:parent.top }
        height: 4.25 * u
        visible: page.contentY > 40
        color: Qt.rgba(8/255,10/255,16/255,0.88)
        border.width: 0
        z: 35
    }
    PorticoCombinedParityMast {
        anchors { left:parent.left; right:parent.right; top:parent.top }
        controller: home.controller
        backdrop: backdropLayer
    }
}
