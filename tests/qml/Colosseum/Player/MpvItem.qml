import QtQuick 2.15
Item {
    property real position: 0
    property real duration: 0
    property bool pause: true
    property real speed: 1
    property real volume: 100
    property bool mute: false
    signal fileLoaded()
    signal endFile()
}
