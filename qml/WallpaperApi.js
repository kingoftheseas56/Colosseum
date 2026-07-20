// WallpaperApi.js - zero-auth wallpaper search for Colosseum.
// Small-scope contract: SFW, wide still wallpapers only.
//
// Source (keyless, per standing sourcing law):
//   Wallhaven - the single searchable pool. Paginated (24/page server-side);
//               relevance / toplist / random sorting; random rides the API's
//               seed so load-more never repeats a page.
// (Konachan was dropped 2026-07-20 — the anime board returned cheap-looking
// art; Wallhaven is the one source now. Reddit's wallpaper subs were probed
// 2026-07-18 and are 403-blocked from this network — not a source here.)
//
// The QML owns ONE opaque search state from freshState(); fetchPage() pulls
// the current page from Wallhaven, advances the state, and hands back the
// rows. hasMore() drives the Load More affordance.
.pragma library

var WALLHAVEN = "https://wallhaven.cc/api/v1";
var PAGE_LIMIT = 24;   // Wallhaven serves 24/page

// ---- Colosseum-native living wallpapers (2026-07-18) ----
// Designed in-house, drawn live by QML — separate from the searchable pool.
// image_url carries the "native:" route Main resolves to a scene file.
function nativePicks() {
    return [{
        source: "Colosseum",
        source_id: "native:arena-night",
        source_url: "",
        image_url: "native:arena-night",
        thumb_url: "",
        w: 0, h: 0,
        aspect: "any",
        attribution: "Colosseum original",
        query: "",
        title: "The Arena at Night",
        spec: "Living wallpaper - Colosseum native"
    }, {
        source: "Colosseum",
        source_id: "native:gilded-rain",
        source_url: "",
        image_url: "native:gilded-rain",
        thumb_url: "",
        w: 0, h: 0,
        aspect: "any",
        attribution: "Colosseum original",
        query: "",
        title: "Gilded Rain",
        spec: "Living wallpaper - Colosseum native"
    }, {
        // Ported from a real KDE Plasma QML wallpaper plugin (2026-07-20), LGPL-2.1;
        // KDE deps stripped for our Qt6 build. See THIRD_PARTY_NOTICES.md.
        source: "Colosseum",
        source_id: "native:aurora-flow",
        source_url: "https://github.com/VicenteMcMahon/kde-plasma-gradient-wallpaper",
        image_url: "native:aurora-flow",
        thumb_url: "",
        w: 0, h: 0,
        aspect: "any",
        attribution: "Ported from KDE Plasma gradient wallpaper (LGPL-2.1)",
        query: "",
        title: "Aurora Flow",
        spec: "Living gradient - ported from KDE Plasma (QML)"
    }];
}

function isNativePick(url) {
    return String(url || "").indexOf("native:") === 0;
}

// The ONE map from a "native:" route to its live QML scene file. Every surface that
// draws a native wallpaper — the runtime shell (Main), the picker preview, the picker
// tiles — resolves through here, so a new living wallpaper is added in exactly one place.
// (Main mirrors this in nativeWallpaperFile(); keep the two in sync when adding a scene.)
function nativeSceneFor(url) {
    if (url === "native:arena-night") return "wallpapers/ArenaNight.qml";
    if (url === "native:gilded-rain") return "wallpapers/GildedRain.qml";
    if (url === "native:aurora-flow") return "wallpapers/AuroraFlow.qml";
    return "";
}

// ---- KDE Plasma wallpapers shelf (2026-07-20) ----
// Real OS-desktop wallpapers with a free license: KDE Plasma's own default set
// (github.com/KDE/plasma-workspace-wallpapers), each CC-BY-SA-4.0 or LGPLv3 — both
// permit redistribution with credit. The artist rides each pick (shown on the
// tile) and the licence is recorded in THIRD_PARTY_NOTICES.md. Same zero-bloat
// path as any remote pick: the full image + the grid thumbnail both ride the
// keyless wsrv.nl proxy over a jsDelivr CDN origin, so a 5K source lands as a
// crisp capped jpeg and a ~40 KB thumb with nothing committed to the repo.
// (This replaced the Captured Motion shelf — those were Microsoft-copyrighted.)

