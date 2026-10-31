import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhonePlayerGestures"
    when: windowShown
    Window { id: window; width:792; height:354; visible:true; property bool androidHost:true }
    Component { id: surfaceComponent; App.PlayerTouchSurface { width:window.width; height:window.height; seekEnabled:true } }
    SignalSpy { id: seeks; signalName:"seekRequested" }
    SignalSpy { id: taps; signalName:"singleTapped" }
    SignalSpy { id: volumes; signalName:"volumeRequested" }
    SignalSpy { id: brightness; signalName:"brightnessRequested" }
    SignalSpy { id: fits; signalName:"fitRequested" }
    function mouseDoubleClick(item,x,y) { mouseDoubleClickSequence(item,x,y,Qt.LeftButton) }
    function surface(properties) {
        window.requestActivate()
        const item=createTemporaryObject(surfaceComponent,window.contentItem,properties || {})
        verify(item !== null)
        seeks.target=item; taps.target=item; volumes.target=item; brightness.target=item; fits.target=item
        seeks.clear(); taps.clear(); volumes.clear(); brightness.clear(); fits.clear()
        return item
    }
    function test_double_tap_sides_and_center_single_tap() {
        const item=surface()
        mouseDoubleClick(item,150,160)
        compare(seeks.count,1); compare(seeks.signalArguments[0][0],-10)
        mouseDoubleClick(item,640,160)
        compare(seeks.count,2); compare(seeks.signalArguments[1][0],10)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0,"Seeking must not toggle the HUD or pause state")
        mouseClick(item,396,160)
        tryCompare(taps,"count",1)
        compare(seeks.count,2)
        verify(item.feedback.length > 0)
    }
    function test_locked_modal_disabled_and_edge_guards() {
        const item=surface({locked:true})
        mouseDoubleClick(item,150,160)
        compare(seeks.count,0)
        item.locked=false; item.modalOpen=true
        mouseDoubleClick(item,640,160)
        compare(seeks.count,0)
        item.modalOpen=false; item.seekEnabled=false
        mouseDoubleClick(item,150,160)
        compare(seeks.count,0)
        item.seekEnabled=true
        for (const point of [Qt.point(5,160),Qt.point(787,160),Qt.point(150,5),Qt.point(150,349)])
            mouseDoubleClick(item,point.x,point.y)
        compare(seeks.count,0)
        item.enabled=false
        mouseDoubleClick(item,640,160)
        compare(seeks.count,0)
    }
    function test_swipes_are_opt_in_and_do_not_become_taps() {
        const item=surface({volumeLevel:50,brightnessLevel:0.5})
        mouseDrag(item,640,230,0,-90,Qt.LeftButton)
        compare(volumes.count,0)
        item.adjustmentsEnabled=true
        mouseDrag(item,640,230,0,-90,Qt.LeftButton)
        verify(volumes.count > 0)
        verify(volumes.signalArguments[volumes.count-1][0] > 50)
        mouseDrag(item,150,230,0,-90,Qt.LeftButton)
        verify(brightness.count > 0)
        verify(brightness.signalArguments[brightness.count-1][0] > 0.5)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0); compare(seeks.count,0)
        item.modalOpen=true
        const before=volumes.count
        mouseDrag(item,640,230,0,-90,Qt.LeftButton)
        compare(volumes.count,before)
    }
    function test_pinch_routes_fit_once_and_cancels_pending_taps() {
        const item=surface({adjustmentsEnabled:true,fitEnabled:true})
        const points=touchEvent(item)
        points.press(0,item,320,160).press(1,item,460,160).commit()
        wait(20)
        points.move(0,item,290,160).move(1,item,490,160).commit()
        wait(20)
        points.move(0,item,250,160).move(1,item,530,160).commit()
        wait(20)
        points.release(0,item,250,160).release(1,item,530,160).commit()
        tryCompare(fits,"count",1)
        compare(fits.signalArguments[0][0],true)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0); compare(seeks.count,0)
    }
}
