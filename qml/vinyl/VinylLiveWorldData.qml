import QtQuick
import "VinylWorldLists.js" as Lists

// Presentation only. Collection records and the music session remain authoritative.
// Taste is owned by the active profile preferences.
Item {
    id: root
    objectName: "vinylLiveWorldData"
    required property var session
    property string profile: "local"
    property var preferencesStore: typeof ProfilePreferences !== "undefined" ? ProfilePreferences : null
    property var searchHistoryStore: typeof SearchHistory !== "undefined" ? SearchHistory : null
    property string personalError: ""
    readonly property string persistenceError: personalError || (preferencesStore ? preferencesStore.vinylPersistenceError || "" : "")
    readonly property var recentSearches: {
        var revision = searchHistoryStore ? searchHistoryStore.revision : 0;
        return searchHistoryStore ? JSON.parse(JSON.stringify(searchHistoryStore.list("Vinyl"))) : [];
    }
    function rememberSearch() { if(searchHistoryStore) searchHistoryStore.record("Vinyl",query); }
    function loadTaste() {
        personalError = "";
        pickerAnswered = preferencesStore ? !!preferencesStore.vinylTasteAnswered : false;
        picks = preferencesStore ? JSON.parse(JSON.stringify(preferencesStore.vinylTasteArtistIds || [])) : [];
        pickerDraft = picks.slice(); pickerQuery = "";
    }
    onPreferencesStoreChanged: loadTaste()
    Component.onCompleted: loadTaste()
    Connections { target: root.preferencesStore; ignoreUnknownSignals:true; function onVinylTasteChanged() { root.loadTaste(); } }

    readonly property bool catalogueReachable: true
    readonly property var catalogue: ({albums: session.library.map(function(entry) {
        var a = entry.payload && entry.payload.album ? entry.payload.album : entry;
        return Object.assign({}, a, {artists:a.artists || [], tracks:(a.tracks || []).map(function(t) {
            return Object.assign({},t,{artists:t.artists || a.artists || []});
        })});
    }), people:{}, profiles:{}, homeRegion:[]})
    property var lens: ({kind:"albums",genre:"",decade:"",country:"",language:""})
    property string query: ""
    function refreshSearch() {
        if(!session.pagedSearchSupported) return;
        if(query.trim()) session.search(query);
        else if(session.browseOperation==="search") session.cancelBrowse();
    }
    onQueryChanged: refreshSearch()
    property string sortOrder: "title"
    property bool pickerAnswered: false
    property var picks: []
    property var pickerDraft: []
    property string pickerQuery: ""
    readonly property string tasteNotice: "Taste choices are saved to this profile."
    readonly property string lensTitle: [lens.language,lens.decade,lens.genre,lens.country].filter(function(v){return !!v}).join(" · ") || "Local music"
    readonly property var albums: catalogue.albums.filter(function(a) {
        // Keep the existing local view while a replacement page chain loads; never
        // expose a partial page as a complete result or alter lens/sort semantics.
        var complete=session.pagedSearchSupported && session.searchComplete && session.searchQuery===query.trim();
        var included=!complete || session.results.some(function(record){return record.id===a.id;});
        return included && Lists.matches(a,lens) && (!query.trim() || (a.title+" "+a.artists.join(" ")+" "+(a.tracks || []).map(function(t){return t.title}).join(" ")).toLowerCase().indexOf(query.trim().toLowerCase())>=0);
    }).sort(function(a,b) {
        return sortOrder === "year" ? (Number(b.year)||0)-(Number(a.year)||0) || String(a.title).localeCompare(String(b.title)) : String(a.title).localeCompare(String(b.title));
    })
    readonly property var shelves: [{key:"local-"+lens.kind,title:"Local "+lens.kind,icon:"threads",kind:lens.kind,items:project(albums,lens.kind)}]
    readonly property var homeRows: {
        var preferred=albums.filter(function(a){return picks.some(function(name){return (a.artistIds || []).indexOf(name)>=0 || (!(a.artistIds || []).length && a.artists.indexOf(name)>=0)})});
        var result=[];
        if(preferred.length) result.push({key:"picked-artists",title:"From your selected artists",kind:"albums",items:project(preferred,"albums")});
        if(albums.length) result.push({key:"local-albums",title:"Your local albums",kind:"albums",items:project(albums,"albums")});
        return result;
    }
    readonly property var coldShelves: []
    readonly property var shelfKeys: shelves.map(function(r){return r.key})
    readonly property var carouselSlides: {
        var result=[];
        if(session.playingIndex>=0) result.push({key:"resume",title:session.playingTrack.title,blurb:session.playingEdition.artist,art:session.playingEdition.cover || "",artKind:"poster",cta:"Now playing",kicker:"Your music session"});
        if(albums.length) {var a=albums[0];result.push({key:"local-albums",title:a.title,blurb:a.artists.join(" · "),art:a.cover || "",artKind:"poster",cta:"Browse library",kicker:"On this device"});}
        return result;
    }
    signal itemRequested(var item)
    signal listRequested(string key, bool home)
    signal resumeRequested()
    function artwork(url) { return url || ""; }
    function project(records,kind) {
        if(kind==="artists") {
            var seen={},result=[];
            records.forEach(function(a){a.artists.forEach(function(name,index){
                var id=(a.artistIds || [])[index] || name;
                if(seen[id])return;seen[id]=true;
                result.push({id:id,artistId:(a.artistIds || [])[index] || "",albumId:a.id,kind:"artists",title:name,artist:name,subtitle:"",cover:"",aspect:1,round:true,reason:""});
            });});
            return result;
        }
        // Reuse approved projection semantics, then remove fixture-only display assumptions.
        return Lists.project(catalogue,records,kind).map(function(item) {
            var album=records.find(function(a){return a.id===item.albumId});
            item.subtitle=kind==="albums" ? [item.artist,album.year || ""].filter(function(v){return !!v}).join(" · ") : item.subtitle;
            if(kind==="songs") {var t=album.tracks[item.trackIndex];item.id=album.id+"|"+((album.editions || [])[0] || {}).id+"|"+(t.disc || 1)+"|"+(t.number || item.trackIndex+1)+"|"+item.trackIndex;item.trackId=t.id;}
            return item;
        });
    }
    function row(key,home) {
        if(key==="library") return {key:key,title:"Your library",kind:"albums",items:project(albums,"albums")};
        if(key.indexOf("local-")===0) {var kind=key.slice(6);return {key:key,title:"Local "+kind,kind:kind,items:project(albums,kind)};}
        return homeRows.concat(shelves).find(function(r){return r.key===key}) || {key:key,title:"Collection",items:[]};
    }
    function listItems(key) {return row(key,false).items;}
    function filterOptions(key) {
        var values=[];
        catalogue.albums.forEach(function(a){var value=key==="decade" ? (Number(a.year)>0 ? Math.floor(a.year/10)*10+"s" : "") : a[key];if(value && values.indexOf(value)<0) values.push(value);});
        values.sort();
        return [{value:"",label:"All",enabled:true}].concat(values.map(function(v){var next=Object.assign({},lens);next[key]=v;return {value:v,label:v,enabled:catalogue.albums.some(function(a){return Lists.matches(a,next)})};}));
    }
    function setLens(key,value) {
        if(["genre","decade","country","language"].indexOf(key)<0) return false;
        if(!filterOptions(key).some(function(o){return o.value===value && o.enabled})) return false;
        var next=Object.assign({},lens);next[key]=value;lens=next;return true;
    }
    function setKind(kind) {if(["albums","artists","songs"].indexOf(kind)<0)return false;lens=Object.assign({},lens,{kind:kind});return true;}
    function setPickerQuery(value) {pickerQuery=value;}
    function togglePickerArtist(id) {var next=pickerDraft.slice(),at=next.indexOf(id);if(at<0)next.push(id);else next.splice(at,1);pickerDraft=next;}
    function artistOptions(value) {return project(catalogue.albums,"artists").filter(function(a){return a.title.toLowerCase().indexOf(String(value||"").toLowerCase())>=0});}
    function answerPicker(values) {
        var valid=artistOptions("").map(function(a){return a.id});
        var selected=values.filter(function(v,i){return valid.indexOf(v)>=0 && values.indexOf(v)===i});
        if(!preferencesStore || !preferencesStore.setVinylTaste(true,selected)) {personalError="Taste choices could not be saved.";return false;}
        loadTaste(); return true;
    }
    function skipPicker() {answerPicker([]);}
    function resetPicker() {
        if(!preferencesStore || !preferencesStore.setVinylTaste(false,[])) {personalError="Taste choices could not be saved.";return false;}
        loadTaste();return true;
    }
    function activate(item) {itemRequested(item);}
    function openList(key,home) {listRequested(key,home);}
    function resume() {resumeRequested();}
    Connections {target:root.session;function onLibraryChanged(){root.refreshSearch();}function onProfileGenerationChanged(){root.loadTaste();root.query="";root.lens=({kind:"albums",genre:"",decade:"",country:"",language:""});}}
}
