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
    function reply(request,items,cursor,stale) {
        source.completed(request.lane,request.id,{version:1,kind:request.value,source:request.options.catalogueSource,
            list:request.options.list,genre:request.options.genre,items:items,nextCursor:cursor || {},complete:!cursor,
            fetchedAt:"2026-11-04",stale:!!stale},"");
    }
    function entry(id,provider,cover) {
        return {id:id,kind:"songs",catalogueCard:true,title:"Catalogue song",artist:"Artist",
            source:provider || "kworb",cover:cover===undefined ? "fixture.svg" : cover};
    }
    function pick(group,key) {
        var picker=findChild(browser,"discoverFilterPicker");verify(picker!==null);
        picker.picked(group+"|"+key);
    }
    function test_one_source_and_raw_activation() {
        compare(browser.posterVisualProfile,"gallery");compare(browser.currentType,"songs");
        verify(findChild(browser,"vinylDiscoverWall").reuseItems);
        compare(browser.sources.length,1,"Discover must have exactly one source");
        compare(browser.catalogMenuModel.filter(row=>row.key).length,1,"Discover must have exactly one catalogue");
        compare(browser.currentSource,"Vinyl");compare(browser.currentCatalog.title,"Vinyl");
        compare(last().operation,"browseCatalogue");compare(last().options.catalogueSource,"vinyl");
        compare(last().options.list,"chartWeek");compare(last().options.pageSize,50);
        reply(last(),[entry("one"),entry("two","youtube-music")]);compare(page.items.length,2);verify(browser.exhausted);
        compare(source.calls.length,1);compare(world.activated,null);
        browser.itemOpenRequested(page.items[0]);compare(world.activated.id,"one");verify(world.activated.catalogueCard);
        compare(world.activated.source,"kworb");
    }
    function test_widget_faces_and_caption_geometry() {
        var first=entry("known","kworb","");
        first.artist="Singer";
        first.hints={album:"Debut"};
        var second=entry("unknown","kworb","");
        second.artist="Other singer";
        reply(last(),[first,second]);
        tryVerify(function(){return findChild(browser,"discoverCard_known")!==null;});
        tryVerify(function(){
            var widget=findChild(browser,"discoverCard_known_widget");
            return widget!==null && widget instanceof Vinyl.VinylItemCard;
        },2000,"Discover must use the VinylItemCard widget face");
        var song=findChild(browser,"discoverCard_known_widget");
        compare(song.width, 196);
        tryCompare(song, "implicitHeight", 251);
        compare(song.height, 251);
        compare(findChild(song, song.objectName + "_captionTitle").text, first.title);
        compare(findChild(song, song.objectName + "_captionDetail").text, "Singer");
        var wall = findChild(browser, "vinylDiscoverWall");
        compare(wall.cellHeight, song.height + wall.gapY);
        compare(findChild(song,"vinylArtworkImage").status,Image.Null);
        verify(findChild(song,"vinylArtworkPlaceholder").visible);
        tryVerify(function(){return findChild(browser,"discoverCard_unknown_widget")!==null;});
        var other=findChild(browser,"discoverCard_unknown_widget");
        compare(findChild(other, other.objectName + "_captionDetail").text, "Other singer");
        mouseClick(song,song.width/2,song.width/2);
        tryVerify(function(){return world.activated && world.activated.id==="known";});
        keyClick(Qt.Key_Right);
        tryCompare(other, "keyboardFocused", true);
        compare(findChild(other, other.objectName + "_artRing").border.color, "#f0c44a");
        keyClick(Qt.Key_Return);
        tryVerify(function(){return world.activated && world.activated.id==="unknown";});

        browser.selectType("albums");
        var album=entry("album","kworb","");
        album.kind="albums";album.artist="Band";
        reply(last(),[album]);
        tryVerify(function(){return findChild(browser,"discoverCard_album_widget")!==null;});
        var jewel=findChild(browser,"discoverCard_album_widget");
        verify(jewel!==null && jewel instanceof Vinyl.VinylItemCard);
        compare(jewel.captionDetail,"Band");
        compare(wall.cellHeight, jewel.width + 55 + wall.gapY);
        compare(findChild(jewel,"vinylArtworkImage").status, Image.Null);
        verify(findChild(jewel,"vinylArtworkPlaceholder").visible);

        browser.selectType("artists");
        var artist=entry("artist","kworb","");
        artist.kind="artists";artist.title="Band Name";artist.artist=artist.title;
        reply(last(),[artist]);
        tryVerify(function(){return findChild(browser,"discoverCard_artist_widget")!==null;});
        var record=findChild(browser,"discoverCard_artist_widget");
        verify(record!==null && record instanceof Vinyl.VinylItemCard);
        compare(record.captionDetail,"Artist");
        var fallback=findChild(record,"vinylArtworkFallback");
        verify(fallback.visible);
        compare(fallback.border.width,0);
        compare(fallback.radius,fallback.width/2);
        var initials=findChild(record,"vinylArtistInitials");
        verify(initials.visible);compare(initials.text,"BN");
    }
    function test_list_and_genre_picker_without_language() {
        var byline=findChild(browser,"vinylDiscoverHeadingProvider");
        compare(byline.text,"This week");
        compare(browser.filterGroups.map(group=>group.group),["List","Genre"]);
        compare(browser.filterGroups[0].options.map(choice=>choice.label),["This week","Popular","New"]);
        compare(browser.filterGroups[1].options.length,24);
        verify(browser.filterMenuModel.some(row=>row.key==="Genre|all" && row.text==="All"));
        verify(!browser.filterMenuModel.some(row=>row.header==="Language"));
        pick("Genre","metal");compare(last().options.list,"chartWeek");compare(last().options.genre,"metal");
        compare(browser.filters.Genre,"metal");
        pick("List","popular");compare(last().options.list,"popular");compare(last().options.genre,"metal");
        compare(byline.text,"Popular");
        pick("List","new");compare(last().options.list,"new");compare(last().options.genre,"metal");
        compare(byline.text,"New");
        pick("Genre","rock");compare(last().options.list,"new");compare(last().options.genre,"rock");
        compare(browser.filters.List,"new");compare(browser.filters.Genre,"rock");
        browser.selectType("albums");compare(last().options.list,"chartWeek");
        browser.selectType("songs");compare(last().options.list,"new");compare(last().options.genre,"rock");
        pick("Genre","all");compare(last().options.list,"new");compare(last().options.genre,"all");
    }
    function test_missing_key_is_plain_retry_and_does_not_block_other_lists() {
        for (var list of ["chartWeek", "new", "popular"]) {
            pick("List",list);
            if (list!=="popular") {
                reply(last(),[entry(list)]);
                compare(page.items.length,1);verify(!browser.pageFailed);verify(!browser.bannerVisible);
                pick("Genre","metal");
            }
            var request=last();
            source.completed(request.lane,request.id,null,"Vinyl's catalogue key is missing");
            verify(browser.pageFailed);verify(browser.pageRetryable);compare(browser.bannerText,"Try again.");
            browser.retryPage();verify(last().id>request.id);compare(last().options.list,list);
            pick("Genre","all");
        }
    }
    function test_uncached_genre_page_explains_partial_results() {
        pick("Genre","metal");
        var request=last();
        source.completed(request.lane,request.id,{version:1,kind:"songs",source:"vinyl",list:"chartWeek",
            genre:"metal",items:[],complete:true,genresPending:true},"");
        verify(!browser.pageFailed);
        verify(browser.bannerText.indexOf("Genre results may be incomplete")>=0);
        pick("Genre","metal");reply(last(),[entry("metal")]);
        compare(page.items.length,1);verify(!browser.bannerVisible);
    }
    function test_art_updates_same_cards_and_stale_notice() {
        pick("List","popular");
        var items=[];
        for(var i=0;i<40;i++) items.push(entry("song-"+i,"lastfm",""));
        reply(last(),items,null,true);
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
    function test_week_art_arrives_without_replacing_the_widget() {
        var song=entry("week","kworb","");
        song.artist="Singer";song.artPending=true;
        reply(last(),[song]);
        compare(last().operation,"browseCatalogue");
        verify(!browser.loading);
        tryVerify(function(){return findChild(browser,"discoverCard_week_widget")!==null;});
        var widget=findChild(browser,"discoverCard_week_widget");
        compare(widget.captionTitle, song.title);
        compare(widget.captionDetail, "Singer");
        var artwork=findChild(widget,"vinylArtworkImage");
        verify(artwork!==null);
        compare(page.items[0].cover,"");
        compare(artwork.status,Image.Null);
        tryVerify(function(){return last().operation==="resolveCatalogueArt";});
        var call=last();compare(call.value,"kworb");
        source.completed(call.lane,call.id,{version:1,covers:{},settled:[]},"");
        tryVerify(function(){return last().id>call.id;});
        call=last();
        var cover=String(Qt.resolvedUrl("../../qml/vinyl/assets/record-ring.svg"));
        source.completed(call.lane,call.id,{version:1,covers:{week:cover},albums:{week:"Debut"},settled:["week"]},"");
        compare(page.items[0].cover,cover);
        compare(findChild(browser,"discoverCard_week_widget"),widget);
        compare(widget.captionTitle, song.title);
        compare(widget.implicitHeight, widget.width + 55);
        tryCompare(artwork,"status",Image.Ready);
    }
    function test_next_page_cancels_art() {
        pick("List","popular");
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
        pick("List","popular");
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
        pick("List","popular");
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
