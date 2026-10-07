import QtQuick
import QtTest
TestCase {
    name: "VinylDiscoverPage"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem,name) }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null); host.pageData.frozen=true; host.page="world"
        tryCompare(host,"ready",true)
    }
    function cleanup() { if(host){host.close();host.destroy()} }

    function test_orderAndSeeAll() {
        verify(host.worldData!==undefined)
        var world=item("vinylWorld");world.setTab("discover")
        compare(JSON.stringify(host.worldData.shelfKeys),JSON.stringify(["popular","trending","new","essentials","global"]))
        var page=item("vinylDiscoverPage");compare(page.shelfCount,5)
        host.worldScroll.contentY=world.contentTop+40
        var y=host.worldScroll.contentY
        host.worldData.openList("popular",false)
        var list=item("vinylListPage")
        tryCompare(list,"itemCount",host.worldData.listItems("popular").length)
        compare(list.title,"Popular")
        mouseClick(item("vinylListBack"))
        tryVerify(function(){return Math.abs(host.worldScroll.contentY-y)<4})
    }
    function test_lensAndIndex() {
        verify(host.worldData!==undefined)
        item("vinylWorld").setTab("discover")
        host.worldData.setProfile("seasoned")
        host.worldData.setLens("language","Telugu")
        compare(item("vinylLensTitle").text,"Telugu")
        host.worldData.setLens("decade","1990s")
        compare(item("vinylLensTitle").text,"Telugu · 1990s")
        var sidebar=item("vinylWorldRowIndexSidebar")
        tryVerify(function(){return sidebar.visibleRows.length===host.worldData.shelfKeys.length})
        verify(sidebar.automationRows.indexOf("New and notable")<0)
        verify(sidebar.automationRows.indexOf("Global")<0)
        sidebar.visibleRows.forEach(function(r){verify(!!r.iconSource)})
        host.worldData.setKind("artists")
        host.worldData.shelves.forEach(function(s){s.items.forEach(function(i){verify(i.round)})})
    }
    function test_disabledValueAndListRefresh() {
        verify(host.worldData!==undefined)
        item("vinylWorld").setTab("discover")
        host.worldData.openList("popular",false)
        host.worldData.setLens("language","Telugu")
        host.worldData.setLens("decade","1990s")
        var list=item("vinylListPage")
        tryCompare(list,"itemCount",host.worldData.listItems("popular").length)
        var button=item("vinylFilter_country")
        host.worldScroll.contentY=item("vinylWorld").contentTop
        mouseClick(button)
        var value=item("vinylFilterValue_country_United States")
        verify(value!==null); verify(!value.enabled)
        var before=JSON.stringify(host.worldData.lens)
        mouseClick(value)
        compare(JSON.stringify(host.worldData.lens),before)
    }
    function test_filterMenuInsideWindow() {
        verify(host.worldData!==undefined)
        var world=item("vinylWorld");world.setTab("discover")
        waitForRendering(host.contentItem)
        var barTop=item("vinylPlayerBar").mapToItem(host.contentItem,0,0).y
        var fold=host.worldScroll.y+host.worldScroll.height
        ;["genre","decade","country","language"].forEach(function(key) {
            ["genre","decade","country","language"].forEach(function(k){host.worldData.setLens(k,"")})
            var button=item("vinylFilter_"+key)
            verify(button!==null)
            mouseClick(button)
            var menu=findChild(host,"vinylFilterMenu")
            tryCompare(menu,"opened",true)
            var scroll=item("vinylFilterMenuScroll")
            verify(scroll!==null)
            tryVerify(function(){return scroll.contentHeight>0 && scroll.height>0})
            waitForRendering(host.contentItem)
            var menuTop=menu.contentItem.mapToItem(host.contentItem,0,0)
            var menuBottom=menuTop.y-menu.topPadding+menu.height
            verify(menuBottom<=fold,key+" menu bottom "+menuBottom+" must stay inside the visible page (fold "+fold+")")
            verify(menuBottom<=barTop,key+" menu must stay above the player bar (top "+barTop+")")
            var options=host.worldData.filterOptions(key)
            var value=options[options.length-1]
            verify(value.enabled,"last "+key+" value must be choosable under an empty lens")
            var btn=item("vinylFilterValue_"+key+"_"+value.value)
            verify(btn!==null)
            scroll.contentY=Math.max(0,btn.y+btn.height-scroll.height)
            waitForRendering(host.contentItem)
            var valueTop=btn.mapToItem(host.contentItem,0,0)
            verify(valueTop.y>=menuTop.y-1 && valueTop.y+btn.height<=menuTop.y+scroll.height+1,"last "+key+" value must be inside the visible menu")
            mouseClick(btn)
            tryCompare(menu,"opened",false)
            tryVerify(function(){return host.worldData.lens[key]===value.value})
        })
    }
    function test_windowGeometry_data() {
        return [{tag:"compact",w:1280,h:720},{tag:"laptop",w:1280,h:700},{tag:"wide",w:1600,h:900}]
    }
    function test_windowGeometry(data) {
        verify(host.worldData!==undefined)
        host.width=data.w;host.height=data.h
        waitForRendering(host.contentItem)
        if (host.width!==data.w || host.height!==data.h)
            skip("Screen granted "+host.width+"x"+host.height+" for "+data.w+"x"+data.h)
        var world=item("vinylWorld");world.setTab("discover")
        waitForRendering(host.contentItem)
        host.worldScroll.contentY=world.contentTop
        waitForRendering(host.contentItem)
        var card=item("vinylCard_popular_0")
        tryVerify(function(){return card && card.width>140})
        var p=card.mapToItem(host.contentItem,0,0)
        verify(p.x>=0 && p.x+card.width<=host.width)
        verify(p.y>=host.worldScroll.y && p.y+card.height<=host.worldScroll.y+host.worldScroll.height)
    }
    function test_wrappedLensDoesNotOverlap() {
        host.width=1280;host.height=720
        item("vinylWorld").setTab("discover")
        host.worldData.setLens("language","Telugu")
        host.worldData.setLens("decade","1990s")
        host.worldData.setLens("genre","Film soundtrack")
        host.worldData.setLens("country","India")
        waitForRendering(host.contentItem)
        var lens=item("vinylLens"),title=item("vinylLensTitle"),filter=item("vinylFilter_genre")
        var y=filter.mapToItem(lens,0,0).y
        verify(y>=title.y+title.height+10,"Wrapped title must clear filters")
    }
    function test_offlineRetains() {
        verify(host.worldData!==undefined)
        item("vinylWorld").setTab("discover")
        var keys=JSON.stringify(host.worldData.shelfKeys)
        host.worldData.setCatalogueReachable(false)
        compare(JSON.stringify(host.worldData.shelfKeys),keys)
        verify(!item("vinylFilter_language").enabled)
        verify(!item("vinylKind_artists").enabled)
    }
}
