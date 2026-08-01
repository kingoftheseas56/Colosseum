// Deterministic scene for the lanista harness. Every interactive element is
// objectNamed — the bridge's whole targeting model rests on that discipline.
import QtQuick

Window {
    id: win
    objectName: "harnessWindow"
    width: 800; height: 600
    visible: true
    title: "LanistaHarness"

    property int clickCount: 0

    Rectangle { anchors.fill: parent; color: "#101218" }

    Rectangle {
        id: counterButton
        objectName: "counterButton"
        x: 100; y: 100; width: 200; height: 48
        radius: 8; color: ma.containsMouse ? "#2a3242" : "#1a2030"
        Text {
            objectName: "counterLabel"
            anchors.centerIn: parent
            text: "clicks: " + win.clickCount
            color: "#f0f0f0"
        }
        MouseArea {
            id: ma; objectName: "counterMouse"
            anchors.fill: parent; hoverEnabled: true
            onClicked: win.clickCount++
        }
    }

    TextInput {
        id: field
        objectName: "nameField"
        x: 100; y: 200; width: 200; height: 32
        color: "#f0f0f0"; font.pixelSize: 16
    }

    // An item partly OUTSIDE the window — geometry assertions must see this.
    Rectangle {
        objectName: "clippedBox"
        x: win.width - 40; y: 300; width: 120; height: 40; color: "#803030"
    }

    // A core-QtQuick ListView (no Controls import): its leaf class is
    // "QQuickListView", which carries NO "Flickable" token — so ui-snapshot must
    // walk the SUPERCLASS chain (QQuickListView -> QQuickFlickable) to mark it
    // interactive. This is the fixture that pins the chain-walk over a leaf check.
    // A scrollable ListView: contentHeight (20*24=480) far exceeds height (120),
    // so a wheel scroll has somewhere to go — ui-scroll asserts contentY moves.
    ListView {
        objectName: "mainList"
        x: 100; y: 260; width: 200; height: 120; clip: true
        model: 20
        delegate: Rectangle {
            required property int index
            objectName: "listRow" + index
            width: 200; height: 24
            color: index % 2 ? "#181c26" : "#12151d"
            Text { text: "item " + parent.index; color: "#c0c0c0"; x: 6; y: 4 }
        }
    }

    Flickable {
        objectName: "longList"
        x: 400; y: 100; width: 300; height: 400
        contentWidth: width; contentHeight: col.height
        Column {
            id: col
            Repeater {
                model: 40
                delegate: Rectangle {
                    required property int index
                    objectName: "row" + index
                    width: 300; height: 50
                    color: index % 2 ? "#181c26" : "#12151d"
                    Text { text: "row " + parent.index; color: "#c0c0c0"; x: 8; y: 14 }
                }
            }
        }
    }

    // A focusable key sink for ui-keypress: a plain Item has no click-to-focus of
    // its own, so its MouseArea calls forceActiveFocus() — ui-click the area to
    // focus it, then ui-keypress lands on Keys.onPressed and lastKey records e.text.
    Item {
        id: keySink
        objectName: "keySink"
        x: 100; y: 400; width: 200; height: 32
        focus: true
        property string lastKey: ""
        Keys.onPressed: (event) => { keySink.lastKey = event.text }
        MouseArea {
            objectName: "keySinkMouse"
            anchors.fill: parent
            onClicked: keySink.forceActiveFocus()
        }
    }
}
