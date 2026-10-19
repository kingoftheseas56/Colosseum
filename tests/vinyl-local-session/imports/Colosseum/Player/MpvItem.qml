import QtQuick
// Contract-only fake; native-app qualification must use the real MpvItem.
Item {
    property real position: 0
    property real duration: 120
    property bool pause: true
    property var audioTracks: [{id:1}]
    property string loadedUrl: ""
    property real lastSeek: -1
    signal fileLoaded()
    signal endFile(string reason)
    signal playbackError(string code, string message)
    function loadFileWithHeaders(url, headers) { loadedUrl=url; }
    function seekExact(seconds) { lastSeek=seconds; position=seconds; }
}
