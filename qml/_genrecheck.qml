// THROWAWAY harness - run the embeddable GenrePage in a normal window:
//   native/build-msvc/colosseum.exe qml/_genrecheck.qml
import QtQuick
import QtQuick.Window

Window {
    width: 1280; height: 800
    visible: true
    color: "#05060a"
    title: "genrecheck (Adventure)"

    GenrePage {
        anchors.fill: parent
        genreName: "Adventure"
        onSeriesRequested: (t) => console.log("READ seriesRequested:", t)
        onBackRequested: console.log("backRequested")
    }
}
