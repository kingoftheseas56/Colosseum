import QtQuick
import QtTest
TestCase {
    id: test
    name: "VinylQueuePanelContracts"
    when: windowShown
    width: 520
    height: 600
    Item {id:host;anchors.fill:parent}
    Item {id:focusTarget;activeFocusOnTab:true;width:20;height:20}
    property var panel
    property var session
    function init() {
        var sc=Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");compare(sc.status,Component.Ready,sc.errorString());
        session=sc.createObject(host,{source:null});
        session.shownEdition=session.normalize({id:"a",title:"Album",artists:["Artist"],tracks:[{id:"1",title:"One"},{id:"2",title:"Two"},{id:"3",title:"Three"}]});
        session.enqueueTrack(0);session.enqueueTrack(1);session.enqueueTrack(2);
        var c=Qt.createComponent("../../qml/vinyl/VinylQueuePanel.qml");compare(c.status,Component.Ready,c.errorString());
        panel=c.createObject(host,{pageData:session,backdrop:host});verify(panel!==null);focusTarget.forceActiveFocus();panel.open();wait(20);
    }
    function cleanup() {panel.close();panel.destroy();session.destroy();wait(0);}
    function test_keyboardSelectMoveRemoveAndPlay() {
        var list=findChild(panel,"vinylQueueList");verify(list!==null);list.forceActiveFocus();
        list.currentIndex=0;keyClick(Qt.Key_Down);compare(list.currentIndex,1);
        keyClick(Qt.Key_Down,Qt.ControlModifier);compare(session.queue[2].trackId,"2");
        keyClick(Qt.Key_Delete);compare(session.queue.length,2);
        list.currentIndex=0;keyClick(Qt.Key_Return);compare(session.playingTrack.id,"1");
    }
    function test_narrowBoundsAndEscape() {
        compare(panel.width,488);verify(panel.height<=host.height);
        var clear=findChild(panel,"vinylQueueClear");verify(clear!==null);
        var point=clear.mapToItem(panel.contentItem,0,0);verify(point.x>=0);verify(point.x+clear.width<=panel.availableWidth+1);
        keyClick(Qt.Key_Escape);tryCompare(panel,"visible",false);tryCompare(focusTarget,"activeFocus",true);
    }
}
