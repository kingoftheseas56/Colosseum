// THROWAWAY harness - run the cinematic (MCU) page standalone:
//   qml.exe qml/_cinemacheck.qml      (delete when done)
import QtQuick
import QtQuick.Window

Window {
    width: 1280; height: 820
    visible: true
    color: "#08070b"
    title: "cinemacheck (MCU)"

    CinematicPage {
        anchors.fill: parent
        onBackRequested: console.log("backRequested")
        onWatchRequested: (item) => console.log("WATCH watchRequested:", JSON.stringify(item))
    }
}
