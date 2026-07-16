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

    // Register the native bridge under the name "bridge" (what bridge_boot.js reads
    // as channel.objects.bridge). registerObject-by-name is required for a C++
    // context object — the QML-attached-id form can't carry one.
    Component.onCompleted: channel.registerObject("bridge", Reader2Bridge)

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
        activeFocusOnPress: false          // ROOT FIX: the paper never owns a key —
                                           // keeps focus on the ReaderShell FocusScope.
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        settings.focusOnNavigationEnabled: false
        webChannel: WebChannel { id: channel }
        url: Qt.resolvedUrl("../../resources/reader2/paper.html")
        onJavaScriptConsoleMessage: (lvl, msg, line, src) => console.log("[paper]", msg)
    }
}
