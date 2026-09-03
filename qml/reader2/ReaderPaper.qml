// ReaderPaper.qml - platform selector for ReaderShell's publication surface.
// Desktop keeps the existing Paper.qml + Qt WebEngine implementation. Android loads
// AndroidPaper.qml, whose backend contract is supplied by the Android host.
import QtQuick

Item {
    id: host

    signal paperEvent(string name, var payload)
    property bool readerDebug: false
    readonly property bool glueUp: loader.item ? !!loader.item.glueUp : false

    function _call(name, args) {
        if (!loader.item || typeof loader.item[name] !== "function") return
        loader.item[name].apply(loader.item, args || [])
    }

    function open(path, cfi, gen) { _call("open", [path, cfi, gen]) }
    function next() { _call("next") }
    function prev() { _call("prev") }
    function goTo(target) { _call("goTo", [target]) }
    function setAppearance(value) { _call("setAppearance", [value]) }
    function search(value) { _call("search", [value]) }
    function clearSearch() { _call("clearSearch") }
    function addHighlight(value) { _call("addHighlight", [value]) }
    function removeHighlight(value) { _call("removeHighlight", [value]) }
    function clearSelection() { _call("clearSelection") }
    function setReadAlongStyle(value) { _call("setReadAlongStyle", [value]) }
    function paintReadAlong(value) { _call("paintReadAlong", [value]) }
    function clearReadAlong() { _call("clearReadAlong") }
    function ensureReadAlongVisible(value) { _call("ensureReadAlongVisible", [value]) }
    function navigateReadAlong(value) { _call("navigateReadAlong", [value]) }
    function focusPaper() { _call("focusPaper") }

    onReaderDebugChanged: {
        if (loader.item && loader.item.readerDebug !== undefined)
            loader.item.readerDebug = readerDebug
    }

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.platform.os === "android" ? "AndroidPaper.qml" : "Paper.qml"
        onLoaded: {
            if (item.readerDebug !== undefined)
                item.readerDebug = host.readerDebug
            if (item.paperEvent)
                item.paperEvent.connect(function(name, payload) {
                    host.paperEvent(name, payload)
                })
        }
    }
}
