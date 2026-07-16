// Harness.qml — TASK 5 root window. Boots into the book shelf; clicking a book
// hides the shelf and reveals the ReaderShell (the paper). Esc in the shell
// returns to the shelf. No Colosseum chrome — this is the standalone "first
// pixels" harness that proves the paper loads and reports position.
//
// [Agent 2 (Claude), biblio]
import QtQuick

Window {
    id: win
    width: 1280
    height: 720
    visible: true
    title: "reader2 harness"
    color: "#000000"

    HarnessShelf {
        id: shelf
        anchors.fill: parent
        visible: true
        onBookChosen: (path) => {
            shell.openBook(path)
            shelf.visible = false
            shell.visible = true
            shell.forceActiveFocus()
        }
    }

    ReaderShell {
        id: shell
        anchors.fill: parent
        visible: false
        onClosed: {
            shell.visible = false
            shelf.visible = true
        }
    }
}
