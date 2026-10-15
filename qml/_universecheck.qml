// THROWAWAY harness - run the now-embeddable UniversePage in a normal window:
//   qml.exe qml/_universecheck.qml      (delete when done)
import QtQuick
import QtQuick.Window

Window {
    width: 1280; height: 800
    visible: true
    color: "#05060a"
    title: "universecheck (One Piece)"

    UniversePage {
        anchors.fill: parent
        universeName: "One Piece"
        onSeriesRequested: (t) => console.log("READ seriesRequested:", t)
        onWatchRequested: (item) => console.log("WATCH watchRequested:", JSON.stringify(item))
        onBackRequested: console.log("backRequested")
    }
}
