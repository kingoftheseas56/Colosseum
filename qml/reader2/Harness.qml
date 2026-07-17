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

    // Chrome fonts. In the real app Main.qml loads these; the standalone harness must
    // load them itself so the TopBar (Fraunces title + Inter) renders as designed.
    // STATICS on purpose: a variable TTF registers as "<Name> Variable", so asking for
    // plain "Inter"/"Fraunces" would silently fall back to Tahoma (probe-proven).
    FontLoader { source: "../../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../../assets/fonts/Fraunces-Italic.ttf" }
    FontLoader { source: "../../assets/fonts/Inter-Regular.otf" }
    FontLoader { source: "../../assets/fonts/Inter-Medium.otf" }
    FontLoader { source: "../../assets/fonts/Inter-SemiBold.otf" }
    FontLoader { source: "../../assets/fonts/Inter-Bold.otf" }

    HarnessShelf {
        id: shelf
        anchors.fill: parent
        visible: true
        onBookChosen: (path) => {
            shell.openBook(path)
            shelf.visible = false
            shell.visible = true
            // Focus the shell scope; keys now live IN-PAGE, so this no longer carries
            // page-turns. The web view takes real key focus when the book becomes 'ready'
            // (ReaderShell → paper.focusPaper()), which lands after this and doesn't fight it.
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
