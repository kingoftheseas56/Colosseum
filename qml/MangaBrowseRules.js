// Shared presentation recipes. Catalogue predicates remain native/static-owner SQL.
.pragma library
var PREVIEW_ROW_CAP = 24;
var DISPLAY_ROW_CAP = 20;
var MIN_ROW_ITEMS = 3;
function defaultRows() {
    return [
        {key:"popular", title:"Popular Manga", catalogId:"popular", filters:[]},
        {key:"completed", title:"Completed Series", catalogId:"completed", filters:[]},
        {key:"publishing", title:"Currently Publishing", catalogId:"publishing", filters:[]},
        {key:"short-completed", title:"Completed in Five Volumes or Fewer", catalogId:"short-completed", filters:[]},
        {key:"one-shots", title:"One-shots", catalogId:"one-shots", filters:[]},
        {key:"started-1990s", title:"Manga That Began in the 1990s", catalogId:"started-1990s", filters:[]},
        {key:"started-2000s", title:"Manga That Began in the 2000s", catalogId:"started-2000s", filters:[]},
        {key:"started-2010s", title:"Manga That Began in the 2010s", catalogId:"started-2010s", filters:[]}
    ];
}
function pin(def) {
    return {type:"manga", catalogId:def.catalogId, filterGroup:"", filterKey:""};
}
// Preserve provider order within equal exposure, but avoid identical previews on every rail.
// This is presentation-only: See all retains the owner's full, stable popularity order.
function present(definitions, pages, showExplicit, visible) {
    var exposure = {}, rows = [];
    for (var i = 0; i < definitions.length; ++i) {
        var def = definitions[i], page = pages[def.key], seen = {}, candidates = [];
        var source = page && page.items ? page.items : [];
        for (var j = 0; j < source.length; ++j) {
            var raw = source[j], id = raw && raw.mal_id;
            if (typeof id !== "number" || !isFinite(id) || id <= 0 || Math.floor(id) !== id) continue;
            var key = String(id);
            if (seen[key] || !visible("tankoban", raw, showExplicit)) continue;
            seen[key] = true;
            candidates.push({raw:raw, id:key, position:j});
        }
        if (candidates.length < MIN_ROW_ITEMS) continue;
        // Popular stays ranked, other rows preferentially reveal unseen titles.
        if (def.key !== "popular") candidates.sort(function(a,b) {
            return (exposure[a.id] || 0) - (exposure[b.id] || 0) || a.position - b.position;
        });
        var cap = def.key === "popular" ? 10 : DISPLAY_ROW_CAP;
        var items = candidates.slice(0, cap).map(function(entry) {
            exposure[entry.id] = (exposure[entry.id] || 0) + 1;
            var r = entry.raw, title = r.title || r.title_english || "";
            return {id:entry.id, malId:entry.id, type:"manga", title:title, caption:title,
                cover:r.cover || (r.images && r.images.jpg && (r.images.jpg.large_image_url || r.images.jpg.image_url)) || "",
                year:r.year || 0, rating:r.score || 0, raw:r, availability:r.availability === true,
                explicit:r.explicit === true};
        });
        rows.push({key:def.key, title:def.title, ranked:def.key === "popular", sourceKind:"house",
            items:items, seeAllPin:pin(def)});
    }
    return rows;
}
