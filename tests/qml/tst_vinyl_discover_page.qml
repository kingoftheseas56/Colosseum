import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylMusicDiscover"
    width: 1280; height: 800
    visible: true
    when: windowShown
    QtObject {
        id: source
        property int serial: 0
        property var calls: []
        property var cancelled: []
        signal completed(string lane, int id, var data, string error)
        signal profileLeaving()
        signal shuttingDown()
        function request(lane,operation,value,options) {
            var id=++serial;calls=calls.concat([{id:id,lane:lane,operation:operation,value:value,options:options}]);return id;
        }
        function cancel(lane) {cancelled=cancelled.concat([lane]);}
    }
    QtObject {
        id: session
        property var source: source
        property bool profileActive: true
        property bool shuttingDown: false
        property int profileGeneration: 1
    }
    QtObject {
        id: world
        property var session: session
        property var activated: null
        function activate(item) {activated=item;}
    }
    Item {id: wallpaper}
    Flickable {id: flick;width:1200;height:700;contentWidth:width;contentHeight:page ? page.implicitHeight : 0}
    Component {
        id: factory
        Vinyl.VinylDiscoverPage {width:1200;worldData:world;backdrop:wallpaper;pageFlick:flick;active:false}
    }
    property var page
    property var browser
    function init() {
        source.calls=[];source.cancelled=[];world.activated=null;
        page=createTemporaryObject(factory,test);verify(page!==null);
        browser=findChild(page,"vinylDiscoverBrowser");verify(browser!==null);
        page.active=true;
        tryVerify(function(){return source.calls.length>0;});
    }
    function last() {return source.calls[source.calls.length-1];}
    function reply(request,items,cursor,options,stale) {
        source.completed(request.lane,request.id,{version:1,kind:request.value,source:request.options.catalogueSource,
            list:request.options.list,items:items,nextCursor:cursor || {},complete:!cursor,
            fetchedAt:"2026-11-04",stale:!!stale,listOptions:options},"");
    }
    function entry(id,provider,cover) {
        return {id:id,kind:"songs",catalogueCard:true,title:"Catalogue song",artist:"Artist",
            source:provider || "lastfm",cover:cover===undefined ? "fixture.svg" : cover};
    }
    function test_shared_shell_defaults_and_raw_activation() {
        compare(browser.posterVisualProfile,"gallery");compare(browser.currentType,"songs");
        compare(browser.currentSource,"Worldwide");compare(browser.currentCatalog.attribution,"Last.fm");
        compare(last().operation,"browseCatalogue");compare(last().options.catalogueSource,"lastfm");
        compare(last().options.list,"chart");compare(last().options.pageSize,50);
        reply(last(),[entry("one")]);compare(page.items.length,1);verify(browser.exhausted);
        compare(source.calls.length,1);compare(world.activated,null);
        browser.itemOpenRequested(page.items[0]);compare(world.activated.id,"one");verify(world.activated.catalogueCard);
        browser.setFilter(browser.filterGroups[0].group,"rock");compare(last().options.list,"rock");
        browser.selectType("albums");compare(last().options.list,"pop");
        verify(!browser.filterGroups[0].options.some(choice=>choice.key==="chart"));
        browser.selectType("artists");compare(last().options.list,"chart");
        browser.selectType("songs");compare(last().options.list,"rock");
    }
    function test_indian_combined_picker() {
        browser.selectSource("Indian");
        compare(browser.currentCatalog.attribution,"JioSaavn");
        compare(last().options.list,"tamil:default");
        compare(browser.filterGroups[0].options.length,8);
        var picker=findChild(browser,"discoverFilterPicker");verify(picker!==null);
        var language=browser.filterGroups[0].group;
        var list=browser.filterGroups[1].group;
        picker.picked(language+"|telugu");
        compare(last().options.list,"telugu:default");
        reply(last(),[entry("telugu","jiosaavn")],null,[{key:"telugu-chart",label:"Telugu decade"}]);
        verify(browser.filterMenuModel.some(row=>row.key===list+"|telugu-chart"));
        var count=source.calls.length;
        picker.picked(list+"|telugu-chart");
        compare(source.calls.length,count+1);
        compare(last().options.list,"telugu:telugu-chart");
        compare(browser.filters[language],"telugu");
        count=source.calls.length;
        picker.picked(language+"|malayalam");
        compare(source.calls.length,count+1);
        compare(last().options.list,"malayalam:default");
        compare(browser.filters[list],"default");
        verify(browser.activeFilterLabel.indexOf("Top hits and editorial")>=0);
        browser.selectType("albums");browser.selectSource("Indian");
        compare(browser.filterGroups[1].options[0].key,"new");
        picker.picked(list+"|new");compare(last().options.list,"tamil:new");
    }
    function test_art_updates_same_cards_and_stale_notice() {
        var items=[];
        for(var i=0;i<40;i++) items.push(entry("song-"+i,"lastfm",""));
        reply(last(),items,null,null,true);
        compare(page.items.length,40);compare(last().operation,"browseCatalogue");
        verify(browser.bannerText.indexOf("As of 2026-11-04")>=0);
        tryVerify(function(){return last().operation==="resolveCatalogueArt";});
        var call=last();compare(call.options.items.length,12);
        flick.contentY=180;
        var y=flick.contentY;
        source.completed(call.lane,call.id,{version:1,covers:{"song-0":"cover.svg"}},"");
        compare(page.items.length,40);compare(page.items[0].id,"song-0");
        compare(page.items[0].cover,"cover.svg");compare(flick.contentY,y);
        tryVerify(function(){return last().id>call.id;});
        compare(last().options.items[0].id,"song-12");
        compare(last().options.items.length,12);
    }
    function test_next_page_cancels_art() {
        reply(last(),[entry("one","lastfm","")],{offset:12});
        tryVerify(function(){return last().operation==="resolveCatalogueArt";});
        var art=last();
        browser.requestPage();compare(last().operation,"browseCatalogue");
        verify(source.cancelled.indexOf("catalogue-art")>=0);
        source.completed(art.lane,art.id,{version:1,covers:{one:"stale.svg"}},"");
        compare(page.items[0].cover,"");
    }
    function test_finite_pagination_and_inactive_stale_completion() {
        var first=last();reply(first,[entry("one")],{offset:12});
        page.active=false;var count=source.calls.length;page.active=true;compare(source.calls.length,count);
        browser.requestPage();var second=last();compare(second.options.cursor.offset,12);
        page.active=false;verify(source.cancelled.indexOf(second.lane)>=0);
        reply(second,[entry("stale")]);compare(page.items.length,1);
        page.active=true;tryCompare(source,"serial",second.id+1);
        reply(last(),[entry("two")]);compare(page.items.length,2);verify(browser.exhausted);
        count=source.calls.length;browser.requestPage();compare(source.calls.length,count);
    }
    function test_failed_page_retry_and_nonprogressing_cursor() {
        var request=last();
        source.completed(request.lane,request.id,null,"Network unavailable");
        verify(browser.pageFailed);compare(page.items.length,0);
        browser.retryPage();verify(last().id>request.id);
        reply(last(),[entry("one")],{offset:12});
        browser.requestPage();reply(last(),[entry("two")],{offset:12});
        verify(browser.exhausted);
    }
    function test_duplicate_continuation_can_advance_cursor() {
        reply(last(),[entry("one")],{offset:12});
        browser.requestPage();compare(last().options.cursor.offset,12);
        reply(last(),[entry("one")],{offset:24});
        compare(page.items.length,1);verify(!browser.exhausted);
        browser.requestPage();compare(last().options.cursor.offset,24);
        reply(last(),[entry("one"),entry("later-unique"),entry("later-unique")]);
        compare(page.items.length,2);compare(page.items[1].id,"later-unique");verify(browser.exhausted);
    }
    function test_missing_album_art_does_not_schedule_lookup() {
        browser.selectType("albums");
        var call=last();
        var album=entry("album","lastfm","");
        album.kind="albums";
        reply(call,[album]);
        compare(page.items.length,1);
        wait(0);
        compare(last().id,call.id);
    }
    function test_source_replacement_cancels_only_owned_request() {
        var request=last();
        session.source=null;
        verify(source.cancelled.indexOf(request.lane)>=0);
        reply(request,[entry("stale-source")]);compare(page.items.length,0);
        session.source=source;wait(0);
        verify(last().id>request.id);
    }
    function test_profile_change_clears_retained_chart() {
        reply(last(),[entry("old-profile")]);compare(page.items.length,1);
        page.active=false;session.profileGeneration++;
        compare(page.items.length,0);
        page.active=true;tryVerify(function(){return last().id>1;});
    }
}
