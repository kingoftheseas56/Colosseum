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

    function test_mouse_wheel_reaches_shared_controller() {
        flick.contentY = 300
        glide.cancelGlide()
        mouseWheel(flick, flick.width / 2, flick.height / 2,
                   0, -120, Qt.NoButton, Qt.NoModifier, 0)
        tryVerify(function() {
            return glide._pendingPx !== 0 || flick.contentY > 300
        }, 500, "a real wheel event must enter ScrollGlide")
        verify(glide._pendingPx !== 0 || flick.contentY > 300,
               "shared controller must receive the wheel event")
    }
}
