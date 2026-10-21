import QtQuick
import "VinylWorldLists.js" as Lists

Item {
    id: root
    objectName: "vinylWorldData"
    property var catalogue: ({albums: [], profiles: {}, people: {}, homeRegion: []})
    property url artworkRoot: ""
    property string profile: "new"
    property var lens: ({kind:"albums",genre:"",decade:"",country:"",language:""})
    property bool catalogueReachable: true
    property bool tastePickerOpen: false
    property var profileStates: ({new:{answered:false,picks:[]}, skipped:{answered:true,picks:[]},
                                  seasoned:{answered:true,picks:[]}})
    readonly property var picks: (profileStates[profile] || {}).picks || []
    readonly property bool pickerAnswered: !!(profileStates[profile] || {}).answered
    readonly property var profileRecord: catalogue.profiles[profile === "seasoned" ? "seasoned" : "new"] || ({history:[],saved:[]})
    property var shelves: []
    property var shelfCache: ({})
    property var rowTitles: ({})
    readonly property var pickerDraft: (profileStates[profile] || {}).draft || []
    readonly property string pickerQuery: (profileStates[profile] || {}).query || ""
    readonly property var shelfKeys: shelves.map(function(s) {return s.key})
    readonly property var homeRows: Lists.homeRows(catalogue,profileRecord,picks)
    readonly property var coldShelves: pickerAnswered ? Lists.coldShelves(catalogue,profileRecord,picks) : []
    readonly property string lensTitle: [lens.language,lens.decade,lens.genre,lens.country].filter(function(v) {return !!v}).join(" · ") || "Everything"
    property var resumeSlide: ({})
    readonly property var carouselSlides: {
        var out=[]
        if (resumeSlide.title) out.push(Object.assign({key:"resume",cta:"Resume",kicker:"Continue listening"},resumeSlide))
        shelves.filter(function(s) {return s.items.length}).slice(0,3).forEach(function(s) {
            var i=s.items[0]
            out.push({key:s.key,title:i.title,blurb:i.subtitle,art:artwork(i.cover),
                artKind:"poster",cta:"Explore",kicker:s.title})
        })
        return out
    }
    signal itemRequested(var item)
    signal listRequested(string key, bool home)
    signal resumeRequested()
    function artwork(file) { return file && artworkRoot ? String(artworkRoot)+"/"+file : "" }
    function cacheKey() { return profile+"|"+JSON.stringify(lens) }
    function refresh() {
        if (!catalogueReachable) return;
        shelves=Lists.shelves(catalogue,lens,profileRecord)
        var titles=Object.assign({},rowTitles)
        shelves.forEach(function(row) { titles[profile+"|"+row.key]=row.title })
        rowTitles=titles
        var next=Object.assign({},shelfCache);next[cacheKey()]=shelves;shelfCache=next;
    }
    onProfileChanged: {
        if (catalogueReachable) refresh();
        else shelves=shelfCache[cacheKey()] || [];
    }
    onCatalogueChanged: refresh()
    onLensChanged: refresh()
    onProfileRecordChanged: refresh()
    onCatalogueReachableChanged: if (catalogueReachable) refresh()
    Component.onCompleted: refresh()
    function listItems(key) {
        var all=shelves.concat(homeRows,coldShelves)
        var row=all.find(function(r) {return r.key===key})
        return row ? row.items : []
    }
    function row(key,home) {
        var source=home ? homeRows.concat(coldShelves) : shelves
        return source.find(function(r) {return r.key===key}) || {key:key,title:rowTitles[profile+"|"+key] || "",items:[]}
    }
    function filterOptions(key) {
        var values=[]
        Lists.albums(catalogue).forEach(function(a) {
            var v=key==="decade" ? Math.floor(a.year/10)*10+"s" : a[key]
            if (values.indexOf(v)<0) values.push(v)
        })
        values.sort()
        return [{value:"",label:key==="decade" ? "Any decade" : "All",enabled:true}].concat(values.map(function(v) {
            var next=Object.assign({},lens);next[key]=v
            return {value:v,label:v,enabled:Lists.shelves(catalogue,next,profileRecord).length>0}
        }))
    }
    function setLens(key,value) {
        if (!catalogueReachable || ["genre","decade","country","language"].indexOf(key)<0) return false
        var option=filterOptions(key).find(function(o) {return o.value===value})
        if (!option || !option.enabled) return false
        var next=Object.assign({},lens);next[key]=value;lens=next;return true
    }
    function setKind(kind) {
        if (!catalogueReachable || ["albums","artists","songs"].indexOf(kind)<0) return false
        lens=Object.assign({},lens,{kind:kind});return true
    }
    function setProfile(value) {
        if (["new","skipped","seasoned"].indexOf(value)<0) return
        profile=value
    }
    function setPickerQuery(value) {
        var states=Object.assign({},profileStates)
        states[profile]=Object.assign({},states[profile],{query:value})
        profileStates=states
    }
    function togglePickerArtist(id) {
        var draft=pickerDraft.slice(), at=draft.indexOf(id)
        if (at<0) draft.push(id); else draft.splice(at,1)
        var states=Object.assign({},profileStates)
        states[profile]=Object.assign({},states[profile],{draft:draft})
        profileStates=states
    }
    function answerPicker(values) {
        tastePickerOpen=false;
        var valid=Lists.artistOptions(catalogue,"").map(function(i) {return i.id})
        var selected=values.filter(function(v,i) {return valid.indexOf(v)>=0 && values.indexOf(v)===i})
        var states=Object.assign({},profileStates);states[profile]={answered:true,picks:selected};profileStates=states
    }
    function skipPicker() { answerPicker([]) }
    function resetPicker() {
        tastePickerOpen=true;
        var states=Object.assign({},profileStates);states[profile]={answered:false,picks:[]};profileStates=states
    }
    function artistOptions(query) { return Lists.artistOptions(catalogue,query) }
    function setCatalogueReachable(value) { catalogueReachable=value }
    function activate(item) { itemRequested(item) }
    function openList(key,home) { listRequested(key,home) }
    function resume() { resumeRequested() }
}
