import QtQuick
import QtTest
TestCase {
    name: "VinylWorldJourneys"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem,name) }
    function click(name) {
        var target=item(name); verify(target!==null,name)
        for(var p=target.parent;p;p=p.parent) {
            if(p.contentY!==undefined && p.contentItem!==undefined && p.contentHeight!==undefined) {
                var y=target.mapToItem(p.contentItem,0,target.height/2).y
                if(y<p.contentY+20 || y>p.contentY+p.height-20)
                    p.contentY=Math.max(0,Math.min(p.contentHeight-p.height,y-p.height/2))
            }
        }
        waitForRendering(host.contentItem)
        mouseClick(target); waitForRendering(host.contentItem)
    }
    function shot(name) {
        if(typeof VinylCaptureDirectory!=="undefined" && VinylCaptureDirectory.length) {
            waitForRendering(host.contentItem)
            grabImage(host.contentItem).save(VinylCaptureDirectory+"/journey-"+name+".png")
        }
    }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null,{page:"world",width:1280,height:720})
        host.pageData.frozen=true
        tryCompare(host,"ready",true)
    }
    function cleanup(){if(host){host.close();host.destroy()}}
    function test_discoverJourney() {
        click("vinylTab_discover")
        var world=item("vinylWorld")
        tryCompare(world,"currentTab","discover")
        host.worldScroll.contentY=world.contentTop
        waitForRendering(host.contentItem)
        click("vinylFilter_language")
        click("vinylFilterValue_language_Telugu")
        tryCompare(item("vinylLens"),"title","Telugu")
        shot("discover-lens")
        click("vinylShelfSeeAll_popular")
        tryCompare(item("vinylListPage"),"title","Popular")
        shot("discover-list")
        click("vinylListBack")
        tryCompare(world,"listKey","")
        var chosen=host.worldData.listItems("popular")[0]
        var origin=host.worldScroll.contentY
        click("vinylCard_popular_0")
        tryCompare(host,"page","album")
        tryCompare(host.pageData,"shownAlbumId",chosen.albumId)
        var album=item("vinylAlbumPage")
        // Track rows belong to the album's own scroll surface.
        var track=item("vinylTrackRow_1")
        verify(track!==null)
        album.contentY=0
        click("vinylTrackRow_1")
        tryCompare(host.pageData,"playingIndex",1)
        shot("discover-second-track")
        click("vinylAlbumBack")
        tryCompare(host,"page","world")
        compare(world.currentTab,"discover")
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-origin)<4})
        shot("discover-return")
    }
    function test_homeJourney() {
        var world=item("vinylWorld")
        click("vinylDevProfileNew")
        click("vinylDevResetPicker")
        tryCompare(item("vinylTastePicker"),"visible",true)
        host.worldScroll.contentY=world.contentTop
        waitForRendering(host.contentItem)
        click("vinylPickerSkip")
        tryCompare(host.worldData,"pickerAnswered",true)
        tryCompare(item("vinylHomeColdShelf_popular"),"visible",true)
        shot("home-skip")
        click("vinylDevResetPicker")
        tryCompare(item("vinylTastePicker"),"visible",true)
        host.worldScroll.contentY=world.contentTop
        waitForRendering(host.contentItem)
        click("vinylPickerArtist_0")
        click("vinylPickerArtist_1")
        click("vinylPickerDone")
        tryCompare(host.worldData,"pickerAnswered",true)
        tryCompare(item("vinylHomeRow_recommend-songs"),"visible",true)
        shot("home-picks")
        click("vinylDevProfileSeasoned")
        tryCompare(host.worldData,"profile","seasoned")
        var reason=item("vinylCard_recommend-songs_0").reason
        verify(reason.length>0)
        console.log("Seasoned recommendation reason: "+reason)
        shot("home-seasoned")
        host.worldScroll.contentY=0
        waitForRendering(host.contentItem)
        click("featuredPrimaryAction_0")
        tryCompare(host,"page","song")
        shot("home-resume")
    }
}
