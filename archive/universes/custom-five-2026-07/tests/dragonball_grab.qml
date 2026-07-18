// Visual grab of the Dragon Ball saga page via Qt's grabToImage readback (offscreen).
// The seven-orb signature is drawn (no network), so it renders fully here; remote poster
// art rides IPv4-pinned hosts in the real app but may be blank on this bare stack — the
// layout + orbs are the proof shot.
import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1360; height: 940; visible: true
    color: "#05040a"

    UI.DragonBallUniversePage {
        id: page
        anchors.fill: parent
        universeName: "Dragon Ball"
    }

    Timer {
        interval: 7000; running: true; repeat: false
        onTriggered: {
            var ok = page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/dragonball-grab.png");
                console.log("GRAB " + (saved ? "OK" : "FAIL"));
                Qt.exit(saved ? 0 : 1);
            });
            if (!ok) { console.log("GRAB request rejected"); Qt.exit(2); }
        }
    }
}
