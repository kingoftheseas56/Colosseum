import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "ScrollGlideWheel"
    when: windowShown

    Window {
        id: testWindow
        width: 480
        height: 360
        visible: true

        Flickable {
            id: flick
            objectName: "scrollGlideWheelFlick"
            anchors.fill: parent
            contentWidth: width
            contentHeight: 2200
            pixelAligned: false
            boundsBehavior: Flickable.StopAtBounds
            Column {
                width: flick.width
                Repeater {
                    model: 22
                    Rectangle { width: flick.width; height: 100 }
                }
            }
        }
        Colosseum.ScrollGlide { id: glide; flick: flick }
    }

    // A nested scroller at its end hands the wheel to the outer page (Library walls in a world).
    Window {
        id: nestWindow
        width: 480
        height: 360
        visible: true

        Flickable {
            id: outer
            anchors.fill: parent
            contentWidth: width
            contentHeight: 2000
            pixelAligned: false
            boundsBehavior: Flickable.StopAtBounds
            Flickable {
                id: inner
                y: 0
                width: outer.width
                height: 300
                contentWidth: width
                contentHeight: 600
                pixelAligned: false
                boundsBehavior: Flickable.StopAtBounds
            }
        }
        Colosseum.ScrollGlide { id: outerGlide; flick: outer }
        Colosseum.ScrollGlide { id: innerGlide; flick: inner }
    }

    function test_mouse_wheel_reaches_shared_controller() {
        flick.contentY = 300
        glide.cancelGlide()
        mouseWheel(flick, flick.width / 2, flick.height / 2,
                   0, -120, Qt.NoButton, Qt.NoModifier, 0)
        // ScrollGlide is a zero-size sibling of the Flickable, as in production (WorldPage).
        // Qt's own Flickable wheel also moves contentY, so "it moved" proves nothing: the
        // settled distance must be ScrollGlide's notch (120 * speed 1.4 = 168 px), exactly.
        tryVerify(function() {
            return glide._pendingPx === 0 && flick.contentY > 300
        }, 2000, "the wheel glide must settle")
        compare(flick.contentY, 468, "one notch must travel ScrollGlide's 168 px, not Qt's default wheel")
    }

    function test_nested_scroller_at_end_hands_wheel_to_outer() {
        outer.contentY = 0
        inner.contentY = 300          // inner is at its bottom
        outerGlide.cancelGlide()
        innerGlide.cancelGlide()
        mouseWheel(inner, inner.width / 2, inner.height / 2,
                   0, -120, Qt.NoButton, Qt.NoModifier, 0)
        tryVerify(function() {
            return outerGlide._pendingPx === 0 && outer.contentY > 0
        }, 2000, "the outer page must take the wheel")
        compare(outer.contentY, 168, "outer page travels one notch")
        compare(inner.contentY, 300, "inner stays at its bottom")

        mouseWheel(inner, inner.width / 2, inner.height - 20,   // inside the window: outer is scrolled 168
                   0, 120, Qt.NoButton, Qt.NoModifier, 0)
        tryVerify(function() { return innerGlide._pendingPx === 0 && inner.contentY < 300 }, 2000,
                  "wheel up scrolls the inner scroller first")
        compare(inner.contentY, 132)
        compare(outer.contentY, 168)
    }
}
