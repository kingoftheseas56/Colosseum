// ComicReaderInput — the reader's semantic input map (Task 11). It fills the reading area BELOW the
// HUD and turns raw pointer/keyboard events into SEMANTIC actions only (never raw scroll deltas or
// page indices leaking out). The shell wires these actions to its navigation + the surfaces; the
// HUD sits above and consumes pill clicks before they reach here.
//
// The decision logic is pure functions (keyAction / zoneForX / navByZone / releaseAt / wheelAction)
// that BOTH the live handlers AND the offscreen harness call — so the tested logic IS the shipped
// logic. Ground-truthed against the lineage TankobanQTGroundWork comic_reader.py keyPressEvent +
// eventFilter (~2377-2600): click zones turn pages BY DIRECTION, center toggles chrome / (double
// click) fullscreen with the 220ms single-vs-double disambiguation, and a >4px press-drag while
// magnified pans and cancels the click so panning never turns a page.

import QtQuick

Item {
    id: input

    // ---- reading-state mirrors (bound from the shell) ----
    property string mode: "long_strip"      // "long_strip" | "double_page"
    property bool   rtl: false
    property int    zoomPercent: 100
    property bool   modalOpen: false         // an overlay is up (Task 12)
    property bool   chromeVisible: true

    // ---- tunables (lineage house numbers) ----
    property int  singleClickMs: 220         // SINGLE_CLICK_DELAY_MS
    property real panKeyStep: 128            // arrow pan step (double, zoomed)
    property real dragCancelPx: 4            // drift beyond this cancels a click
    property int  topRevealPx: 48            // top reveal zone (back pill)
    property int  bottomRevealPx: 72         // BOTTOM_HUD_HEIGHT reveal zone

    // ================= semantic actions (the shell / Task 12 wire these) =================
    signal next()
    signal previous()
    signal scrollBy(real screens)            // strip page-scroll, in viewport-heights (+ forward)
    signal zoomBy(int delta)                 // double-page zoom, +/- percent
    signal panBy(real dx, real dy)           // double-page pan (magnified)
    signal toggleChrome()
    signal toggleFullscreen()
    signal openSettings()
    signal openNavigator()
    signal openThumbnails()
    signal toggleBookmark()
    signal goToPage()
    signal closeTop()                        // close the top overlay (Task 12)
    signal cycleMode()                       // M — toggle the two modes
    signal toggleDirection()                 // I
    signal nudgeCoupling()                   // P — core.nudgeCoupling (via shell)
    signal openShortcuts()                   // K
    signal toggleLoupe()                     // L
    signal firstPage()                       // Home
    signal lastPage()                        // End
    signal prevEntry()                       // Alt+Left (crossing)
    signal nextEntry()                       // Alt+Right (crossing)
    signal back()                            // Esc fallback -> backRequested
    signal openContextMenu(real x, real y)   // right-click -> settings + spread override (Task 12)
    signal revealRequested()                 // pointer entered a top/bottom reveal zone
    signal activity()                        // any pointer move (keeps chrome alive)

    // ================= keyboard map (pure) =================
    // returns the semantic token it fired for `key`+`mods` (or "" if unhandled), and emits it.
    function keyAction(key, mods) {
        var ctrl = (mods & Qt.ControlModifier) !== 0
        var alt  = (mods & Qt.AltModifier) !== 0
        var dbl  = (mode === "double_page")
        var zoomed = dbl && zoomPercent > 100

        // Escape order: close top overlay -> hide chrome -> back
        if (key === Qt.Key_Escape) {
            if (modalOpen) { closeTop(); return "closeTop" }
            if (chromeVisible) { toggleChrome(); return "toggleChrome" }
            back(); return "back"
        }
        if (ctrl && key === Qt.Key_G) { goToPage(); return "goToPage" }
        if (ctrl && (key === Qt.Key_Plus || key === Qt.Key_Equal)) { zoomBy(20); return "zoomBy" }
        if (ctrl && (key === Qt.Key_Minus || key === Qt.Key_Underscore)) { zoomBy(-20); return "zoomBy" }
        if (alt && key === Qt.Key_Left)  { prevEntry(); return "prevEntry" }
        if (alt && key === Qt.Key_Right) { nextEntry(); return "nextEntry" }

        switch (key) {
        case Qt.Key_O: openNavigator();    return "openNavigator"
        case Qt.Key_T: openThumbnails();   return "openThumbnails"
        case Qt.Key_F: toggleFullscreen(); return "toggleFullscreen"
        case Qt.Key_M: cycleMode();        return "cycleMode"
        case Qt.Key_I: toggleDirection();  return "toggleDirection"
        case Qt.Key_P: nudgeCoupling();    return "nudgeCoupling"
        case Qt.Key_K: openShortcuts();    return "openShortcuts"
        case Qt.Key_B: toggleBookmark();   return "toggleBookmark"
        case Qt.Key_L: toggleLoupe();      return "toggleLoupe"
        case Qt.Key_H: toggleChrome();     return "toggleChrome"
        case Qt.Key_Home: firstPage();     return "firstPage"
        case Qt.Key_End:  lastPage();      return "lastPage"
        }

        // Space / PageDown / PageUp — double navigates a unit, strip scrolls a screen
        if (key === Qt.Key_Space || key === Qt.Key_PageDown) {
            if (dbl) { next(); return "next" }
            scrollBy(0.8); return "scrollBy"
        }
        if (key === Qt.Key_PageUp) {
            if (dbl) { previous(); return "previous" }
            scrollBy(-0.8); return "scrollBy"
        }

        // arrows — pan when magnified, navigate otherwise
        if (key === Qt.Key_Left || key === Qt.Key_Right || key === Qt.Key_Up || key === Qt.Key_Down) {
            if (zoomed) {
                if (key === Qt.Key_Left)       panBy(-panKeyStep, 0)
                else if (key === Qt.Key_Right) panBy(panKeyStep, 0)
                else if (key === Qt.Key_Up)    panBy(0, -panKeyStep)
                else                           panBy(0, panKeyStep)
                return "panBy"
            }
            if (key === Qt.Key_Left)  { if (rtl) { next(); return "next" } else { previous(); return "previous" } }
            if (key === Qt.Key_Right) { if (rtl) { previous(); return "previous" } else { next(); return "next" } }
            if (key === Qt.Key_Up)    { previous(); return "previous" }
            if (key === Qt.Key_Down)  { next(); return "next" }
        }
        return ""
    }

    // ================= click zones (pure) =================
    function zoneForX(x, w) {
        if (w <= 0) return "mid"
        var third = w / 3.0
        if (x < third) return "left"
        if (x >= w - third) return "right"
        return "mid"
    }
    // RTL: left third = NEXT; LTR: left third = PREVIOUS (lineage double_page_next_on_left).
    function navByZone(zone) {
        if (zone === "left")  { if (rtl) { next(); return "next" } else { previous(); return "previous" } }
        if (zone === "right") { if (rtl) { previous(); return "previous" } else { next(); return "next" } }
        return ""
    }

    // ================= press / drag / release lifecycle =================
    property real _pressX: 0
    property real _pressY: 0
    property real _lastX: 0
    property real _lastY: 0
    property bool _pressed: false
    property bool _clickCancelled: false

    function pressAt(x, y) {
        _pressed = true
        _pressX = x; _pressY = y
        _lastX = x; _lastY = y
        _clickCancelled = false
    }
    // returns true if it panned (magnified drag)
    function moveTo(x, y) {
        if (!_pressed) return false
        var drift = Math.abs(x - _pressX) + Math.abs(y - _pressY)
        if (drift > dragCancelPx) _clickCancelled = true
        if (mode === "double_page" && zoomPercent > 100) {
            panBy(_lastX - x, _lastY - y)   // drag content with the pointer
            _lastX = x; _lastY = y
            return true
        }
        return false
    }
    function releaseAt(x, w) {
        // A double-click's TRAILING release arrives with NO matching press: Qt delivers a double
        // click as Press, Release, DblClick, Release — the 2nd press is a DblClick, so pressAt never
        // ran for it and `_pressed` is already false here. Discard that stray release. Without this
        // gate it would re-arm the single-click timer AFTER doubleClick() stopped it (chrome toggles
        // ~220ms after every fullscreen double-click), and would run navByZone on BOTH releases of a
        // side-zone double-click (two page turns). (moveTo is likewise gated on _pressed.)
        if (!_pressed) return ""
        var cancelled = _clickCancelled
        _pressed = false
        _clickCancelled = false
        if (cancelled) return ""            // a drag never turns a page / toggles
        if (mode === "double_page") {
            var zone = zoneForX(x, w)
            if (zone === "left" || zone === "right") return navByZone(zone)
        }
        singleClickTimer.restart()          // center (double) or any click (strip): chrome toggle after 220ms
        return "pendingSingle"
    }
    // A double click toggles fullscreen ONLY in the center third — the side thirds are page
    // navigation, so a side double-click must navigate (single turn) and NEVER fullscreen. The
    // single-click timer is always stopped so a pending center single-click can't fire alongside it.
    function doubleClick(x, w) {
        singleClickTimer.stop()
        if (zoneForX(x, w) === "mid") toggleFullscreen()
    }
    function _commitSingleClick() { toggleChrome() }

    readonly property alias singleClickRunning: singleClickTimer.running
    Timer { id: singleClickTimer; interval: input.singleClickMs; repeat: false; onTriggered: input._commitSingleClick() }

    // ================= wheel (pure) =================
    // Ctrl+wheel zooms (double); an unmodified wheel while magnified pans; strip wheels are NOT
    // consumed here (they fall through to the strip surface's smooth accumulator).
    function wheelAction(angleY, angleX, ctrl) {
        if (mode !== "double_page") return ""
        if (ctrl) { zoomBy(angleY >= 0 ? 20 : -20); return "zoomBy" }
        if (zoomPercent > 100) {
            var dyPx = angleY * (100.0 / 120.0)
            var dxPx = angleX * (100.0 / 120.0)
            if (Math.abs(dxPx) > Math.abs(dyPx)) panBy(-dxPx, 0)
            else panBy(0, -dyPx)
            return "panBy"
        }
        return ""
    }

    // ================= live handlers (delegate to the pure functions) =================
    function _checkRevealZone(y) {
        if (y <= topRevealPx || y >= height - bottomRevealPx) revealRequested()
        else activity()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onPressed: function (m) {
            if (m.button === Qt.RightButton) { input.openContextMenu(m.x, m.y); return }
            input.pressAt(m.x, m.y)
        }
        onPositionChanged: function (m) {
            if (input._pressed) input.moveTo(m.x, m.y)
            else input._checkRevealZone(m.y)
        }
        onReleased: function (m) {
            if (m.button === Qt.RightButton) return
            input.releaseAt(m.x, input.width)
        }
        onDoubleClicked: function (m) {
            if (m.button === Qt.LeftButton) input.doubleClick(m.x, input.width)
        }
    }

    // Ctrl+wheel zoom / magnified pan — double mode only; strip wheels fall through to the surface.
    WheelHandler {
        enabled: input.mode === "double_page"
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) {
            var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            input.wheelAction(event.angleDelta.y, event.angleDelta.x, ctrl)
        }
    }
}
