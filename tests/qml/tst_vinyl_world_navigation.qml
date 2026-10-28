import QtQuick
import QtTest
TestCase {
    name: "VinylWorldNavigation"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem,name) }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null)
        host.pageData.frozen=true
        host.page="world"
        tryCompare(host,"ready",true)
    }
    function cleanup() { if(host){host.close();host.destroy()} }
    function test_carouselSameAcrossTabs() {
        var world=item("vinylWorld"), carousel=item("vinylCarousel")
        verify(world!==null && carousel!==null)
        var playing=host.pageData.playingEditionId
        ;["home","discover","library"].forEach(function(tab) {
            world.setTab(tab); compare(item("vinylCarousel"),carousel)
            verify(carousel.visible);compare(host.pageData.playingEditionId,playing)
        })
        host.worldData.resume();compare(host.page,"song")
    }
    function test_snapshotAlbumRoundTrip() {
        var world=item("vinylWorld")
        verify(world!==null)
        world.setTab("discover")
        var chosen=host.worldData.listItems("popular")[2]
        var y=world.contentTop
        host.worldScroll.contentY=y
        host.worldData.activate(chosen)
        compare(host.page,"album")
        compare(host.pageData.shownAlbumId,chosen.albumId)
        compare(host.pageData.shownEdition.title,chosen.title)
        var a=host.worldData.catalogue.albums.find(function(a){return a.id===chosen.albumId})
        compare(host.pageData.tracks.length,a.tracks.length)
        mouseClick(item("vinylAlbumBack"))
        compare(host.page,"world")
        compare(world.currentTab,"discover")
        verify(Math.abs(host.worldScroll.contentY-y)<4)
    }
    function test_seeAllReturnAndSongStays() {
        var world=item("vinylWorld");verify(world!==null)
        world.setTab("discover");host.worldData.openList("popular",false)
        compare(world.listKey,"popular")
        host.worldData.activate(host.worldData.listItems("popular")[1]);compare(host.page,"album")
        mouseClick(item("vinylAlbumBack"));compare(world.listKey,"popular")
        host.worldData.setKind("songs")
        var s=host.worldData.listItems("popular")[0]
        host.worldData.activate(s)
        compare(host.page,"world")
        compare(host.pageData.playingTrack.title,s.title)
    }
    function test_songAlbumRoundTripKeepsWorldOrigin() {
        var world=item("vinylWorld");world.setTab("discover")
        host.worldData.activate(host.worldData.listItems("popular")[2])
        host.pageData.playTrack(0)
        host.openSong()
        var song=item("vinylSongPage")
        verify(song!==null)
        song.albumRequested()
        compare(host.page,"album")
        mouseClick(item("vinylAlbumBack"))
        compare(host.page,"world")
        compare(world.currentTab,"discover")
    }
    function test_playingOriginalAlbumCanReplaceInspectedSnapshot() {
        var original=host.pageData.playingEditionId
        host.worldData.activate(host.worldData.listItems("popular")[2])
        verify(host.pageData.shownEditionId!==original)
        host.openSong()
        item("vinylSongPage").albumRequested()
        compare(host.pageData.shownEditionId,original)
        compare(host.pageData.shownEdition.name,host.pageData.playingEdition.name)
    }
    function test_repeatedSongRequestKeepsBackOrigin() {
        host.openSong();host.openSong()
        item("vinylSongPage").backRequested()
        compare(host.page,"world")
    }
    function test_browsingDoesNotCancelContinuousPlay() {
        host.worldData.setProfile("seasoned")
        var mix=host.worldData.homeRows.find(function(r){return r.kind==="mixes"}).items[0]
        host.worldData.activate(mix)
        var queue=host.pageData.continuousQueue.length
        verify(queue>0)
        var playing=host.pageData.playingEditionId
        host.worldData.activate(host.worldData.listItems("popular")[2])
        compare(host.pageData.continuousQueue.length,queue)
        compare(host.pageData.playingEditionId,playing)
        host.pageData.playTrack(0)
        compare(host.pageData.continuousQueue.length,0)
    }
    function test_leavingListKeepsShelfOffset() {
        var world=item("vinylWorld");world.setTab("discover")
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop+220
        var origin=host.worldScroll.contentY
        host.worldData.openList("popular",false)
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop+650
        waitForRendering(host.contentItem)
        var listOffset=host.worldScroll.contentY
        world.setTab("home")
        waitForRendering(host.contentItem)
        world.setTab("discover")
        waitForRendering(host.contentItem)
        compare(world.listKey,"popular")
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-listOffset)<4})
        world.backFromList()
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-origin)<4})
    }
    function test_snapshotMissingSectionsAbsent() {
        verify(host.worldData!==undefined)
        host.worldData.activate(host.worldData.listItems("popular")[0])
        ;["about","credits","videos","editions","information"].forEach(function(k) {
            verify(!item("vinylSection_"+k).visible,k)
        })
        host.pageData.setFixture("thriller")
        ;["about","credits","videos","editions","information"].forEach(function(k) {
            verify(item("vinylSection_"+k).visible,k)
        })
    }
}
