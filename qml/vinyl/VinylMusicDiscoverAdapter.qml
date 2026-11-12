import QtQuick

QtObject {
    id: root
    property var source: null
    property int profileGeneration: 0
    required property var browser
    property var pending: null
    property var pendingArt: null
    property var chartOptions: ({})
    readonly property string genreGroup: qsTr("Genre")
    readonly property string languageGroup: qsTr("Language")
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
    readonly property var languages: [
        {key:"tamil",label:qsTr("Tamil")}, {key:"telugu",label:qsTr("Telugu")},
        {key:"malayalam",label:qsTr("Malayalam")}, {key:"kannada",label:qsTr("Kannada")},
        {key:"hindi",label:qsTr("Hindi")}, {key:"punjabi",label:qsTr("Punjabi")},
        {key:"bengali",label:qsTr("Bengali")}, {key:"marathi",label:qsTr("Marathi")}
    ]
    signal resetRequested()

    function types() {
        return [{key:"songs",label:qsTr("Songs")},{key:"albums",label:qsTr("Albums")},{key:"artists",label:qsTr("Artists")}];
    }

    function defaultCatalog(type) { return "lastfm"; }

    function catalogs(type) {
        return [
            {key:"lastfm",title:"Last.fm",sourceKind:"builtin",section:qsTr("Worldwide"),attribution:"Last.fm"},
            {key:"jiosaavn",title:"JioSaavn",sourceKind:"builtin",section:qsTr("Indian"),attribution:"JioSaavn"}
        ];
    }

    function combinesFilters(type, catalogKey) { return catalogKey==="jiosaavn"; }

    function listOptions(type, language) {
        var choices=[{key:"default",label:qsTr("Top hits and editorial")}];
        if(type==="albums")
            choices.unshift({key:"new",label:qsTr("New releases")});
        choices.push({key:"editorial",label:qsTr("Editorial")});
        return choices.concat(chartOptions[type+":"+language] || []);
    }

    function filters(type, catalogKey) {
        if(catalogKey==="jiosaavn") {
            return [{group:languageGroup,options:languages},
                    {group:listGroup,options:listOptions(type,browser.filters[languageGroup] || "tamil")}];
        }
        var choices=type==="albums" ? genres : [{key:"chart",label:qsTr("Top worldwide")}].concat(genres);
        return [{group:genreGroup,options:choices}];
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
        var request=pendingArt;
        if(!request) return;
        pendingArt=null;
        request.source.cancel("catalogue-art");
    }

    function reset() {
        if(pending) cancelRequest(pending.id);
        cancelArt();
        chartOptions=({});
        resetRequested();
    }

    function fetchPage(state, cursor, generation, done) {
        cancelArt();
        if(!source) {done(generation,{failed:true,retryable:true});return;}
        var kind=state.type;
        var selected=state.filters || {};
        var catalog=state.catalogKey;
        var list=selected[genreGroup] || (kind==="albums" ? "pop" : "chart");
        var language="";
        if(catalog==="jiosaavn") {
            language=selected[languageGroup] || "tamil";
            var listKey=selected[listGroup] || "default";
            if(!listOptions(kind,language).some(choice=>choice.key===listKey))
                listKey="default";
            list=language+":"+listKey;
        }
        if(!cursor) {
            browser.noticeText="";
            if(language) {
                // Keep the language and effective list visible. Last.fm
                // defaults stay in the request until a Genre is chosen.
                var effective={};
                effective[languageGroup]=language;
                effective[listGroup]=listKey;
                if(JSON.stringify(browser.filters)!==JSON.stringify(effective))
                    browser.filters=effective;
            }
        }
        var lane="catalogue-"+kind;
        var id=source.request(lane,"browseCatalogue",kind,
                              {catalogueSource:catalog,list:list,cursor:cursor || {},pageSize:50});
        if(id<0) {done(generation,{failed:true,retryable:true});return;}
        pending={id:id,source:source,lane:lane,kind:kind,catalog:catalog,list:list,language:language,
                 generation:generation,cursor:cursor || {},done:done};
        return function(){root.cancelRequest(id);};
    }

    function updateChartOptions(request, options) {
        if(!request.language || !Array.isArray(options)) return;
        var choices=options.filter(option=>option && typeof option.key==="string"
            && option.key.length && typeof option.label==="string" && option.label.length
            && !["default","new","editorial"].includes(option.key));
        var next=Object.assign({},chartOptions);
        next[request.kind+":"+request.language]=choices;
        chartOptions=next;
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
            || data.source!==request.catalog || data.list!==request.list || !Array.isArray(data.items)) {
            request.done(request.generation,{failed:true,retryable:true});return;
        }
        updateChartOptions(request,data.listOptions);
        var existingIds=Object.keys(request.cursor).length ? new Set(browser.items.map(item=>item.id)) : null;
        var incoming=data.items.filter(item=>item && item.id && item.catalogueCard===true
            && item.kind===request.kind && item.source===request.catalog);
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
        if(data.stale && date)
            browser.noticeText=qsTr("As of %1").arg(date);
        if(request.catalog==="lastfm" && request.kind!=="albums") {
            var missing=items.filter(item=>!item.cover).map(item=>item.raw);
            if(missing.length)
                Qt.callLater(function(){root.requestArt(request,missing);});
        }
    }

    function requestArt(request, items) {
        if(!items.length || !browser.active || browser.fetchGen!==request.generation || source!==request.source)
            return;
        var id=source.request("catalogue-art","resolveCatalogueArt","lastfm",{items:items.slice(0,12)});
        if(id>=0)
            pendingArt={id:id,source:source,generation:request.generation,remaining:items.slice(12)};
    }

    function acceptArt(id, data, error) {
        var request=pendingArt;
        if(!request || request.id!==id) return;
        pendingArt=null;
        if(error || !data || data.version!==1 || !data.covers || !browser.active
            || browser.fetchGen!==request.generation || source!==request.source) return;
        // The gallery's model is indexed by row count. Only replace the card
        // payload at the same index so existing delegates retain focus and scroll.
        browser.items=browser.items.map(function(item) {
            var cover=data.covers[item.id];
            return !item.cover && typeof cover==="string" && cover.length ? Object.assign({},item,{cover:cover}) : item;
        });
        if(request.remaining.length)
            Qt.callLater(function(){root.requestArt(request,request.remaining);});
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
