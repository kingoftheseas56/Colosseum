pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import "DockWorldMapGeometry.js" as Geometry

Item {
    id: atlas
    objectName: "dockWorldMap"

    signal worldRequested(string world)
    property Item firstWorldAction

    readonly property var worlds: [
        { key: "tankoban", name: qsTr("Tankoban"), tagline: qsTr("The Land of Manga,\nManhwa and Comics"), inscription: "単行本", icon: "comic-book.svg", labelWidth: 205, nameSize: 25 },
        { key: "theatre", name: qsTr("Theatre"), tagline: qsTr("The Land of Movies,\nTV and Anime"), inscription: "劇場", icon: "projector-theatre.svg", labelWidth: 235, nameSize: 40 },
        { key: "biblio", name: qsTr("Biblio"), tagline: qsTr("The Land of Books\nand Audiobooks"), inscription: "図書", icon: "book-library.svg", labelWidth: 153, nameSize: 25 },
        { key: "universes", name: qsTr("Universes"), tagline: qsTr("The Land of Franchises"), inscription: "宇宙", icon: "universes-atom.svg", labelWidth: 175, nameSize: 26 },
        { key: "vault", name: qsTr("Vault"), tagline: qsTr("The Land of Your\nCollection"), inscription: "宝庫", icon: "vault-door.svg", labelWidth: 150, nameSize: 28 },
        { key: "feria", name: qsTr("Feria"), tagline: qsTr("The Land of the Internet"), inscription: "電脳", labelWidth: 174, nameSize: 22, locked: true },
        { key: "vinyl", name: qsTr("Vinyl"), tagline: qsTr("The Land of Music"), inscription: "音楽", labelWidth: 150, nameSize: 25, locked: true }
    ]
    readonly property var lands: worlds.map(world => ({
        path: Geometry.lands[world.key].path,
        x: Geometry.lands[world.key].x,
        y: Geometry.lands[world.key].y,
        locked: world.locked === true
    }))

    Theme { id: theme }
    FontLoader { id: labelFont; source: "../assets/fonts/cinzel/Cinzel-Variable.ttf" }

    function focusFirstWorld() {
        if (firstWorldAction)
            firstWorldAction.forceActiveFocus(Qt.TabFocusReason)
    }

    DockWorldMapMaterial { anchors.fill: parent }

    Item {
        id: chart
        anchors.centerIn: parent
        width: 932
        height: 544
        scale: Math.max(0, Math.min(atlas.width / width, atlas.height / height))
        visible: scale > 0

        DockWorldMapMaterial {
            anchors.fill: parent
            lands: atlas.lands
        }

        Repeater {
            model: atlas.worlds

            delegate: Item {
                id: island
                required property var modelData
                readonly property var geography: Geometry.lands[modelData.key]
                readonly property bool locked: modelData.locked === true
                readonly property color ink: locked ? "#382419" : "#2b180e"
                anchors.fill: chart

                Shape {
                    id: coast
                    x: island.geography.x
                    y: island.geography.y
                    width: island.geography.width
                    height: island.geography.height
                    preferredRendererType: Shape.CurveRenderer
                    containsMode: Shape.FillContains
                    ShapePath {
                        fillRule: ShapePath.OddEvenFill
                        fillColor: "transparent"
                        strokeColor: !island.locked && (coastHover.hovered || action.activeFocus)
                            ? "#fff0ac" : "transparent"
                        strokeWidth: 2.5
                        joinStyle: ShapePath.RoundJoin
                        PathSvg { path: island.geography.path }
                    }
                }

                Column {
                    id: lettering
                    objectName: "dockWorldLabel_" + island.modelData.key
                    x: island.geography.labelX - width / 2
                    y: island.geography.labelY - height / 2
                    width: island.modelData.labelWidth
                    spacing: island.locked ? 2 : 3

                    Image {
                        readonly property int markSize: island.modelData.key === "theatre" ? 43 : 30
                        visible: !island.locked
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: markSize
                        height: markSize
                        sourceSize: Qt.size(markSize * 2, markSize * 2)
                        source: visible ? "../assets/icons/" + island.modelData.icon : ""
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1
                            colorizationColor: island.ink
                        }
                    }

                    Shape {
                        visible: island.locked
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 17
                        height: 17
                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: island.ink
                            strokeWidth: 1.5
                            joinStyle: ShapePath.RoundJoin
                            capStyle: ShapePath.RoundCap
                            PathSvg { path: "M4,7 H13 V15 H4 Z M6,7 V5 A2.5,2.5 0 0 1 11,5 V7 M8.5,10 V12" }
                        }
                    }

                    Text {
                        width: parent.width
                        text: island.modelData.name
                        color: island.ink
                        font.family: labelFont.status === FontLoader.Ready ? labelFont.name : theme.display
                        font.pixelSize: island.modelData.nameSize
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 16
                    }

                    Text {
                        width: parent.width
                        text: island.modelData.tagline
                        color: island.ink
                        font.family: theme.display
                        font.italic: true
                        font.pixelSize: island.modelData.key === "theatre" ? 18 : 14
                        lineHeight: 1.05
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        width: parent.width
                        text: island.modelData.inscription
                        color: island.ink
                        font.pixelSize: island.modelData.key === "theatre" ? 23 : 16
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                KeyboardAction {
                    id: action
                    objectName: "dockWorld_" + island.modelData.key
                    anchors.fill: coast
                    containmentMask: QtObject {
                        function contains(point: point): bool {
                            return coast.contains(point)
                        }
                    }
                    enabled: !island.locked
                    pointerEnabled: false
                    focusOnPointer: false
                    showFocusFrame: false
                    spaceActivates: true
                    accessibleName: island.modelData.name
                    accessibleDescription: island.modelData.tagline
                    onTriggered: atlas.worldRequested(island.modelData.key)
                    Component.onCompleted: {
                        if (island.modelData.key === "tankoban")
                            atlas.firstWorldAction = action
                    }

                    HoverHandler { id: coastHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        gesturePolicy: TapHandler.DragThreshold
                        onTapped: action.activate(Qt.MouseFocusReason)
                    }
                }

                Item {
                    x: lettering.x
                    y: lettering.y
                    width: lettering.width
                    height: lettering.height
                    visible: island.locked
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("%1, locked").arg(island.modelData.name)
                    Accessible.description: island.modelData.tagline
                }
            }
        }
    }
}
