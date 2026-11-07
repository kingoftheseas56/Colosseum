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
    SignalSpy { id: menus; signalName:"contextMenuRequested" }
    property var pendingTouch: null
    property var pendingTouchTarget: null
    property var pendingTouchIds: []
    function touchSequence(item,ids) {
        pendingTouchTarget=item
        pendingTouchIds=ids || [0]
        pendingTouch=touchEvent(item)
        return pendingTouch
    }
    function cleanup() {
        if (pendingTouch) {
            for (const id of pendingTouchIds) pendingTouch.release(id,pendingTouchTarget,-50,-50)
            pendingTouch.commit()
        }
        pendingTouch=null
        pendingTouchTarget=null
        pendingTouchIds=[]
    }
    function mouseDoubleClick(item,x,y) { mouseDoubleClickSequence(item,x,y,Qt.LeftButton) }
    function surface(properties) {
        window.requestActivate()
        const item=createTemporaryObject(surfaceComponent,window.contentItem,properties || {})
        verify(item !== null)
        seeks.target=item; taps.target=item; volumes.target=item; brightness.target=item; fits.target=item; menus.target=item
        seeks.clear(); taps.clear(); volumes.clear(); brightness.clear(); fits.clear(); menus.clear()
        return item
    }
    function tapWithTouch(item,x,y) {
        const points=touchSequence(item)
        points.press(0,item,x,y).commit()
        wait(20)
        points.release(0,item,x,y).commit()
        pendingTouch=null
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
        const points=touchSequence(item,[0,1])
        points.press(0,item,320,160).press(1,item,460,160).commit()
        wait(20)
        points.move(0,item,290,160).move(1,item,490,160).commit()
        wait(20)
        points.move(0,item,250,160).move(1,item,530,160).commit()
        wait(20)
        points.release(0,item,250,160).release(1,item,530,160).commit()
        pendingTouch=null
        tryCompare(fits,"count",1)
        compare(fits.signalArguments[0][0],true)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0); compare(seeks.count,0)
    }
    function test_touch_taps_and_double_tap_are_distinct() {
        const item=surface()
        tapWithTouch(item,396,160)
        tryCompare(taps,"count",1)
        tapWithTouch(item,150,160)
        wait(30)
        tapWithTouch(item,150,160)
        tryCompare(seeks,"count",1)
        compare(seeks.signalArguments[0][0],-10)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,1)
    }
    function test_touch_drag_does_not_tap_or_open_menu() {
        const item=surface({adjustmentsEnabled:true,volumeLevel:50})
        const points=touchSequence(item)
        points.press(0,item,640,240).commit()
        wait(20)
        points.move(0,item,640,210).commit()
        wait(20)
        points.move(0,item,640,150).commit()
        wait(20)
        points.release(0,item,640,150).commit()
        pendingTouch=null
        verify(volumes.count>0)
        verify(volumes.signalArguments[volumes.count-1][0]>50)
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0); compare(seeks.count,0); compare(menus.count,0)
    }
    function test_pending_tap_cancellation_data() {
        return [
            {tag:"disable",property:"enabled",value:false},
            {tag:"hide",property:"visible",value:false},
            {tag:"lock",property:"locked",value:true},
            {tag:"modal",property:"modalOpen",value:true},
            {tag:"media-or-lifecycle",property:"",value:false}
        ]
    }
    function test_pending_tap_cancellation(data) {
        const item=surface()
        tapWithTouch(item,396,160)
        if (data.property.length) item[data.property]=data.value
        else item.cancelGesture()
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0)
        compare(seeks.count,0)
    }
    function test_modal_transition_cancels_active_drag_but_allows_fresh_dismiss_tap() {
        const item=surface({adjustmentsEnabled:true,volumeLevel:50})
        const points=touchSequence(item)
        points.press(0,item,640,240).commit()
        wait(20)
        points.move(0,item,640,200).commit()
        wait(20)
        verify(volumes.count>0)
        const before=volumes.count
        item.modalOpen=true
        item.modalOpen=false
        points.move(0,item,640,140).commit()
        wait(20)
        points.release(0,item,640,140).commit()
        pendingTouch=null
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(volumes.count,before)
        compare(taps.count,0); compare(seeks.count,0)
        item.modalOpen=true
        tapWithTouch(item,396,160)
        tryCompare(taps,"count",1,1000,"A new outside tap must still reach the page's menu-dismiss handler")
        compare(volumes.count,before)
    }
    function test_hold_opens_existing_menu_without_release_tap() {
        const item=surface()
        const area=findChild(item,"playerTouchGestureArea")
        verify(area!==null)
        item.contextMenuRequested.connect(function() { item.modalOpen=true })
        const points=touchSequence(item)
        points.press(0,item,396,160).commit()
        tryCompare(menus,"count",1,area.pressAndHoldInterval+500)
        compare(Math.round(menus.signalArguments[0][0]),396)
        compare(Math.round(menus.signalArguments[0][1]),160)
        points.release(0,item,396,160).commit()
        pendingTouch=null
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(taps.count,0); compare(seeks.count,0)
    }
    function test_swipe_then_hold_is_not_long_press() {
        const item=surface()
        const area=findChild(item,"playerTouchGestureArea")
        const points=touchSequence(item)
        points.press(0,item,640,160).commit()
        wait(20)
        points.move(0,item,540,160).commit()
        wait(area.pressAndHoldInterval+50)
        points.release(0,item,540,160).commit()
        pendingTouch=null
        wait(Qt.styleHints.mouseDoubleClickInterval+30)
        compare(menus.count,0); compare(taps.count,0); compare(seeks.count,0)
    }
}
