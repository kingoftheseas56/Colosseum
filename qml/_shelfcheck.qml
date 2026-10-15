// THROWAWAY verification harness — shows JUST the Bookshelf widget in a normal window so the whole
// shelf is visible in one frame (no fullscreen, no scroll fight). Run via the native launcher:
//   native/build/colosseum.exe qml/_shelfcheck.qml      (delete when done)
import QtQuick
import QtQuick.Window
import "Catalog.js" as Catalog

Window {
    width: 1320; height: 440
    visible: true
    color: "#05060a"
    title: "shelfcheck"

    Theme { id: theme }

    Item {
        id: wall
        anchors.fill: parent
        Image {
            anchors.fill: parent
            source: "../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.30) }
    }

    Bookshelf {
        backdrop: wall
        x: 54
        width: parent.width - 108
        anchors.verticalCenter: parent.verticalCenter
        title: "Tankoban"
        subtitle: "Manga · what's on your shelf"
        books: Catalog.topManga
    }
}
