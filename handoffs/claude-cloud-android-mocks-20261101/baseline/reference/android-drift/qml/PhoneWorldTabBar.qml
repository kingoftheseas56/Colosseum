pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: root

    property var worlds: []
    property string activeTab: ""
    signal worldRequested(string world)
    signal profileRequested()

    Theme { id: theme }
    // Respect the real platform inset; Android system navigation is hidden while Colosseum is foreground.
    readonly property real navigationBottomPadding: SafeArea.margins.bottom

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

    AdaptiveLayout { id:metrics; page:root }
    readonly property bool shortWindow: metrics.shortLandscape
    implicitHeight: (shortWindow ? Math.max(56, 38 + Math.ceil(theme.fontSize(14)))
        : Math.max(72, 46 + Math.ceil(theme.fontSize(14)))) + navigationBottomPadding
    height: implicitHeight

    Rectangle {
        objectName: "phoneWorldTabBarBackground"
        anchors.fill: parent
        color: "#0a0b10"
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
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        anchors.bottomMargin: root.navigationBottomPadding + (root.shortWindow ? 6 : 8)

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

                Rectangle {
                    objectName: "phoneWorldTabFeedback_" + tab.entry.name
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 12
                    color: theme.gold
                    opacity: (touch.pressed && !touch.moved) || tab.activeFocus ? 0.16 : 0
                }

                Image {
                    id: sourceGlyph
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: root.shortWindow ? 6 : 10
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
                    // The shared Universes atom asset is black for its light-surface uses.
                    // Raise its luminance before tinting so the glyph remains visible here.
                    brightness: tab.entry.name === "Universes" ? 1.0 : 0.0
                    colorization: 1.0
                    colorizationColor: tab.active ? "#f0c44a" : "#9a99a5"
                }

                Text {
                    objectName: "phoneWorldTabLabel_" + tab.entry.name
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: sourceGlyph.bottom
                    anchors.topMargin: 4
                    text: tab.entry.name
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    color: tab.active ? "#f0c44a" : "#9a99a5"
                    font.family: theme.ui
                    font.pixelSize: theme.fontSize(11)
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    id: touch
                    objectName: "phoneWorldTabHitArea_" + tab.entry.name
                    anchors.fill: parent
                    property point startPoint
                    property bool moved: false
                    preventStealing: false
                    propagateComposedEvents: false
                    onPressed: function(mouse) {
                        startPoint = Qt.point(mouse.x, mouse.y)
                        moved = false
                    }
                    onPositionChanged: function(mouse) {
                        if (pressed && Math.hypot(mouse.x - startPoint.x, mouse.y - startPoint.y)
                                > Qt.styleHints.startDragDistance) moved = true
                    }
                    onCanceled: moved = true
                    onClicked: if (!moved) tab.activate()
                }

                Keys.onReturnPressed: tab.activate()
                Keys.onEnterPressed: tab.activate()
            }
        }
    }
}
