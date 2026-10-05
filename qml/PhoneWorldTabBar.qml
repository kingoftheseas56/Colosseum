pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: root

    property var worlds: []
    property string activeTab: ""
    signal worldRequested(string world)
    signal profileRequested()

    readonly property var tabs: {
        var out = []
        for (var i = 0; i < root.worlds.length; ++i) {
            var world = root.worlds[i]
            out.push({
                name: String(world.name || ""),
                icon: String(world.icon || ""),
                profile: false
            })
        }
        out.push({
            name: "Profile",
            icon: "../assets/icons/your-colosseum-laurel.svg",
            profile: true
        })
        return out
    }

    implicitHeight: 72
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.039, 0.043, 0.063, 0.86)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(1, 1, 1, 0.18)
    }

    Row {
        id: row
        anchors.fill: parent
        anchors.bottomMargin: 8

        Repeater {
            model: root.tabs.length
            delegate: Item {
                id: tab
                required property int index
                readonly property var entry: root.tabs[index]
                readonly property bool active: root.activeTab === entry.name

                objectName: "phoneWorldTab_" + entry.name
                width: row.width / Math.max(1, root.tabs.length)
                height: row.height
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
                    anchors.topMargin: 10
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
                    font.family: "Segoe UI"
                    font.pixelSize: 11
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                }

                TapHandler { onTapped: tab.activate() }
                Keys.onReturnPressed: tab.activate()
                Keys.onEnterPressed: tab.activate()
            }
        }
    }
}
