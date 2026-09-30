// AndroidPaper.qml - Android-side renderer contract for ReaderShell.
//
// This file deliberately imports no QtWebEngine/QtWebChannel. ReaderShell may keep
// its existing state/chrome logic while an Android renderer object supplies only the
// visual publication operations below. The renderer is injected as `backend` so this
// scaffold does not choose a Java/Kotlin/web renderer before the Android host is ready.
import QtQuick

Item {
    id: paper

    signal paperEvent(string name, var payload)
    property bool readerDebug: false

    // Expected production default: a platform context property registered only in an
    // Android build. Tests and experiments can inject any object with the same surface.
    property var backend: (typeof AndroidEbookRenderer !== "undefined")
                          ? AndroidEbookRenderer : null

    // ReaderShell waits for glueUp before issuing openAtResume(). The adapter itself is
    // immediately callable; an absent backend therefore produces an explicit open error
    // instead of leaving the reader forever waiting on a readiness bit that cannot change.
    readonly property bool glueUp: true

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
