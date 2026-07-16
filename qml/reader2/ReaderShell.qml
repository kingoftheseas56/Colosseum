// ReaderShell.qml — the reader component Biblio embeds on swap day (Task 16).
// v1 = the paper + temporary keyboard page-turns (Right/Space/PageDown → next,
// Left/PageUp → prev, Esc → back to the shelf) + the RESUME SEAM (Task 6): every
// page turn persists position to the SAME progress.json the old reader uses, and
// reopening a book returns to where you left off. The real native chrome arrives
// in Task 7.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

FocusScope {
    id: shell
    property string bookPath: ""
    // Store key = the SHA1[:20] fingerprint of the path, NOT the raw path. The old
    // reader keyed progress/bookmarks/annotations by this (BookBridge::progressKey);
    // deriving it here is what makes positions/marks survive the swap (zero migration).
    // Reader2Bridge.bookKey mirrors that derivation byte-for-byte (both delegate to
    // BookStores::keyFor, the single shared formula).
    property string bookId: bookPath === "" ? "" : Reader2Bridge.bookKey(bookPath)
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
        onGlueUpChanged: if (glueUp && shell.bookPath !== "") shell.openAtResume(shell.bookPath)
        onPaperEvent: (name, p) => {
            console.log("[shell]", name, JSON.stringify(p).slice(0, 160))
            // RESUME SEAM save: persist the new position on every 'relocated'. Read the
            // key + prev entry AT SAVE TIME (shell.bookId, not a stale capture) so a
            // relocated can only ever write the CURRENT book. The .pragma logic can't
            // touch Date, so we stamp updatedAt here (QML) before handing it the payload.
            if (name === "relocated" && shell.bookPath !== "") {
                var id = shell.bookId
                var prev = Reader2Bridge.progressGet(id)
                p.updatedAt = Date.now()
                Reader2Bridge.progressSave(id, L.progressRecord(prev, p, shell.bookPath))
            }
        }
    }

    // Resolve the saved resume position and open the book there. Read by the derived
    // key first; if that's empty, fall back to the RAW PATH key — the old reader wrote
    // some legacy entries under the literal path (get(book.id) then get(book.path)), so
    // mirroring that fallback lets those still resume. "" cfi = open at the start.
    function openAtResume(path) {
        var entry = Reader2Bridge.progressGet(shell.bookId)
        if (!entry || Object.keys(entry).length === 0)
            entry = Reader2Bridge.progressGet(path)       // raw-path fallback (old-reader parity)
        paper.open(path, L.resumeCfiOf(entry))
    }

    function openBook(path) { bookPath = path; if (paper.glueUp) shell.openAtResume(path) }
}
