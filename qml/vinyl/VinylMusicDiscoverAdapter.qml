import QtQuick

QtObject {
    id: root
    property var source: null
    property int profileGeneration: 0
    required property var browser
    property var pending: null
    property var pendingArt: null
    property bool catalogueKeyMissing: false
    readonly property string genreGroup: qsTr("Genre")
    readonly property string listGroup: qsTr("List")
    readonly property var genres: [
        {key:"pop",label:qsTr("Pop")}, {key:"rock",label:qsTr("Rock")},
        {key:"hip-hop",label:qsTr("Hip-hop")}, {key:"rnb",label:qsTr("R&B")},
        {key:"indie",label:qsTr("Indie")}, {key:"electronic",label:qsTr("Electronic")},
        {key:"dance",label:qsTr("Dance")}, {key:"metal",label:qsTr("Metal")},
        {key:"jazz",label:qsTr("Jazz")}, {key:"classical",label:qsTr("Classical")},
        {key:"soul",label:qsTr("Soul")}, {key:"folk",label:qsTr("Folk")},
        {key:"punk",label:qsTr("Punk")}, {key:"alternative",label:qsTr("Alternative")},
        {key:"k-pop",label:qsTr("K-pop")}, {key:"j-pop",label:qsTr("J-pop")},
        {key:"latin",label:qsTr("Latin")}, {key:"reggae",label:qsTr("Reggae")},
        {key:"blues",label:qsTr("Blues")}, {key:"country",label:qsTr("Country")},
        {key:"soundtrack",label:qsTr("Soundtrack")}, {key:"lo-fi",label:qsTr("Lo-fi")},
        {key:"ambient",label:qsTr("Ambient")}
    ]
    readonly property var lists: [
        {key:"chartWeek",label:qsTr("This week")}, {key:"popular",label:qsTr("Popular")},
        {key:"new",label:qsTr("New")}
    ]
    signal resetRequested()

    function types() {
        return [{key:"songs",label:qsTr("Songs")},{key:"albums",label:qsTr("Albums")},{key:"artists",label:qsTr("Artists")}];
    }

    function defaultCatalog(type) { return "vinyl"; }

    function catalogs(type) {
        return [{key:"vinyl",title:"Vinyl",sourceName:"Vinyl",sourceKind:"builtin"}];
    }

    function combinesFilters(type, catalogKey) { return true; }

    function headingByline(filters) {
        var list = lists.find(choice => choice.key === filters[listGroup]);
        return (list || lists[0]).label;
    }

    function filters(type, catalogKey) {
        return [{group:listGroup,options:lists},
                {group:genreGroup,options:[{key:"all",label:qsTr("All")}].concat(genres)}];
    }

    function resolvePin(pin) {
        var type=pin && pin.type || "songs";
        return {missing:false,type:type,catalogKey:defaultCatalog(type),filters:{}};
    }

    function cancelRequest(id) {
        var request=pending;
        if(!request || request.id!==id) return;
        pending=null;
        request.source.cancel(request.lane);
    }

    function cancelArt() {
        artPoll.stop();
        var request=pendingArt;
        if(!request) return;
        pendingArt=null;
        request.source.cancel("catalogue-art");
    }

    function reset() {
        if(pending) cancelRequest(pending.id);
        cancelArt();
        catalogueKeyMissing=false;
        resetRequested();
    }

    function fetchPage(state, cursor, generation, done) {
        cancelArt();
        catalogueKeyMissing=false;
        if(!source) {done(generation,{failed:true,retryable:true});return;}
        var kind=state.type;
        var selected=state.filters || {};
        var catalog=state.catalogKey;
        var list=selected[listGroup] || "chartWeek";
        var genre=selected[genreGroup] || "all";
        if(!cursor) browser.noticeText="";
        var lane="catalogue-"+kind;
        var id=source.request(lane,"browseCatalogue",kind,
                              {catalogueSource:catalog,list:list,genre:genre,cursor:cursor || {},pageSize:50});
        if(id<0) {done(generation,{failed:true,retryable:true});return;}
        pending={id:id,source:source,lane:lane,kind:kind,catalog:catalog,list:list,genre:genre,
                 generation:generation,cursor:cursor || {},done:done};
        return function(){root.cancelRequest(id);};
    }

    function accept(lane,id,data,error) {
        if(lane==="catalogue-art") {
            acceptArt(id,data,error);
            return;
        }
        var request=pending;
        if(!request || request.id!==id || request.lane!==lane) return;
        pending=null;
        try {data=JSON.parse(JSON.stringify(data));} catch(conversionError) {data=null;}
        if(error || !data || data.version!==1 || data.kind!==request.kind
            || data.source!==request.catalog || data.list!==request.list || data.genre!==request.genre
            || !Array.isArray(data.items)) {
            catalogueKeyMissing=error==="Vinyl's catalogue key is missing";
            request.done(request.generation,{failed:true,retryable:true});return;
        }
        var existingIds=Object.keys(request.cursor).length ? new Set(browser.items.map(item=>item.id)) : null;
        var incoming=data.items.filter(item=>item && item.id && item.catalogueCard===true
            && item.kind===request.kind);
        if(existingIds) {
            incoming=incoming.filter(function(item) {
                if(existingIds.has(item.id)) return false;
                existingIds.add(item.id);
                return true;
            });
        }
        var items=incoming.map(function(item) {
            return {id:item.id,type:request.kind,title:item.title,cover:item.cover || "",year:item.year || "",
                format:request.kind==="artists" ? qsTr("Artist") : request.kind==="albums" ? qsTr("Album") : qsTr("Song"),
                publisher:item.subtitle || item.artist || "",raw:item};
        });
        var cursor=data.nextCursor || {};
        var date=String(data.sourceDate || data.fetchedAt || "");
        request.done(request.generation,{items:items,nextCursor:Object.keys(cursor).length ? cursor : null,
            exhausted:data.complete!==false || !Object.keys(cursor).length || JSON.stringify(cursor)===JSON.stringify(request.cursor),
            freshness:date,warning:String(data.warning || "")});
        var notices=[];
        if(data.stale && date)
            notices.push(qsTr("As of %1").arg(date));
        if(request.genre!=="all" && data.genresPending===true)
            notices.push(qsTr("Genre results may be incomplete. Select the genre again to check for more."));
        browser.noticeText=notices.join(" · ");
        if(request.list==="chartWeek") {
            Qt.callLater(root.resumeWeekArt);
        } else if(request.list==="popular" && request.kind!=="albums") {
            var missing=items.filter(item=>!item.cover && item.raw.source==="lastfm").map(item=>item.raw);
            if(missing.length)
                Qt.callLater(function(){root.requestArt(request,missing);});
        }
    }

    // This week already queued its network work with genre tagging. This lane
    // only reads completed cache entries, including across transient helpers.
    function resumeWeekArt() {
        if(!source || !browser.active || browser.loading || pendingArt || artPoll.running
            || (browser.filters[listGroup] || "chartWeek")!=="chartWeek") return;
        var missing=browser.items.filter(item=>item.raw.artPending===true).map(item=>item.raw);
        if(missing.length)
            requestArt({source:source,generation:browser.fetchGen,week:true,deadline:Date.now()+90000},missing);
    }

    property Timer artPoll: Timer {
        interval: 1200
        property var request
        property var items
        onTriggered: root.requestArt(request,items)
    }

    function requestArt(request, items) {
        if(!items.length || !browser.active || browser.fetchGen!==request.generation || source!==request.source)
            return;
        var batch=items.slice(0,12);
        var id=source.request("catalogue-art","resolveCatalogueArt",request.week ? "kworb" : "lastfm",{items:batch});
        if(id>=0)
            pendingArt=Object.assign({},request,{id:id,batch:batch,remaining:items.slice(12)});
    }

    function acceptArt(id, data, error) {
        var request=pendingArt;
        if(!request || request.id!==id) return;
        pendingArt=null;
        if(error || !browser.active || browser.fetchGen!==request.generation || source!==request.source) return;
        try {data=JSON.parse(JSON.stringify(data));} catch(conversionError) {data=null;}
        if(!data || data.version!==1 || !data.covers || (request.week && !Array.isArray(data.settled))) return;
        // The gallery's model is indexed by row count. Only replace the card
        // payload at the same index so existing delegates retain focus and scroll.
        var settled=new Set(data.settled || []);
        if(Object.keys(data.covers).length || settled.size) {
            browser.items=browser.items.map(function(item) {
                var candidate=data.covers[item.id];
                var cover=item.cover || (typeof candidate==="string" ? candidate : "");
                var album=data.albums && data.albums[item.id];
                if(cover===item.cover && !settled.has(item.id)) return item;
                var raw=Object.assign({},item.raw,{cover:cover,artPending:false});
                if(typeof album==="string" && album) raw.hints=Object.assign({},raw.hints,{album:album});
                return Object.assign({},item,{cover:cover,raw:raw});
            });
        }
        if(request.week) {
            var retry=(request.retry || []).concat(request.batch.filter(item=>!settled.has(item.id)));
            if(request.remaining.length) {
                request.retry=retry;
                root.requestArt(request,request.remaining);
            } else if(retry.length && Date.now()<request.deadline) {
                request.retry=[];
                artPoll.request=request;
                artPoll.items=retry;
                artPoll.start();
            }
        } else if(request.remaining.length) {
            Qt.callLater(function(){root.requestArt(request,request.remaining);});
        }
    }

    onProfileGenerationChanged: reset()
    onSourceChanged: reset()
    Component.onDestruction: {
        if(pending) cancelRequest(pending.id);
        cancelArt();
    }

    property Connections transport: Connections {
        target: root.source
        ignoreUnknownSignals: true
        function onCompleted(lane,id,data,error) {root.accept(lane,id,data,error);}
        function onProfileLeaving() {root.reset();}
        function onShuttingDown() {root.reset();}
    }
}
