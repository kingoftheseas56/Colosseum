.pragma library
// Native exact query identities own delivery. A peer completion never rereads
// or remaps another shelf, and the existing slot objects remain retained.
function definitions() {
    var curated = [
        ["Recently Available", "recently-available", "", ""],
        ["Complete Runs", "complete-runs", "", ""],
        ["Near Complete", "near-complete", "", ""],
        ["Omnibuses", "popular", "format", "omnibus"],
        ["Deluxe Editions", "popular", "format", "deluxe"],
        ["Graphic Novels", "popular", "format", "graphic novel"],
        ["Community Collections", "community-collections", "", ""]
    ].map(function(s) {
        return {label:s[0], catalogId:s[1], filterGroup:s[2], filterKey:s[3],
            method:"discoverPageFiltered", args:[s[1],s[2]?[{axis:s[2],key:s[3]}]:[],false,0,24]};
    });
    return curated.concat([
        ["Most Stocked","stocked",""], ["Marvel","publisher","Marvel"],
        ["DC","publisher","DC"], ["Image","publisher","Image"],
        ["The 2020s","decade","2020"], ["The 2010s","decade","2010"],
        ["Deep Shelves","deep",""], ["Fan-Made Shelf","fanmade",""]
    ].map(function(s) {return {label:s[0],kind:s[1],arg:s[2],method:"shelf",args:[s[1],s[2],24]};}));
}
function queryKey(catalog, spec) {
    return typeof catalog.remoteQueryKey === "function" ? catalog.remoteQueryKey(spec.method,spec.args) : "";
}
function readShelf(catalog, spec) {
    var a=spec.args;
    var rows=spec.method === "shelf" ? catalog.shelf(a[0],a[1],a[2])
        : (catalog.discoverPageFiltered(a[0],a[1],a[2],a[3],a[4]) || {}).items;
    var result={label:spec.label,rows:(rows||[]).map(function(r) {
        var item={caption:r.title+(r.year?" ("+r.year+")":""),title:r.title,
            cover:r.cover||"",coverCandidates:r.coverCandidates||[]};
        if(spec.method === "shelf") item.gcdId=r.gcdId;
        else item.locgId="locg:"+r.locgId;
        return item;
    })};
    if(spec.method === "shelf") { result.kind=spec.kind; result.arg=spec.arg; }
    else {result.catalogId=spec.catalogId;result.filterGroup=spec.filterGroup;result.filterKey=spec.filterKey;}
    return result;
}
function initial(catalog) {return definitions().map(function(s){return readShelf(catalog,s);});}
function update(catalog, key, previous) {
    var specs=definitions();
    for(var i=0;i<specs.length;i++) if(queryKey(catalog,specs[i]) === key) {
        var next=previous.slice(); next[i]=readShelf(catalog,specs[i]); return next;
    }
    return previous;
}
