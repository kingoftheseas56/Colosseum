// AndroidPaper.qml - Android-side renderer contract for ReaderShell.
//
// ReaderShell keeps its existing state/chrome logic. The native backend supplies
// a foreign Android WebView window and the shared Foliate command/event protocol.
import QtQuick

Item {
    id: paper

    signal paperEvent(string name, var payload)
    property bool readerDebug: false

    // Expected production default: a platform context property registered only in an
    // Android build. Tests and experiments can inject any object with the same surface.
    property var backend: (typeof AndroidEbookRenderer !== "undefined")
                          ? AndroidEbookRenderer : null

    // Production readiness comes from the trusted WebView shell. The missing/fake
    // backend fallback preserves the explicit error and host-test contract.
    readonly property bool glueUp: backend ? (backend.glueUp === undefined ? true : backend.glueUp) : true

    WindowContainer {
        anchors.fill: parent
        window: paper.backend && paper.backend.foreignWindow !== undefined ? paper.backend.foreignWindow : null
    }
    Component.onCompleted: if (backend && typeof backend.create === "function") backend.create()
    Component.onDestruction: if (backend && typeof backend.close === "function") backend.close()

    function _missing(command, gen) {
        if (readerDebug) console.warn("[android-paper] backend missing command:", command)
        if (command === "open")
            paperEvent("error", { gen: gen, message: "Android ebook renderer is unavailable" })
    }

    function open(path, cfi, gen) {
        if (!backend || typeof backend.open !== "function") { _missing("open", gen); return }
        backend.open(path, cfi || "", gen)
    }

    function next() {
        if (!backend || typeof backend.next !== "function") { _missing("next"); return }
        backend.next()
    }
    function prev() {
        if (!backend || typeof backend.prev !== "function") { _missing("prev"); return }
        backend.prev()
    }
    function goTo(target) {
        if (!backend || typeof backend.goTo !== "function") { _missing("goTo"); return }
        backend.goTo(target)
    }
    function setAppearance(appearance) {
        if (!backend || typeof backend.setAppearance !== "function") { _missing("setAppearance"); return }
        backend.setAppearance(appearance)
    }
    function search(query) {
        if (!backend || typeof backend.search !== "function") { _missing("search"); return }
        backend.search(query)
    }
    function clearSearch() {
        if (!backend || typeof backend.clearSearch !== "function") { _missing("clearSearch"); return }
        backend.clearSearch()
    }
    function addHighlight(highlight) {
        if (!backend || typeof backend.addHighlight !== "function") { _missing("addHighlight"); return }
        backend.addHighlight(highlight)
    }
    function removeHighlight(id) {
        if (!backend || typeof backend.removeHighlight !== "function") { _missing("removeHighlight"); return }
        backend.removeHighlight(id)
    }
    function clearSelection() {
        if (!backend || typeof backend.clearSelection !== "function") { _missing("clearSelection"); return }
        backend.clearSelection()
    }
    function setReadAlongStyle(style) {
        if (!backend || typeof backend.setReadAlongStyle !== "function") { _missing("setReadAlongStyle"); return }
        backend.setReadAlongStyle(style)
    }
    function paintReadAlong(cue) {
        if (!backend || typeof backend.paintReadAlong !== "function") { _missing("paintReadAlong"); return }
        backend.paintReadAlong(cue)
    }
    function clearReadAlong() {
        if (!backend || typeof backend.clearReadAlong !== "function") { _missing("clearReadAlong"); return }
        backend.clearReadAlong()
    }
    function ensureReadAlongVisible(location) {
        if (!backend || typeof backend.ensureReadAlongVisible !== "function") { _missing("ensureReadAlongVisible"); return }
        backend.ensureReadAlongVisible(location)
    }
    function navigateReadAlong(location) {
        if (!backend || typeof backend.navigateReadAlong !== "function") { _missing("navigateReadAlong"); return }
        backend.navigateReadAlong(location)
    }
    function focusPaper() {
        if (!backend || typeof backend.focusPaper !== "function") { _missing("focusPaper"); return }
        backend.focusPaper()
    }

    // Native backends emit JSON for the same reason Reader2Bridge does today: the
    // book-scoped event schema stays a plain transport contract instead of leaking a
    // Java/Kotlin/QVariant object model into ReaderShell.
    Connections {
        target: paper.backend
        ignoreUnknownSignals: true

        function onEventRaised(name, json) {
            var payload = ({})
            try {
                payload = (typeof json === "string") ? JSON.parse(json) : (json || ({}))
            } catch (e) {
                paper.paperEvent("error", {
                    message: "Android ebook renderer emitted invalid event JSON"
                })
                return
            }
            paper.paperEvent(name, payload)
        }
    }
}
