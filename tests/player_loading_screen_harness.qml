import QtQuick
import QtQuick.Window
import "../qml"

// Eyes-on harness (Task 6): renders PlayerLoadingScreen full-window with sample data so the
// cinematic loader can be reviewed in isolation. Run with a Qt qml runtime that has QtQuick.Effects.
Window {
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "PlayerLoadingScreen harness"

    PlayerLoadingScreen {
        anchors.fill: parent
        active: true
        title: "Attack on Titan"
        episodeLine: "S4 · E28 · The Dawn of Humanity"
        statusText: "Preparing stream"
        hudFamily: "Segoe UI"
        // backdropUrl / logoUrl intentionally empty here to exercise the title fallback + black base.
        onCancelRequested: console.log("harness: cancelRequested")
    }
}
