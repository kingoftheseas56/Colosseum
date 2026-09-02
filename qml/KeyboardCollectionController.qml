// KeyboardCollectionController — shared spatial keyboard law for Colosseum media collections.
// The owning view keeps focus; this controller moves currentIndex, keeps it visible, and emits
// semantic activation/context/reorder requests. Boundary arrows remain unaccepted so a parent
// FocusScope can move focus to the neighbouring region.
import QtQuick

Item {
    id: nav

    required property var view
    property string orientation: "vertical" // vertical | horizontal | grid
    property int columns: 1
    property int count: view && view.count !== undefined ? view.count : 0
    property int currentIndex: view && view.currentIndex !== undefined ? view.currentIndex : -1
    property int pageStep: 0
    // Optional seam for Flickable/Row collections without positionViewAtIndex().
    property var positionIndexFn: null
    property bool spaceActivates: true
    property bool contextEnabled: false
    property bool reorderEnabled: false
    property bool keyboardRecentlyMoved: false
    property int keyboardQuietMs: 220
    property int positionMode: GridView.Contain

    signal activated(int index)
    signal contextRequested(int index)
    signal reorderRequested(int fromIndex, int toIndex)

    visible: false

    Timer {
        id: keyboardQuietTimer
        interval: nav.keyboardQuietMs
        onTriggered: nav.keyboardRecentlyMoved = false
    }

    function markKeyboardMovement() {
        nav.keyboardRecentlyMoved = true
        keyboardQuietTimer.restart()
    }

    function indexNow() {
        if (nav.view && nav.view.currentIndex !== undefined)
            return nav.view.currentIndex
        return nav.currentIndex
    }
    function moveTo(index, focusReason) {
        const n = Math.max(0, nav.count)
        if (!nav.view || n <= 0)
            return false
        const next = Math.max(0, Math.min(n - 1, index))
        if (next === nav.indexNow())
            return false
        nav.view.currentIndex = next
        nav.currentIndex = next
        if (nav.positionIndexFn)
            nav.positionIndexFn(next)
        else if (nav.view.positionViewAtIndex)
            nav.view.positionViewAtIndex(next, nav.positionMode)
        if (nav.view.forceActiveFocus)
            nav.view.forceActiveFocus(focusReason)
        nav.markKeyboardMovement()
        return true
    }

    function defaultPageStep() {
        if (!nav.view)
            return 1
        if (nav.orientation === "grid") {
            const rowHeight = Number(nav.view.cellHeight) || 1
            const visibleRows = Math.max(1, Math.floor(Number(nav.view.height) / rowHeight))
            return visibleRows * Math.max(1, nav.columns)
        }
        if (nav.orientation === "horizontal") {
            const itemWidth = nav.view.currentItem ? Number(nav.view.currentItem.width) : 0
            return Math.max(1, itemWidth > 0 ? Math.floor(Number(nav.view.width) / itemWidth) : 5)
        }
        const itemHeight = nav.view.currentItem ? Number(nav.view.currentItem.height) : 0
        return Math.max(1, itemHeight > 0 ? Math.floor(Number(nav.view.height) / itemHeight) : 8)
    }

    function stepForKey(key) {
        const cols = Math.max(1, nav.columns)
        if (nav.orientation === "horizontal") {
            if (key === Qt.Key_Left) return -1
            if (key === Qt.Key_Right) return 1
            return 0
        }
        if (nav.orientation === "grid") {
            if (key === Qt.Key_Left) return -1
            if (key === Qt.Key_Right) return 1
            if (key === Qt.Key_Up) return -cols
            if (key === Qt.Key_Down) return cols
            return 0
        }
        if (key === Qt.Key_Up) return -1
        if (key === Qt.Key_Down) return 1
        return 0
    }
    function stepAllowed(index, step) {
        if (step === 0)
            return false
        const next = index + step
        if (next < 0 || next >= nav.count)
            return false
        if (nav.orientation === "grid" && Math.abs(step) === 1) {
            const cols = Math.max(1, nav.columns)
            return Math.floor(index / cols) === Math.floor(next / cols)
        }
        return true
    }

    function reorderDeltaFor(event) {
        if (!nav.reorderEnabled
                || !(event.modifiers & Qt.ControlModifier)
                || !(event.modifiers & Qt.ShiftModifier))
            return 0
        return nav.stepForKey(event.key)
    }

    function contextKey(event) {
        return event.key === Qt.Key_Menu
            || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
    }

    function activateKey(event) {
        return event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            || (nav.spaceActivates && event.key === Qt.Key_Space)
    }
    function handle(event) {
        if (!event || nav.count <= 0)
            return false
        const index = Math.max(0, nav.indexNow())
        const reorderDelta = nav.reorderDeltaFor(event)
        if (reorderDelta !== 0 && nav.stepAllowed(index, reorderDelta)) {
            nav.reorderRequested(index, index + reorderDelta)
            event.accepted = true
            nav.markKeyboardMovement()
            return true
        }
        if (nav.activateKey(event)) {
            nav.activated(index)
            event.accepted = true
            return true
        }
        if (nav.contextEnabled && nav.contextKey(event)) {
            nav.contextRequested(index)
            event.accepted = true
            return true
        }

        const step = nav.stepForKey(event.key)
        if (step !== 0 && nav.stepAllowed(index, step)) {
            const reason = step < 0 ? Qt.BacktabFocusReason : Qt.TabFocusReason
            if (nav.moveTo(index + step, reason)) {
                event.accepted = true
                return true
            }
        }
        if (event.key === Qt.Key_Home) {
            if (nav.moveTo(0, Qt.BacktabFocusReason)) {
                event.accepted = true
                return true
            }
        } else if (event.key === Qt.Key_End) {
            if (nav.moveTo(nav.count - 1, Qt.TabFocusReason)) {
                event.accepted = true
                return true
            }
        } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
            const page = nav.pageStep > 0 ? nav.pageStep : nav.defaultPageStep()
            const delta = event.key === Qt.Key_PageUp ? -page : page
            const reason = delta < 0 ? Qt.BacktabFocusReason : Qt.TabFocusReason
            if (nav.moveTo(index + delta, reason)) {
                event.accepted = true
                return true
            }
        }
        return false
    }
}
