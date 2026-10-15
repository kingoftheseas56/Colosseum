import QtQuick
import "../qml" as App
import "../qml/UniverseContentApi.js" as Api
import "../qml/Universes.js" as Universes
Item {
    id: proof
    width: 1280; height: 720
    Rectangle { id: backdrop; anchors.fill: parent; color: "black" }
    App.UniversesWorld { id: world; anchors.fill: parent; backdrop: backdrop }
    function require(ok, message) { if (!ok) throw new Error(message) }
    function find(item, name) {
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i=0;i<children.length;++i) { var got=find(children[i],name); if(got)return got }
        return null
    }
    function banners() {
        var ledger=find(world,"universesLedger"),out=[]
        for(var i=0;i<ledger.children.length;++i) if(ledger.children[i].artRequested!==undefined)out.push(ledger.children[i])
        return out
    }
    function guarded(fn) { try { fn() } catch(e) { console.error("UNIVERSE_NATIVE_FAIL",e); Qt.exit(1) } }
    Component.onCompleted: Qt.callLater(function() { guarded(function() {
        require(Content.document("universes-roster").FACETS.length>0,"native roster facets required")
        Api.setAssetResolver(function(path){return Content.assetUrl(path)})
        Api.setReader(function(key){return Content.document(key)})
        Api.reload(Content.generation)
        Qt.callLater(function(){guarded(checkFacets)})
    }) })
    function checkFacets() {
        require(world.roster.length===20,"full API roster")
        require(world.facetTabs.map(function(t){return t.label}).join(",")==="All,Books,Manga,Anime,Films,Series","all six labeled filter pills")
        var expected={books:7,manga:5,anime:5,films:9,series:9}
        for(var key in expected) require(Universes.filterRoster(world.roster,key).length===expected[key],key+" canonical membership")
        require(Universes.holds({chips:[{t:"Graphic novels"}]},"comics"),"old cached RegExp fallback")
        world.facet="manga"
        Qt.callLater(function(){guarded(checkFilter)})
    }
    function checkFilter() {
        require(world.shown.map(function(e){return e.name}).join("|")==="Attack on Titan|Dragon Ball|Naruto|One Piece|Weekly Shonen Jump","filter changes actual page roster")
        world.facet=""
        Qt.callLater(function(){Qt.callLater(function(){guarded(checkViewport)})})
    }
    function checkViewport() {
        var bars=banners(),requested=bars.filter(function(b){return b.artRequested})
        require(bars.length===20,"all ledger delegates exist")
        require(requested.length>0&&requested.length<8,"only viewport and one banner margin request art: "+requested.length)
        require(!bars[19].artRequested,"last offscreen banner remains deferred")
        require(String(find(bars[19],"universeBannerArt").source)==="","offscreen art has empty source")
        world.pageFlickable.contentY=world.pageFlickable.contentHeight-world.pageFlickable.height
        Qt.callLater(function(){guarded(function(){
            var after=banners()
            require(after[19].artRequested,"scroll activates last banner art")
            require(String(find(after[19],"universeBannerArt").source).length>0,"scrolled art receives source")
            require(after[0].artRequested,"previously loaded art remains latched")
            console.log("UNIVERSE_NATIVE_PASS real QVariantList, labeled tabs, all facet memberships, actual filter and viewport art")
            Qt.quit()
        })})
    }
}
