// THROWAWAY — proves the corrected Monster cover in Catalog.topManga loads+decodes in Qt.
//   native/build-msvc/colosseum.exe qml/_monstercheck.qml
import QtQuick
import QtQuick.Window
import "Catalog.js" as Catalog

Window {
    id: win
    width: 400; height: 200; visible: true; color: "#05060a"; title: "monstercheck"
    property var m: Catalog.topManga.filter(function(e){ return e.caption === "Monster" })[0]

    Image {
        id: img
        anchors.centerIn: parent
        width: 120; height: 180
        source: win.m ? win.m.cover : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        onStatusChanged: {
            if (status === Image.Ready)
                console.log("[monstercheck] PASS — Monster cover decoded", sourceSize.width + "x" + sourceSize.height,
                            "from", source)
            else if (status === Image.Error)
                console.log("[monstercheck] FAIL — Monster cover errored:", source)
        }
    }
    Timer { interval: 15000; running: true
        onTriggered: { if (img.status !== Image.Ready) console.log("[monstercheck] TIMEOUT status=" + img.status); Qt.quit() } }
    Connections { target: img; function onStatusChanged() { if (img.status === Image.Ready) quitSoon.start() } }
    Timer { id: quitSoon; interval: 500; onTriggered: Qt.quit() }
}
