import QtQuick
import QtTest
TestCase {
    name: "VinylWorldJourneys"
    when: windowShown
    property var host
    QtObject {
        id: discoverSource
        property int serial: 0
        property var lastRequest: null
        signal completed(string lane, int id, var data, string error)
        function request(lane, operation, kind, options) {
            lastRequest = {id: ++serial, lane: lane, operation: operation, kind: kind, options: options}
            return serial
        }
        function cancel(lane) {}
    }
    function replyToChart(list, entries, listOptions) {
        tryVerify(function() {
            return discoverSource.lastRequest && discoverSource.lastRequest.options.list === list
        }, 5000, "Expected Discover request for " + list)
        var request = discoverSource.lastRequest
        compare(request.operation, "browseCatalogue")
        compare(request.kind, "albums")
        compare(request.options.catalogueSource, "jiosaavn")
        discoverSource.completed(request.lane, request.id, {
            version: 1, kind: request.kind, source: "jiosaavn", list: list,
            items: entries, listOptions: listOptions || [], complete: true
        }, "")
    }
    function openDiscoverFilter() {
        click("vinylDiscoverFilterPickerPill")
        var list = findChild(host, "vinylDiscoverFilterPickerList")
        verify(list !== null)
        tryCompare(list, "activeFocus", true)
        return list
    }
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
        var worldY = host.worldScroll.contentY
        mouseClick(target); waitForRendering(host.contentItem)
        return worldY
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
        // The snapshot host has local album routes but no live catalogue transport.
        // Exercise the real Discover browser with deterministic provider replies.
        discoverSource.lastRequest = null
        var discover = item("vinylDiscoverPage")
        verify(discover !== null)
        discover.worldData = {
            session: {source: discoverSource, profileActive: true},
            activate: function(card) { host.worldData.activate(card) }
        }
        click("vinylTab_discover")
        var world = item("vinylWorld")
        var browser = item("vinylDiscoverBrowser")
        tryCompare(world, "currentTab", "discover")
        tryCompare(browser, "currentSource", "Worldwide")
        host.worldScroll.contentY = world.contentTop
        click("vinylDiscoverType_albums")
        tryCompare(browser, "currentType", "albums")
        browser.selectSource("Indian")
        compare(browser.currentCatalog.attribution, "JioSaavn")
        replyToChart("tamil:default", [])

        var filter = openDiscoverFilter()
        keyClick(Qt.Key_Home) // All
        keyClick(Qt.Key_Down) // Tamil (skips the Language heading)
        keyClick(Qt.Key_Down) // Telugu
        compare(filter.currentItem.modelData.text, "Telugu")
        keyClick(Qt.Key_Return)
        replyToChart("telugu:default", [], [{key: "telugu-decade", label: "Telugu decade"}])
        compare(browser.filters.Language, "telugu")
        shot("discover-language")

        filter = openDiscoverFilter()
        keyClick(Qt.Key_End)
        compare(filter.currentItem.modelData.text, "Telugu decade")
        keyClick(Qt.Key_Return)
        var chosen = host.worldData.catalogue.albums.find(function(album) {
            return album.language === "Telugu" && album.tracks.length > 1
        })
        verify(chosen !== undefined)
        replyToChart("telugu:telugu-decade", [{
            id: "journey-album", albumId: chosen.id, kind: "albums", catalogueCard: true,
            title: chosen.title, cover: host.worldData.artwork(chosen.cover), source: "jiosaavn"
        }])
        tryCompare(discover, "loading", false)
        compare(discover.items.length, 1)
        compare(browser.filters.Language, "telugu")
        compare(browser.filters.List, "telugu-decade")
        shot("discover-list")
        tryVerify(function() { return item("discoverCard_journey-album") !== null })
        var origin = click("discoverCard_journey-album")
        tryCompare(host, "page", "album")
        tryCompare(host.pageData, "shownAlbumId", chosen.id)
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
        compare(browser.currentType, "albums")
        compare(browser.currentCatalog.attribution, "JioSaavn")
        compare(browser.filters.Language, "telugu")
        compare(browser.filters.List, "telugu-decade")
        verify(item("discoverCard_journey-album") !== null)
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
        verify(host.worldData.artistOptions("").length>=5)
        verify(item("vinylPickerSkip")===null)
        for(var i=0;i<4;++i)click("vinylPickerArtist_"+i)
        compare(host.worldData.pickerDraft.length,4)
        compare(item("vinylPickerDone").enabled,false)
        verify(!host.worldData.pickerAnswered)
        tryCompare(item("vinylTastePicker"),"visible",true)
        shot("home-four-locked")
        click("vinylPickerArtist_4")
        compare(item("vinylPickerDone").enabled,true)
        click("vinylPickerDone")
        tryCompare(host.worldData,"pickerAnswered",true)
        tryCompare(item("vinylHomeRow_recommend-songs"),"visible",true)
        shot("home-unlocked")
        click("vinylDevResetPicker")
        tryCompare(item("vinylTastePicker"),"visible",true)
        host.worldScroll.contentY=world.contentTop
        waitForRendering(host.contentItem)
        compare(host.worldData.pickerDraft.length,0)
        for(var i=0;i<5;++i)click("vinylPickerArtist_"+i)
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
