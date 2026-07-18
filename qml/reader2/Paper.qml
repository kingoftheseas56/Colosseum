// Paper.qml — the web "paper" wrapper: a WebEngineView hosting the vendored Anx
// foliate fork + our thin glue (resources/reader2/paper.html + paper_glue.js).
// This is the WHOLE command/event surface between native QML and the paper:
//   commands DOWN  → window.paper.*  (runJavaScript)
//   events   UP    → Reader2Bridge.paperEventReceived → paperEvent(name, payload)
//
// The native Reader2Bridge is registered on this view's QWebChannel as "bridge";
// paper.html's <head> loads qwebchannel.js + bridge_boot.js (classic scripts) to
// build window.bridge from it — the proven in-repo pattern (the old reader wires
// its bridge the same way), so no userScripts injection is needed here.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: paper
    signal paperEvent(string name, var payload)
    property bool glueUp: false
    // Forward the paper page's console messages to the Qt log ONLY when debugging (Part C5):
    // default false so a shipped embedding never spams; the standalone harness flips it on.
    property bool readerDebug: false

    function open(path, cfi) { run("window.paper.open(" + JSON.stringify(path) + "," + JSON.stringify(cfi || "") + ")") }
    function next() { run("window.paper.next()") }
    function prev() { run("window.paper.prev()") }
    function goTo(t) { run("window.paper.goTo(" + JSON.stringify(t) + ")") }
    function setAppearance(a) { run("window.paper.setAppearance(" + JSON.stringify(JSON.stringify(a)) + ")") }
    function search(q) { run("window.paper.search(" + JSON.stringify(q) + ")") }
    function clearSearch() { run("window.paper.clearSearch()") }
    function addHighlight(h) { run("window.paper.addHighlight(" + JSON.stringify(JSON.stringify(h)) + ")") }
    function removeHighlight(id) { run("window.paper.removeHighlight(" + JSON.stringify(id) + ")") }
    function clearSelection() { run("window.paper.clearSelection()") }
    function run(js) { web.runJavaScript(js) }

    // Put active focus on the web view so its in-page keyboard (paper_glue.js) receives
    // keys immediately. Called by ReaderShell when a book becomes 'ready' — the web view
    // OWNS focus/pointer/keyboard (old-reader model), so keys work without a click first.
    function focusPaper() { web.forceActiveFocus() }

    // Register the native PAPER GATE under the name "bridge" (what bridge_boot.js reads
    // as channel.objects.bridge). registerObject-by-name is required for a C++
    // context object — the QML-attached-id form can't carry one.
    // GATE, not the full bridge (least privilege — Codex re-review fix): QWebChannel
    // exposes every invokable of whatever object is registered, so registering
    // Reader2Bridge itself would hand the untrusted paper setAuthorizedBook (self-
    // authorize any path, then filesRead it), all the store writes, and dictLookup.
    // The gate carries exactly filesRead + paperEvent.
    Component.onCompleted: channel.registerObject("bridge", Reader2Bridge.paperGate)

    Connections {
        target: Reader2Bridge
        function onPaperEventReceived(name, json) {
            if (name === "glueLoaded") paper.glueUp = true
            paper.paperEvent(name, JSON.parse(json))
        }
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "#000000"
        focus: true
        // The web view OWNS focus + pointer + keyboard (the old reader's proven model).
        // We deliberately do NOT set activeFocusOnPress:false: that starved the paper of
        // the press/focus that TEXT SELECTION and the glue's double-click listener need
        // (confirmed by a live diagnostic). Keyboard page-turns/Esc are handled IN-PAGE by
        // paper_glue.js and emitted UP as semantic events — so the paper can own focus and
        // keys STILL work. forceActiveFocus() below makes keys live the instant a book opens.
        settings.localContentCanAccessFileUrls: true
        // SANDBOX (hardening): books are UNTRUSTED local content and never need the network —
        // the dictionary runs in C++, and fonts + book bytes are all file://. Deny remote loads
        // so a rigged book can't phone home / beacon. localContentCanAccessFileUrls stays TRUE
        // (needed for @font-face + the file:// book bytes); self-contained EPUBs are unaffected,
        // a book's remote images simply won't load (acceptable — offline reader).
        settings.localContentCanAccessRemoteUrls: false
        webChannel: WebChannel { id: channel }
        url: Qt.resolvedUrl("../../resources/reader2/paper.html")
        onJavaScriptConsoleMessage: (lvl, msg, line, src) => { if (paper.readerDebug) console.log("[paper]", msg) }
        // When paper.html finishes loading, hand it active focus so the in-page keyboard
        // is live without needing a click first (ReaderShell also focuses it on book-ready).
        onLoadingChanged: (info) => { if (info.status === WebEngineView.LoadSucceededStatus) web.forceActiveFocus() }
    }
}
