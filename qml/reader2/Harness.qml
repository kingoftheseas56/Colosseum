// Harness.qml — TASK 5 root window. Boots into the book shelf; clicking a book
// hides the shelf and reveals the ReaderShell (the paper). Esc in the shell
// returns to the shelf. No Colosseum chrome — this is the standalone "first
// pixels" harness that proves the paper loads and reports position.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../" as App

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
    // Literata (Task 10 appearance) — the shipped reading serif + default typeface. STATIC
    // instances register as the plain family "Literata" (a variable TTF would register as
    // "Literata Variable" and silently fall back to Tahoma), so the panel's Literata card +
    // any serif chrome resolve correctly. The BOOK text gets Literata separately, via an
    // @font-face the glue injects into the paper page (paper_glue.js FONT_FACE_CSS).
    FontLoader { source: "../../assets/fonts/Literata-Regular.ttf" }
    FontLoader { source: "../../assets/fonts/Literata-Italic.ttf" }

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

    // The ONE shared audiobook engine (Task 13). In the real app this lives at Main.qml's
    // window root; here it lives at the harness root the same way, and the reader is a REMOTE
    // that drives it (Hemanth: one engine, many faces). Uses `Audiobooks`/`Progress` context
    // props when present; the harness registers Audiobooks + AudioPairing, Progress is absent
    // (AudiobookSession guards `typeof Progress` so resume just no-ops).
    App.AudiobookSession { id: audiobookSession }

    ReaderShell {
        id: shell
        anchors.fill: parent
        visible: false
        readerDebug: true                     // standalone bench keeps the [shell]/[paper] event trace (Part C5)
        audioSession: audiobookSession        // inject the shared engine → the Audio tab drives it
        onClosed: {
            shell.visible = false
            shelf.visible = true
        }
    }
}
