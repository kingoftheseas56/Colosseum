import QtQuick 2.15
import "GuideHostLogic.js" as Host

// Living Guide Task 5 — the reusable immersive overlay. A reader or the Theatre player embeds ONE of
// these; F1 (or a quiet controls entry) calls open(). It fills the host, sits above chrome on an opaque
// backdrop, traps focus, embeds GuidePage, and closes on Escape — via GuidePage's OWN Escape, never a
// second competing shortcut (two Escape shortcuts on one window are an ambiguous overload that fires
// neither — the Task 4 lesson). The host stands its own Escape/back down with `enabled: !overlay.active`.
// The overlay NEVER touches the host's playback or state: it exposes `active`, opened()/closed(), and the
// pure capturePlayback/shouldResume so the host decides a Guide-owned pause/resume. Host integration is a
// later slice; this is only the contract.
FocusScope {
    id: overlay
    objectName: "guideOverlay"
    anchors.fill: parent
    visible: active
    z: 10000
    focus: active

    property var catalog: undefined       // optional injected catalog (host / tests); undefined → GuidePage default
    property bool _active: false          // private lifecycle state — ONLY open()/close() mutate it
    readonly property bool active: _active   // frozen public name, READ-ONLY: a direct external write cannot
                                             // bypass close() (which is the only route that restores focus + emits closed())
    property string lessonId: ""
    property string originLabel: ""
    property var _returnFocusItem: null
    property bool _openInFlight: false     // true between an opened() and its matching closed()

    signal opened()
    signal closed()

    // Pure host-state helpers, delegated to GuideHostLogic — the host wires pause on opened() and, on
    // closed(), resumes only if shouldResume(snapshot, mediaStillPresent). The overlay never pauses anything.
    function capturePlayback(wasPlaying) { return Host.capturePlayback(wasPlaying) }
    function shouldResume(snapshot, mediaStillPresent) { return Host.shouldResume(snapshot, mediaStillPresent) }

    function open(lesson, origin) {
        lessonId = lesson || ""
        originLabel = origin || ""
        if (active) {                      // already open — re-point the lesson only (one open still in flight)
            _applyTarget()
            return
        }
        var w = overlay.Window.window
        _returnFocusItem = w ? w.activeFocusItem : null
        _active = true                     // synchronous Loader → GuidePage loads → onLoaded applies the target
        _openInFlight = true
        overlay.forceActiveFocus()
        opened()
    }

    function close() {
        if (!active)
            return
        _active = false                    // Loader deactivates GuidePage; the HOST beneath is untouched
        if (_returnFocusItem) { _returnFocusItem.forceActiveFocus(); _returnFocusItem = null }
        if (_openInFlight) { _openInFlight = false; closed() }   // exactly one close per open
    }

    function _applyTarget() {
        if (!guideLoader.item)
            return
        guideLoader.item.originLabel = overlay.originLabel
        if (overlay.lessonId)
            guideLoader.item.openLesson(overlay.lessonId)
    }

    // Opaque backdrop + input trap: the host beneath neither shows through nor receives clicks/wheel.
    Rectangle { anchors.fill: parent; color: "#0b0b0b" }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Loader {
        id: guideLoader
        anchors.fill: parent
        active: overlay.active
        source: "GuidePage.qml"
        onLoaded: {
            if (overlay.catalog !== undefined)
                item.catalog = overlay.catalog
            item.closeRequested.connect(overlay.close)
            item.returnRequested.connect(overlay.close)
            overlay._applyTarget()
            item.forceActiveFocus()
        }
    }
}
