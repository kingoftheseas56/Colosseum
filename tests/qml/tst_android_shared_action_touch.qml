// Regression coverage of the retained production scroll owners; no alternate gesture implementation.
import QtQuick
import QtTest
import "../../qml" as App
Item {
    width: 480; height: 420
    Flickable {
        id: vertical; anchors.fill: parent; contentWidth: width; contentHeight: 1400
        boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.VerticalFlick
        App.KeyboardAction { id: action; width: 450; height: 1000 }
    }
    SignalSpy { id: activated; target: action; signalName: "triggered" }
    TestCase {
        name: "AndroidSharedActionTouch"; when: windowShown
        property var activeSequence: null
        function cleanup() {
            if (activeSequence) { activeSequence.release(0,vertical,200,100).commit(); activeSequence=null; wait(20) }
        }
        function init() { vertical.cancelFlick(); vertical.contentY=0; activated.clear(); wait(40) }
        function drag() {
            let seq=touchEvent(vertical); activeSequence=seq; seq.press(0,vertical,200,320).commit(); wait(30)
            for(let y=300;y>=100;y-=20) { seq.move(0,vertical,200,y).commit(); wait(16) }
            let moved=vertical.contentY; seq.release(0,vertical,200,100).commit(); activeSequence=null; wait(20); return moved
        }
        function test_vertical_drag_over_keyboard_action_does_not_activate() {
            verify(drag()>50,"parent must take drag"); compare(activated.count,0); verify(!action.pressed)
        }
        function test_touch_press_feedback_clears_on_drag() {
            action.focus=false; mouseMove(vertical,470,10); wait(20)
            let seq=touchEvent(vertical); activeSequence=seq; seq.press(0,vertical,200,320).commit(); wait(20)
            verify(action.pressed); let feedback=action.interactionActive
            for(let y=300;y>=100;y-=20) { seq.move(0,vertical,200,y).commit(); wait(16) }
            seq.release(0,vertical,200,100).commit(); activeSequence=null; wait(20)
            verify(feedback,"touch press must expose alpha interaction feedback"); verify(!action.pressed); compare(activated.count,0)
        }
        function test_stationary_tap_activates_once() {
            touchEvent(vertical).press(0,vertical,200,120).commit()
            touchEvent(vertical).release(0,vertical,200,120).commit(); wait(20); compare(activated.count,1)
        }
    }
}
