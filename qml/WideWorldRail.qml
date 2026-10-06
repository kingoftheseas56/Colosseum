pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: root

    objectName: "wideWorldRail"
    property var worlds: []
    property string activeTab: ""
    signal worldRequested(string world)
    signal profileRequested()

    Theme { id: theme }

    readonly property var tabs: {
        var out = []
        for (var i = 0; i < root.worlds.length; ++i) {
            var world = root.worlds[i]
            out.push({
                name: String(world.name || ""),
                icon: String(world.icon || ""),
                profile: false,
            })
        }
        out.push({
            name: "Profile",
            icon: "../assets/icons/your-colosseum-laurel.svg",
            profile: true,
        })
        return out
    }

    implicitWidth: 92
    width: implicitWidth

    Rectangle {
        objectName: "wideWorldRailBackground"
        anchors.fill: parent
        color: "#0a0b10"
    }

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Qt.rgba(1, 1, 1, 0.18)
    }

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 18

        Repeater {
            model: root.tabs.length

            delegate: Item {
                id: tab
                required property int index
                readonly property var entry: root.tabs[index]
                readonly property bool active: root.activeTab === entry.name

                objectName: "wideWorldRailTab_" + entry.name
                width: column.width
                height: 64
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: entry.name
                Accessible.onPressAction: tab.activate()

                function activate() {
                    if (entry.profile)
                        root.profileRequested()
                    else
                        root.worldRequested(entry.name)
                }

                Image {
                    id: sourceGlyph
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 9
                    width: 22
                    height: 22
                    source: tab.entry.icon
                    sourceSize.width: 44
                    sourceSize.height: 44
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    visible: false
                }

                MultiEffect {
                    anchors.fill: sourceGlyph
                    source: sourceGlyph
                    colorization: 1.0
                    colorizationColor: tab.active ? "#f0c44a" : "#9a99a5"
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: sourceGlyph.bottom
                    anchors.topMargin: 4
                    text: tab.entry.name
                    color: tab.active ? "#f0c44a" : "#9a99a5"
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    objectName: "wideWorldRailHitArea_" + tab.entry.name
                    anchors.fill: parent
                    preventStealing: true
                    propagateComposedEvents: false
                    onClicked: tab.activate()
                }

                Keys.onReturnPressed: tab.activate()
                Keys.onEnterPressed: tab.activate()
            }
        }
    }
}