// encodeURIComponent leaves ()! literal; the CDN is happy either way, but we
// pin parens too so the emitted URL is byte-stable across encoders.
function _encSeg(seg) {
    return encodeURIComponent(seg).replace(/\(/g, "%28").replace(/\)/g, "%29");
}

// wsrv.nl fetches `url` (the percent-encoded jsDelivr origin, no scheme),
// resizes to `width`, and re-encodes as jpeg. The origin is percent-encoded
// again for the query string — wsrv decodes it once before fetching.
function _wsrv(originNoScheme, width) {
    return "https://wsrv.nl/?url=" + encodeURIComponent(originNoScheme)
         + "&w=" + width + "&output=jpg&q=82";
}

// one KDE wallpaper -> a pick. `file` is the folder's largest landscape image
// under contents/images/. artist + license carried for on-tile credit + notices.
function _kdePick(folder, file, title, artist, license) {
    var path = folder + "/contents/images/" + file;
    var encPath = path.split("/").map(_encSeg).join("/");
    var origin = "cdn.jsdelivr.net/gh/KDE/plasma-workspace-wallpapers@master/" + encPath;
    return {
        source: "KDE Plasma",
        source_id: "kde-" + folder.toLowerCase(),
        source_url: "https://github.com/KDE/plasma-workspace-wallpapers/tree/master/" + folder,
        image_url: _wsrv(origin, 3840),   // applied wallpaper, capped at a crisp 4K
        thumb_url: _wsrv(origin, 600),    // grid thumbnail
        w: 0, h: 0,
        aspect: "wide",
        artist: artist,
        license: license,
        attribution: title + " by " + artist + " (" + license + ") - KDE Plasma",
        query: "",
        title: title,
        spec: title + " - " + artist + " - " + license
    };
}

// The curated KDE Plasma set (13). All origins verified 200 image/jpeg
// 2026-07-20; the ultrawide 32:9 ones (Nuvole, Coast) were left out so nothing
// crops badly on a normal screen.
function kdePicks() {
    return [
        _kdePick("Cascade", "3840x2160.png", "Cascade", "Ken Vermette", "LGPLv3"),
        _kdePick("Flow", "5120x2880.jpg", "Flow", "Sandra Smukaste", "CC-BY-SA-4.0"),
        _kdePick("Kay", "5120x2880.png", "Kay", "ruvkr", "CC-BY-SA-4.0"),
        _kdePick("Shell", "5120x2880.jpg", "Shell", "Lucas Andrade", "CC-BY-SA-4.0"),
        _kdePick("Volna", "5120x2880.jpg", "Volna", "Nikita Babin", "CC-BY-SA-4.0"),
        _kdePick("Nexus", "5120x2880.png", "Nexus", "Krystian Zajdel", "CC-BY-SA-4.0"),
        _kdePick("Opal", "3840x2160.png", "Opal", "Ken Vermette", "LGPLv3"),
        _kdePick("ColdRipple", "2560x1600.jpg", "Cold Ripple", "Risto Saukonpää", "LGPLv3"),
        _kdePick("Elarun", "2560x1600.png", "Elarun", "Nuno Pinheiro", "LGPLv3"),
        _kdePick("IceCold", "5120x2880.png", "Ice Cold", "Santiago Cézar", "CC-BY-SA-4.0"),
        _kdePick("PastelHills", "3200x2000.jpg", "Pastel Hills", "Lionel", "LGPLv3"),
        _kdePick("Honeywave", "5120x2880.jpg", "Honeywave", "Ken Vermette", "CC-BY-SA-4.0"),
        _kdePick("Kokkini", "3840x2160.png", "Kokkini", "Ken Vermette", "LGPLv3")
    ];
}

function defaultQueryFor(world) {
    if (world === "Tankoban")
        return "one piece";
    if (world === "Biblio")
        return "books library";
    if (world === "Theatre")
        return "cinema";
    return "landscape";
}

function requestJson(url, label, done) {
    var xhr = new XMLHttpRequest();
    var completed = false;

    function finish(json, error) {
        if (completed)
            return;
        completed = true;
        done(json, error || "");
    }

    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE)
            return;
        if (xhr.status < 200 || xhr.status >= 300) {
            finish(null, label + " returned " + xhr.status);
            return;
        }
        try {
            finish(JSON.parse(xhr.responseText), "");
        } catch (e) {
            finish(null, "Could not read " + label + " response");
        }
    };
    xhr.onerror = function() { finish(null, "Could not reach " + label); };
    xhr.ontimeout = function() { finish(null, label + " request timed out"); };
    xhr.open("GET", url);
    xhr.timeout = 9000;
    xhr.send();
}

