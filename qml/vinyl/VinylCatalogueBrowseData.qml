import QtQuick

QtObject {
    id: root
    property var source: null
    property bool enabled: false
    property bool active: false
    property string activeKind: ""
    property int profileGeneration: 0
    property var pages: initialPages()
    property var records: []
    property var categories: []
    property var markets: [{code:"ZZ",title:qsTr("Global")}]
    property bool categoriesLoaded: false
    property bool categoriesLoading: false
    property string categoriesError: ""
    property int categoryRequest: -1
    function blank() {return {category:"",region:"ZZ",items:[],title:"",loaded:false,loading:false,error:"",cursor:{},complete:true,request:-1,append:false,scrollY:0,selectedId:"",currentIndex:0};}
    function initialPages() {return {songs:blank(),albums:blank(),artists:blank()};}
    function page(kind) {return pages[kind] || blank();}
    function update(kind, changes) {
        var next=Object.assign({},pages);next[kind]=Object.assign({},page(kind),changes);pages=next;
    }
    function cancel(kind) {
        if(source && typeof source.cancel==="function")source.cancel("catalogue-"+kind);
        update(kind,{request:-1,loading:false});
    }
    function cancelAll() {
        ["songs","albums","artists"].forEach(root.cancel);
        if(source && typeof source.cancel==="function")source.cancel("catalogue-categories");
        categoryRequest=-1;categoriesLoading=false;
    }
    function reset() {
        cancelAll();pages=initialPages();records=[];categories=[];categoriesLoaded=false;categoriesError="";markets=[{code:"ZZ",title:qsTr("Global")}];
    }
    function ensure() {
        if(!enabled || !active || !source || !pages[activeKind])return;
        if(activeKind!=="artists" && !categoriesLoaded && !categoriesLoading && !categoriesError)requestCategories(false);
        var state=page(activeKind);
        if(!state.loaded && !state.loading && !state.error)fetch(activeKind,false,false);
    }
    function requestCategories(refresh) {
        if(!enabled || !active || !source)return;
        categoriesLoading=true;categoriesError="";
        categoryRequest=source.request("catalogue-categories","browseCategories","",{refresh:!!refresh});
        if(categoryRequest<0){categoriesLoading=false;categoriesError=qsTr("Collections could not load. Retry.");}
    }
    function fetch(kind, append, refresh) {
        if(!enabled || !active || !source || !pages[kind])return;
        var state=page(kind);
        if(state.loading || (append && (state.complete || !Object.keys(state.cursor).length)))return;
        var request=source.request("catalogue-"+kind,"browseCatalogue",kind,
            {category:kind==="artists" ? "" : state.category,region:kind==="artists" ? state.region : "",cursor:append ? state.cursor : {},refresh:!!refresh});
        update(kind,{loading:request>=0,error:request<0 ? qsTr("Music could not load. Retry.") : "",request:request,append:!!append});
    }
    function select(kind, category, region) {
        if(!pages[kind])return;
        category=kind==="artists" ? "" : String(category || "");
        region=kind==="artists" ? String(region || "ZZ").toUpperCase() : "ZZ";
        if(kind==="artists" && !/^[A-Z]{2}$/.test(region))return;
        var state=page(kind);
        if(state.category===category && state.region===region)return;
        cancel(kind);
        update(kind,Object.assign(blank(),{category:category,region:region}));
        if(activeKind===kind)ensure();
    }
    function retry(kind) {var state=page(kind);fetch(kind,state.append && state.items.length>0,true);}
    function accept(lane,id,data,error) {
        if(!enabled || id<=0)return;
        var isCategories=lane==="catalogue-categories";
        var kind=isCategories ? "" : String(lane).replace(/^catalogue-/,"");
        if(isCategories) {if(id!==categoryRequest)return;}
        else if(["catalogue-songs","catalogue-albums","catalogue-artists"].indexOf(lane)<0 || id!==page(kind).request)return;
        try {data=JSON.parse(JSON.stringify(data));} catch(conversionError) {data=null;}
        if(isCategories) {
            categoryRequest=-1;categoriesLoading=false;
            if(error){categoriesError=error;return;}
            if(!data || !Array.isArray(data.categories) || data.categories.some(function(c){return !c || typeof c.params!=="string" || typeof c.title!=="string";})) {
                categoriesError=qsTr("Collections could not load. Retry.");return;
            }
            categories=data.categories;categoriesLoaded=true;categoriesError="";return;
        }
        var state=page(kind);
        update(kind,{request:-1,loading:false});
        if(error){update(kind,{error:error});return;}
        if(!data || data.kind!==kind || String(data.category || "")!==(kind==="artists" ? "" : state.category)
            || (kind==="artists" && String(data.region || "")!==state.region)) {
            update(kind,{error:qsTr("The catalogue returned a different selection. Retry.")});return;
        }
        if(!Array.isArray(data.items) || !Array.isArray(data.records)
            || (data.markets!==undefined && !Array.isArray(data.markets))
            || data.items.some(function(item){return !item || typeof item.id!=="string" || !item.id;})
            || data.records.some(function(record){return !record || typeof record.id!=="string" || !record.id;})
            || (data.nextCursor!==undefined && (!data.nextCursor || typeof data.nextCursor!=="object" || Array.isArray(data.nextCursor)))) {
            update(kind,{error:qsTr("Music could not load. Retry.")});return;
        }
        var items=state.append ? state.items.slice() : [],seen={};
        items.forEach(function(item){seen[String(item.id)]=true;});
        ((data && data.items) || []).forEach(function(item){var key=String(item.id || "");if(key && !seen[key]){seen[key]=true;items.push(item);}});
        var all=records.slice(),known={};all.forEach(function(record,index){known[record.id]=index;});
        (data.records || []).forEach(function(record){if(known[record.id]!==undefined)all[known[record.id]]=record;else{known[record.id]=all.length;all.push(record);}});records=all;
        if(kind==="artists" && data.markets && data.markets.length)markets=data.markets.filter(function(m){return m && /^[A-Z]{2}$/.test(String(m.code || ""));});
        var cursor=data.nextCursor || {};
        var progressing=!state.append || items.length>state.items.length;
        update(kind,{items:items,title:String(data.title || ""),loaded:true,error:"",cursor:cursor,
            complete:data.complete!==false || !Object.keys(cursor).length || !progressing,append:false});
    }
    onActiveKindChanged: ensure()
    onActiveChanged: {if(active)ensure();else cancelAll();}
    onEnabledChanged: {if(enabled)ensure();else reset();}
    onProfileGenerationChanged: {reset();ensure();}
    onSourceChanged: {reset();ensure();}
    property Connections transport: Connections {
        target:root.source
        ignoreUnknownSignals:true
        function onCompleted(lane,id,data,error){root.accept(lane,id,data,error);}
        function onProfileLeaving(){root.reset();}
        function onShuttingDown(){root.reset();}
    }
}
