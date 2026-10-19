import QtQuick
import QtTest
TestCase {
    name: "VinylWorldVisual"
    when: windowShown
    property var host
    function item(name){return findChild(host.contentItem,name)}
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null,{page:"world",developerStrip:false})
        host.pageData.frozen=true;tryCompare(host,"ready",true)
    }
    function cleanup(){if(host){host.close();host.destroy()}}
    function test_visualEvidence_data() {
        return [{tag:"compact",w:1280,h:720},{tag:"wide",w:1600,h:900}]
    }
    function test_visualEvidence(data) {
        if(typeof VinylCaptureDirectory==="undefined" || !VinylCaptureDirectory.length)
            skip("No explicit capture directory")
        host.width=data.w;host.height=data.h
        tryCompare(host,"width",data.w);tryCompare(host,"height",data.h)
        var world=item("vinylWorld")
        function shot(name) {
            // Wait for every on-screen cover, not just the first delegate. The
            // shared image primitive fades ready textures in after loading.
            function visibleArtReady(node) {
                if(!node.visible) return true
                if(node.sources!==undefined && node.ready!==undefined) {
                    var pos=node.mapToItem(host.contentItem,0,0)
                    if(pos.x<data.w && pos.x+node.width>0 && pos.y<data.h && pos.y+node.height>0 && !node.ready)
                        return false
                }
                for(var i=0;i<node.children.length;i++)
                    if(!visibleArtReady(node.children[i])) return false
                return true
            }
            tryVerify(function(){return visibleArtReady(host.contentItem)},15000)
            wait(350)
            waitForRendering(host.contentItem)
            var img=grabImage(host.contentItem)
            compare(img.width,data.w);compare(img.height,data.h)
            img.save(VinylCaptureDirectory+"/"+data.tag+"-"+name+".png")
        }
        host.worldScroll.contentY=world.contentTop

        // The page is rendered with actual fixture textures; asynchronous image
        // readiness is verified through the public house art primitive below.
        tryVerify(function(){
            var artist=item("vinylPickerArtist_0")
            return artist && artist.children.some(function(c){return c.ready===true})
        },15000)
        shot("picker")
        host.worldScroll.contentY=0
        shot("world-top")
        host.worldScroll.contentY=world.contentTop
        host.worldData.skipPicker()
        host.worldScroll.contentY=world.contentTop
        tryCompare(item("vinylCard_cold-popular_0_cover"),"ready",true,15000)
        shot("home-skipped")
        host.worldData.resetPicker()
        var picks=host.worldData.artistOptions("").slice(0,3).map(function(a){return a.id})
        host.worldData.answerPicker(picks)
        shot("home-picked")
        host.worldData.setProfile("seasoned")
        host.worldScroll.contentY=world.contentTop
        shot("home-seasoned")
        var recommendation=item("vinylHomeRow_recommend-songs")
        verify(recommendation!==null)
        host.worldScroll.contentY=recommendation.mapToItem(world,0,0).y
        shot("home-reasons")
        world.setTab("discover")
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop
        tryCompare(item("vinylCard_popular_0_cover"),"ready",true,15000)
        shot("discover-everything")
        host.worldData.setLens("language","Telugu");host.worldData.setLens("decade","1990s")
        tryCompare(item("vinylCard_popular_0_cover"),"ready",true,15000)
        shot("discover-telugu-1990s")
        host.worldData.openList("popular",false)
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop
        tryCompare(item("vinylListCard_0_cover"),"ready",true,15000)
        shot("see-all")
        host.worldData.setKind("artists")
        tryCompare(item("vinylListCard_0_cover"),"ready",true,15000)
        shot("artists")
    }
}
