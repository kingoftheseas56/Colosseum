// THROWAWAY harness — smoke MangaReader's parse/load + HUD/keyboard wiring:
//   native\build-msvc\colosseum.exe qml\_readercheck.qml      (delete when done)
// No chapter is loaded (max=0 → download panel); this confirms the QML compiles and the new
// auto-hide / Keys.onPressed edits are syntactically valid. Behavior needs a real chapter.
import QtQuick
import QtQuick.Window

Window {
    width: 1360; height: 900; visible: true
    color: "#000000"
    title: "readercheck"
    MangaReader {
        anchors.fill: parent
        focus: true
        onBackRequested: console.log("READER back")
    }
}
