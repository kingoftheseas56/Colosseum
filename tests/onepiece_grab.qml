// Visual grab of the One Piece Grand Line page via Qt's grabToImage readback (offscreen).
// The voyage (course line + saga waypoints) is drawn, so it renders here; remote poster art
// rides IPv4-pinned hosts in the real app but may be blank on this bare stack.
import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1360; height: 940; visible: true
    color: "#04070a"

    UI.OnePieceUniversePage {
        id: page
        anchors.fill: parent
        universeName: "One Piece"
    }

    Timer {
        interval: 7000; running: true; repeat: false
        onTriggered: {
            var ok = page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/onepiece-grab.png");
                console.log("GRAB " + (saved ? "OK" : "FAIL"));
                Qt.exit(saved ? 0 : 1);
            });
            if (!ok) { console.log("GRAB request rejected"); Qt.exit(2); }
        }
    }
}
