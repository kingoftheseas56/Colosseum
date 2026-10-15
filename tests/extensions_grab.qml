// Visual grab of the Extensions Store (House hero + live stremio-addons.net rows) via Qt's own
// grabToImage readback — works offscreen without an external window capture. The C++
// `Extensions` registry is absent here, so install state reads as "not installed".
import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1320; height: 900; visible: true
    color: "#05060a"

    UI.ExtensionsStorePage {
        id: page
        anchors.fill: parent
    }

    Timer {
        interval: 10000; running: true; repeat: false
        onTriggered: {
            var ok = page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/extensions-store-grab.png");
                console.log("GRAB " + (saved ? "OK" : "FAIL"));
                Qt.exit(saved ? 0 : 1);
            });
            if (!ok) { console.log("GRAB request rejected"); Qt.exit(2); }
        }
    }
}