// Wide enough to fill the frame: at least 1080p, and an aspect from 16:10 up
// to ultrawide - PreserveAspectCrop trims the rest (Axis 1: the old exact-16:9
// gate threw away born-wide art over a rounding hair).
function isWideEnough(w, h) {
    if (w < 1920 || h < 1080)
        return false;
    var ratio = w / h;
    return ratio >= 1.55 && ratio <= 2.45;
}

function aspectLabel(w, h) {
    var ratio = h > 0 ? w / h : 0;
    if (Math.abs(ratio - (16 / 9)) < 0.04) return "16:9";
    if (Math.abs(ratio - (16 / 10)) < 0.04) return "16:10";
    if (ratio > 2.1) return "21:9";
    return "wide";
}

function mapWallhaven(item, query) {
    var w = Number(item.dimension_x || 0);
    var h = Number(item.dimension_y || 0);
    var resolution = item.resolution || (w + "x" + h);

    return {
        source: "Wallhaven",
        source_id: item.id,
        source_url: item.url,
        image_url: item.path,
        thumb_url: item.thumbs && item.thumbs.large ? item.thumbs.large : item.path,
        w: w,
        h: h,
        aspect: aspectLabel(w, h),
        attribution: "Wallhaven / original uploader",
        query: query,
        title: "Wallhaven " + item.id,
        spec: resolution + " - Still - Wallhaven"
    };
}

// ---- per-source fetches ----

function searchWallhaven(query, sorting, page, seed, done) {
    var params = [
        "purity=100",
        "ratios=16x9,16x10,21x9",
        "atleast=1920x1080",
        "sorting=" + (sorting === "top" ? "toplist" : sorting === "random" ? "random" : "relevance"),
        "order=desc",
        "page=" + Math.max(1, page),
        "q=" + encodeURIComponent(query)
    ];
    if (sorting === "random" && seed)
        params.push("seed=" + encodeURIComponent(seed));

    requestJson(WALLHAVEN + "/search?" + params.join("&"), "Wallhaven", function(json, error) {
        if (!json || !json.data) {
            done([], { lastPage: 0, seed: "" }, error);
            return;
        }
        var out = [];
        for (var i = 0; i < json.data.length; i++) {
            var it = json.data[i];
            if (isWideEnough(Number(it.dimension_x || 0), Number(it.dimension_y || 0)))
                out.push(mapWallhaven(it, query));
        }
        var meta = json.meta || ({});
        done(out, { lastPage: Number(meta.last_page || page), seed: String(meta.seed || "") }, "");
    });
}

// ---- the one search the QML drives ----

function freshState(query, sorting) {
    var q = String(query || "").trim();
    if (!q)
        q = "landscape";
    return {
        query: q,
        sorting: sorting === "top" || sorting === "random" ? sorting : "relevance",
        whPage: 1, whLastPage: -1, whSeed: ""    // -1 = unknown until the first reply
    };
}

function hasMore(state) {
    if (!state)
        return false;
    return state.whLastPage < 0 || state.whPage <= state.whLastPage;
}

// Fetch the CURRENT page from Wallhaven, advance the state, return the rows.
function fetchPage(state, done) {
    if (!(state.whLastPage < 0 || state.whPage <= state.whLastPage)) {
        done([], state, "");
        return;
    }

    searchWallhaven(state.query, state.sorting, state.whPage, state.whSeed, function(rows, meta, err) {
        if (err) {
            state.whLastPage = 0;              // don't hammer a failing source on Load more
            done([], state, rows.length ? "" : err);
            return;
        }
        state.whLastPage = meta.lastPage;
        state.whSeed = meta.seed || state.whSeed;
        state.whPage += 1;
        done(rows, state, "");
    });
}

// (kept for callers that only ever wanted one shot of results)
function search(query, done) {
    var state = freshState(query, "relevance");
    fetchPage(state, function(rows, st, error) { done(rows, error); });
}
