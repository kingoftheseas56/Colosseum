// Visual grab of the Extensions Discover view (real logos) via Qt's own
// grabToImage readback — works offscreen without an external window capture.
// The C++ `Extensions` registry is absent here, so the guarded refresh() no-ops
// and the Installed pane is empty; the Discover rails come from ExtensionsCatalog.js
// (pure JS) and render the real bundled logos — exactly the proof shot we want.
import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1320; height: 900; visible: true
    color: "#05060a"

    UI.ExtensionsPage {
        id: page
        anchors.fill: parent
    }

    Timer {
        interval: 4000; running: true; repeat: false
        onTriggered: {
            var ok = page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/extensions-logos-grab.png");
                console.log("GRAB " + (saved ? "OK" : "FAIL"));
                Qt.exit(saved ? 0 : 1);
            });
            if (!ok) { console.log("GRAB request rejected"); Qt.exit(2); }
        }
    }
}
