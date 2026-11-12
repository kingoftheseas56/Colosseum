import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/vinyl" as Vinyl
TestCase {
    id: test
    name: "VinylQueueViewedActions"
    when: windowShown
    property var host
    property var panel
    Component {
        id: factory
        Window {
            width: 560; height: 680; visible: true
            property alias session: session
            property alias viewed: viewed
            Item { id: backdrop; anchors.fill: parent }
            QtObject {
                id: session
                property var queue: []
                property int queueIndex: 0
                property bool canPrevious: false
                property bool canNext: true
                property bool shuffleEnabled: false
                property string repeatMode: "off"
                property int lastPlayed: -1
                property var playingTrack: ({id:"one",title:"Playing song",available:true})
                property string playingEditionId: "playing"
                function artwork(url) { return "" }
                function formatTime(n) { return "3:00" }
                function setShuffle(value) { shuffleEnabled=value }
                function cycleRepeat() { repeatMode=repeatMode==="off" ? "all" : "off" }
                function clearQueue() { queue=[] }
                function next(n) { queueIndex=Math.max(0,Math.min(queue.length-1,queueIndex+n)) }
                function playQueue(index) { lastPlayed=index;queueIndex=index }
                function moveQueue(from,to) { var copy=session.queue.slice(),moved=copy.splice(from,1)[0];copy.splice(to,0,moved);session.queue=copy }
                function removeQueue(index) { var copy=session.queue.slice();copy.splice(index,1);session.queue=copy }
                function enqueuePlayingSong(next) { return false }
            }
            QtObject {
                id: viewed
                property var playingTrack: ({id:"viewed",title:"Viewed song",available:true})
                property string playingEditionId: "viewed-edition"
                property bool accept: true
                property int requests: 0
                property string browseError: ""
                function enqueuePlayingSong(next) {
                    requests++
                    if(!accept)return false
                    var copy=session.queue.slice(),row={queueId:"viewed-"+requests,title:"Viewed song",artist:"Artist",seconds:180,cover:"",edition:{name:"Album",discs:[{}]}}
                    copy.splice(next ? session.queueIndex+1 : copy.length,0,row)
                    session.queue=copy
                    return true
                }
            }
            Vinyl.VinylQueuePanel { id: queuePopup; pageData: session; actionData: viewed; backdrop: backdrop }
        }
    }
    function item(name) {
        function walk(node) {
            if(node.objectName===name)return node
            for(var child of node.children || []) { var match=walk(child);if(match)return match }
            return null
        }
        return findChild(panel,name) || walk(panel.contentItem)
    }
    function init() {
        host=factory.createObject(null);verify(host!==null)
        host.session.queue=[0,1,2].map(i=>({queueId:String(i),title:["Kiara","Kong","Eyesdown"][i],artist:"Bonobo",seconds:180,cover:"",edition:{name:"Black Sands",discs:[{}]}}))
        panel=findChild(host,"vinylQueuePanel");panel.open();tryCompare(panel,"opened",true);waitForRendering(host.contentItem)
    }
    function cleanup() { host.close();host.destroy();panel=null;host=null }
    function test_trueSuccessInsertsViewedSongAtRequestedPosition() {
        item("vinylSongNext").activate();compare(host.session.queue[1].title,"Viewed song")
        compare(panel.actionFeedback,"Viewed song will play next.")
        item("vinylSongQueue").activate();compare(host.session.queue[host.session.queue.length-1].title,"Viewed song")
        compare(panel.actionFeedback,"Viewed song added to the queue.")
        compare(host.session.playingTrack.title,"Playing song");compare(panel.pageData,host.session)
    }
    function test_falseAndVoidCallsNeverShowSuccess() {
        host.viewed.accept=false;var count=host.session.queue.length
        item("vinylSongNext").activate();compare(host.session.queue.length,count)
        compare(panel.actionFeedback,"The song could not be added to the queue.")
        panel.actionData={playingTrack:{id:"void",title:"Void song",available:true},enqueuePlayingSong:function(){}}
        item("vinylSongQueue").activate();compare(panel.actionFeedback,"The song could not be added to the queue.")
    }
    function test_contextChangeClearsFeedbackAndUnavailableCannotAdd() {
        item("vinylSongNext").activate();verify(panel.actionFeedback.length>0)
        host.viewed.playingTrack={id:"other",title:"Unavailable song",available:false}
        compare(panel.actionFeedback,"");verify(!item("vinylSongNext").enabled)
        item("vinylSongNext").activate();compare(host.viewed.requests,1)
    }
    function test_inlineRowControlsAndKeyboardReorderRemovePlay() {
        tryVerify(()=>item("vinylQueueDown_0")!==null);item("vinylQueueDown_0").activate();compare(host.session.queue[1].title,"Kiara");waitForRendering(host.contentItem)
        tryVerify(()=>item("vinylQueueUp_1")!==null);item("vinylQueueUp_1").activate();compare(host.session.queue[0].title,"Kiara");waitForRendering(host.contentItem)
        item("vinylQueueRemove_2").activate();compare(host.session.queue.length,2)
        var list=item("vinylQueueList");list.forceActiveFocus();list.currentIndex=0
        keyClick(Qt.Key_Down,Qt.ControlModifier);compare(host.session.queue[1].title,"Kiara")
        keyClick(Qt.Key_Delete);compare(host.session.queue.length,1)
        list.currentIndex=0;keyClick(Qt.Key_Return);compare(host.session.lastPlayed,0)
        compare(item("vinylQueueTrackMenu"),null)
    }
    function test_narrowLabelledActionsFit() {
        host.width=360;waitForRendering(host.contentItem)
        for(var name of ["vinylSongNext","vinylSongQueue"]) {
            var button=item(name),point=button.mapToItem(panel.contentItem,0,0)
            verify(point.x>=0 && point.x+button.width<=panel.availableWidth+1,name)
            verify(button.accessibleName.length>0)
        }
        keyClick(Qt.Key_Escape);tryCompare(panel,"opened",false)
    }
}
