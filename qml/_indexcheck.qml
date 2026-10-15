// THROWAWAY harness — smoke the GenreIndex ("Explore") page standalone:
//   native\build-msvc\colosseum.exe qml\_indexcheck.qml      (delete when done)
import QtQuick
import QtQuick.Window

Window {
    width: 1440; height: 900; visible: true
    color: "#05060a"
    title: "GenreIndex smoke"
    GenreIndex {
        anchors.fill: parent
        onGenrePicked: (name) => console.log("INDEX picked genre:", name)
        onBackRequested: console.log("INDEX back")
        onSearchClicked: console.log("INDEX search")
    }
}
