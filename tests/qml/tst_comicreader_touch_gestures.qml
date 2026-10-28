// Comic reader touch gestures (phone / tablet) — against the REAL production ComicReaderInput.qml,
// with real pointer events through Qt's delivery (not calls to the pure functions): a swipe turns
// a paged page by direction, a finger drags and flings the strip, a centre double-tap and a pinch
// zoom a paged layout. Also pins the other half of the contract: with touchMode off (desktop) a
// mouse drag still turns nothing and a centre double-click is still fullscreen.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/comicreader" as ComicReader

TestCase {
    id: testCase
    name: "ComicReaderTouchGestures"
    when: windowShown

    Window {
        id: testWindow
        width: 360; height: 640; visible: true
        ComicReader.ComicReaderInput {
            id: input
            anchors.fill: parent
            touchMode: true
            mode: "double_page"
        }
    }

    SignalSpy { id: nextSpy; target: input; signalName: "next" }
    SignalSpy { id: previousSpy; target: input; signalName: "previous" }
    SignalSpy { id: dragSpy; target: input; signalName: "dragScroll" }
    SignalSpy { id: flingSpy; target: input; signalName: "flingScroll" }
    SignalSpy { id: zoomSpy; target: input; signalName: "zoomTo" }
    SignalSpy { id: panSpy; target: input; signalName: "panBy" }
    SignalSpy { id: chromeSpy; target: input; signalName: "toggleChrome" }
    SignalSpy { id: fullscreenSpy; target: input; signalName: "toggleFullscreen" }

    function init() {
        input.touchMode = true
        input.mode = "double_page"
        input.rtl = false
        input.zoomPercent = 100
        input.stripFlinging = false
        var spies = [nextSpy, previousSpy, dragSpy, flingSpy, zoomSpy, panSpy, chromeSpy, fullscreenSpy]
        for (var i = 0; i < spies.length; i++) spies[i].clear()
    }

    // A finger drag in steps, so the handler sees real moves between press and release.
    function drag(x0, y0, x1, y1, steps, stepDelayMs) {
        mousePress(input, x0, y0)
        for (var i = 1; i <= steps; i++) {
            mouseMove(input, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, stepDelayMs || -1)
        }
        mouseRelease(input, x1, y1)
    }

    function test_swipe_left_brings_the_right_hand_page() {
        drag(260, 320, 100, 325, 8)
        compare(nextSpy.count, 1, "comic order: a leftward swipe is next")
        compare(previousSpy.count, 0)
    }

    function test_swipe_right_goes_back_in_comic_order() {
        drag(100, 320, 260, 315, 8)
        compare(previousSpy.count, 1)
        compare(nextSpy.count, 0)
    }

    function test_swipe_follows_reading_order_in_manga() {
        input.rtl = true
        drag(100, 320, 260, 315, 8)
        compare(nextSpy.count, 1, "manga order: the next page lies to the left, so a rightward swipe brings it in")
        drag(260, 320, 100, 325, 8)
        compare(previousSpy.count, 1)
    }

    function test_a_vertical_drag_on_a_paged_page_is_not_a_swipe() {
        drag(180, 200, 190, 420, 8)
        compare(nextSpy.count + previousSpy.count, 0)
        verify(panSpy.count > 0, "it pans a page taller than the screen (the surface clamps one that fits)")
    }

    function test_a_short_wobble_is_still_a_tap() {
        mousePress(input, 330, 320)
        mouseMove(input, 333, 323)             // 6px of drift: past the mouse threshold, inside a finger's
        mouseRelease(input, 333, 323)
        compare(nextSpy.count, 1, "the right click zone still turns the page")
    }

    function test_magnified_drag_pans_and_never_turns() {
        input.zoomPercent = 200
        drag(260, 320, 100, 325, 8)
        verify(panSpy.count > 0)
        compare(nextSpy.count + previousSpy.count, 0)
    }

    function test_strip_follows_the_finger_then_flings() {
        input.mode = "long_strip"
        drag(180, 500, 180, 200, 10, 8)          // 8ms a step: a real finger's cadence, so it has a velocity
        verify(dragSpy.count > 0)
        var total = 0
        for (var i = 0; i < dragSpy.count; i++) total += dragSpy.signalArguments[i][0]
        verify(total > 250 && total <= 300, "the strip moved forward with the finger, less the tap threshold: " + total)
        compare(flingSpy.count, 1, "a moving release flings")
        verify(flingSpy.signalArguments[0][0] < 0, "finger moving up the screen = negative velocity")
        compare(chromeSpy.count, 0, "a drag is not a tap")
    }

    function test_strip_finger_that_rests_does_not_fling() {
        input.mode = "long_strip"
        mousePress(input, 180, 500)
        for (var i = 1; i <= 10; i++) mouseMove(input, 180, 500 - 20 * i)
        wait(180)
        mouseRelease(input, 180, 300)
        compare(flingSpy.count, 0)
    }

    function test_centre_double_tap_zooms_in_and_back() {
        mouseDoubleClickSequence(input, 180, 320)
        compare(zoomSpy.count, 1)
        compare(zoomSpy.signalArguments[0][0], 200)
        compare(fullscreenSpy.count, 0, "a phone has no fullscreen to toggle")
        input.zoomPercent = 200
        mouseDoubleClickSequence(input, 180, 320)
        compare(zoomSpy.signalArguments[1][0], 100)
    }

    function test_pinch_zooms_a_paged_page() {
        var touch = touchEvent(input)
        touch.press(0, input, 150, 320).commit()
        touch.stationary(0).press(1, input, 210, 320).commit()
        for (var i = 1; i <= 8; i++) {
            touch.move(0, input, 150 - 8 * i, 320).move(1, input, 210 + 8 * i, 320).commit()
            wait(10)
        }
        verify(zoomSpy.count > 0, "the pinch reached the reader")
        verify(zoomSpy.signalArguments[zoomSpy.count - 1][0] > 120,
               "spreading fingers zoom in: " + zoomSpy.signalArguments[zoomSpy.count - 1][0])
        touch.release(0, input, 86, 320).release(1, input, 274, 320).commit()
        compare(nextSpy.count + previousSpy.count + chromeSpy.count, 0, "a pinch is neither a tap nor a swipe")
    }

    function test_desktop_mouse_drag_still_turns_nothing() {
        input.touchMode = false
        drag(260, 320, 100, 325, 8)
        compare(nextSpy.count + previousSpy.count, 0)
        input.mode = "long_strip"
        drag(180, 500, 180, 200, 10)
        compare(dragSpy.count + flingSpy.count, 0)
    }

    function test_desktop_centre_double_click_is_still_fullscreen() {
        input.touchMode = false
        mouseDoubleClickSequence(input, 180, 320)
        compare(fullscreenSpy.count, 1)
        compare(zoomSpy.count, 0)
    }
}
