// PROTOTYPE harness: qml.exe qml/_backcheck.qml — instantiates every BackAction variant and
// quits. Proves the component compiles and renders (Shape chevron, Theme, tooltip machinery).
import QtQuick

Rectangle {
    width: 420; height: 200; color: "#101014"

    Row {
        anchors.centerIn: parent; spacing: 30
        BackAction { onTriggered: console.log("plain ok") }
        BackAction { label: "Home"; labelSize: 14 }
        BackAction { variant: "capsule"; tip: "Back" }
        BackAction { variant: "immersive"; tip: "Back to series" }
    }

    Timer {
        interval: 1500; running: true
        onTriggered: { console.log("BACKCHECK OK: all variants instantiated"); Qt.quit() }
    }
}
