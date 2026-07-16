// ReaderShell.qml — the reader component Biblio embeds on swap day (Task 16).
// v1 = the paper + temporary keyboard page-turns (Right/Space/PageDown → next,
// Left/PageUp → prev, Esc → back to the shelf). The real native chrome arrives in
// Task 7; this is just enough to prove the paper turns pages under a Qt viewport.
//
// [Agent 2 (Claude), biblio]
import QtQuick

FocusScope {
    id: shell
    property string bookPath: ""
    property string bookId: bookPath
    signal closed()
    focus: true

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Right || e.key === Qt.Key_Space || e.key === Qt.Key_PageDown) { paper.next(); e.accepted = true }
        else if (e.key === Qt.Key_Left || e.key === Qt.Key_PageUp) { paper.prev(); e.accepted = true }
        else if (e.key === Qt.Key_Escape) { shell.closed(); e.accepted = true }
    }

    Paper {
        id: paper
        anchors.fill: parent
        onGlueUpChanged: if (glueUp && shell.bookPath !== "") paper.open(shell.bookPath, "")
        onPaperEvent: (name, p) => console.log("[shell]", name, JSON.stringify(p).slice(0, 160))
    }

    function openBook(path) { bookPath = path; if (paper.glueUp) paper.open(path, "") }
}
