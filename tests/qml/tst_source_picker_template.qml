import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    name:"SourcePickerTemplate"
    when:windowShown
    Window { id:host; visible:true; width:354; height:792; property bool androidHost:true }
    Component { id:pageComponent; App.MangaTankobanSourcesPage { anchors.fill:parent } }
    QtObject {
        id:service
        property var calls:[]
        function downloadNyaa(volume,hash) { calls=calls.concat([{volume:volume,hash:hash}]) }
        function statusOf(volume) { return {state:"downloading"} }
    }
    function find(item,name) {
        if (item.objectName===name) return item
        for (const child of item.children || []) { const result=find(child,name); if (result) return result }
        return null
    }
    function test_manga_card_wraps_and_preserves_transport_identity_data() {
        return [{tag:"narrow",width:354,height:792},{tag:"phone",width:412,height:900},{tag:"tablet",width:800,height:1280}]
    }
    function test_manga_card_wraps_and_preserves_transport_identity(data) {
        host.width=data.width; host.height=data.height
        service.calls=[]
        const source={infoHash:"1234567890123456789012345678901234567890",uploader:"Archive",releaseTitle:"A very long release title ".repeat(8),sizeBytes:120000000,seeders:10,tier:1,enabled:true}
        const page=createTemporaryObject(pageComponent,host.contentItem,{open:true,service:service,rows:[source],context:{volumeId:"volume-42"}})
        wait(80)
        const card=find(page,"tankobanSourceCard_0")
        verify(card !== null && card.visible)
        verify(card.height>=48 && card.width<=page.width)
        const action=find(page,"tankobanSourceDownload_0")
        verify(action !== null && action.visible && action.width>=48 && action.height>=48)
        mouseClick(action,action.width/2,action.height/2)
        compare(service.calls.length,1)
        compare(service.calls[0].volume,"volume-42")
        compare(service.calls[0].hash,source.infoHash)
        compare(page.livePhase,"live")
        compare(action.enabled,false)
    }
}
