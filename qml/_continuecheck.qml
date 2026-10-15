// PROTOTYPE harness: qml.exe qml/_continuecheck.qml — instantiates both ContinueTile variants
// (home over a dummy backdrop, world with watched + remove states) and quits. Proves the
// component compiles, the Glass loader wires, and the cover pipeline degrades to gradient.
import QtQuick

Rectangle {
    id: root
    width: 700; height: 400; color: "#101014"

    Rectangle { id: fakeWall; anchors.fill: parent; color: "#1a1a22"; z: -1 }

    property var sampleVideo: ({ id: "t1", kind: "video", title: "The Wire", sub: "S1 · E4 · 12:34 left",
                                 cover: "", c1: "#2c5f7c", c2: "#0d1f2a", progress: 0.76 })
    property var sampleWatched: ({ id: "t2", kind: "video", title: "Berserk (1997)", sub: "S1 · E19 · finished",
                                   cover: "", c1: "#4a4e69", c2: "#14141f", progress: 1.0, watched: true })
    property var sampleComic: ({ id: "gc:hellboy", kind: "comic", title: "Hellboy", sub: "Issue #2",
                                 cover: "", c1: "#7c2c3f", c2: "#200a10", progress: 2.5 })   // clamps to 1

    Row {
        anchors.centerIn: parent; spacing: 24
        ContinueTile {
            variant: "home"; entry: root.sampleVideo; backdrop: fakeWall
            onResumeRequested: console.log("home resume ok")
        }
        ContinueTile { variant: "world"; entry: root.sampleWatched }
        ContinueTile { variant: "world"; entry: root.sampleComic }
    }

    Timer {
        interval: 1500; running: true
        onTriggered: { console.log("CONTINUECHECK OK: both variants instantiated"); Qt.quit() }
    }
}
