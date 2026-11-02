// Regression coverage of the retained production scroll owners; no alternate gesture implementation.
import QtQuick
import QtTest
import "../../qml" as App
Item {
    width: 480; height: 420
    Flickable {
        id: vertical; anchors.fill: parent; contentWidth: width; contentHeight: 1400
        boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.VerticalFlick
        App.PosterRail {
            id: rail; width: 480; title: "Touch test"
            items: Array.from({length:12}, (_,i)=>({id:"fixture"+i,title:"Fixture "+i,type:"movie",poster:""}))
        }
    }
    SignalSpy { id: activated; target: rail; signalName: "itemRequested" }
    TestCase {
        name: "AndroidSharedPosterTouch"; when: windowShown
        property var activeSequence: null
        function cleanup() {
            if (activeSequence) { activeSequence.release(0,vertical,200,100).commit(); activeSequence=null; wait(20) }
        }
        function list() { return rail.children.find(c=>c.orientation!==undefined && c.contentX!==undefined) }
        function init() { vertical.cancelFlick(); vertical.contentY=0; list().cancelFlick(); list().contentX=0; activated.clear(); wait(80) }
        function swipe(x1,y1,x2,y2) {
            let seq=touchEvent(vertical); activeSequence=seq; seq.press(0,vertical,x1,y1).commit(); wait(30)
            for(let i=1;i<=10;i++) { seq.move(0,vertical,x1+(x2-x1)*i/10,y1+(y2-y1)*i/10).commit(); wait(16) }
            seq.release(0,vertical,x2,y2).commit(); activeSequence=null; wait(30)
        }
        function test_horizontal_drag() { swipe(390,140,100,140); verify(rail.currentContentX>80); compare(activated.count,0); compare(vertical.contentY,0) }
        function test_vertical_drag() { swipe(90,240,90,40); verify(vertical.contentY>60); compare(activated.count,0); fuzzyCompare(rail.currentContentX,0,1) }
        function test_stationary_tap() { touchEvent(vertical).press(0,vertical,60,140).commit(); touchEvent(vertical).release(0,vertical,60,140).commit(); wait(30); compare(activated.count,1) }
    }
}
