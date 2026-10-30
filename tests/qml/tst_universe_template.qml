import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id:tests
    name:"UniverseTemplate"
    when:windowShown
    Window { id:host;visible:true;width:354;height:792;property bool androidHost:true }
    Component { id:pageComponent;App.UniverseExtensionPage { anchors.fill:parent } }
    SignalSpy { id:watchSpy;signalName:"watchRequested" }
    function find(item,name) {
        if(item.objectName===name)return item
        for(const child of item.children || []){const found=find(child,name);if(found)return found}
        return null
    }
    function test_payload_sizes_data() {
        return [{tag:"narrow",w:354,h:792},{tag:"phone",w:412,h:900},{tag:"tablet",w:800,h:1280},{tag:"wide",w:1280,h:800},{tag:"desktop",w:1280,h:700,desktop:true}]
    }
    function test_payload_sizes(data) {
        host.width=data.w;host.height=data.h;host.androidHost=!data.desktop
        const page=createTemporaryObject(pageComponent,host.contentItem)
        verify(page!==null)
        page.payload={title:"A voyage through distant worlds",background:"",sections:[{title:"Films",kind:"video",entries:[{id:"",title:"A new beginning",type:"movie"},{id:"",title:"A second voyage",type:"series"}]}]}
        wait(60)
        const title=find(page,"universePayloadTitle")
        const rail=find(page,"universeSectionRail_0")
        verify(title!==null && rail!==null)
        verify(title.x+title.width<=data.w)
        verify(rail.width>0 && rail.width<=data.w-page.universeInset*2)
        if(!data.desktop)verify(rail.posterWidth*2+rail.spacing<=rail.width)
        const tile=rail.itemAtIndex(0)
        verify(tile!==null)
        watchSpy.target=page;watchSpy.clear()
        mouseClick(tile,tile.width/2,Math.min(30,tile.height/2))
        compare(watchSpy.count,1)
        compare(watchSpy.signalArguments[0][0].title,"A new beginning")
        compare(watchSpy.signalArguments[0][0].type,"movie")
    }
}
