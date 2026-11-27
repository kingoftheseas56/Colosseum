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

var DEFAULT_WALLPAPER = "../assets/wallpaper/cold-ripple.jpg";

// Owned stills use the content store (local assets in dev, published assets in releases).
function imageSource(pick, contentStore) {
    var source = pick && pick.image_url ? String(pick.image_url)
                                       : DEFAULT_WALLPAPER;
    // Old KDE picks stored a wsrv.nl URL with an encoded jsDelivr origin.
    // Match the repository path in both those URLs and bare upstream URLs.
    if (source.indexOf("native:") === 0
            || /(?:\/|%2f)KDE(?:\/|%2f)plasma-workspace-wallpapers(?:@|%40|\/|%2f)/i.test(source))
        return defaultBackgroundSource(contentStore);
    if (source.indexOf("../assets/") === 0)
        return contentStore ? contentStore.assetUrl(source.substring(3)) : "";
    return source;
}

// Page backgrounds share the shell's default and track content publications.
function defaultBackgroundSource(contentStore) {
    if (contentStore) {
        var source = (contentStore.generation, imageSource(null, contentStore));
        if (source) return source;
    }
    return Qt.resolvedUrl(DEFAULT_WALLPAPER);
}

function colosseumPicks() {
    return ["Silk", "Folds", "Tide"].map(function(title) {
        var id = "colosseum-" + title.toLowerCase();
        var stem = "../assets/wallpaper/" + id;
        return {
            source: "Colosseum",
            source_id: id,
            source_url: "",
            image_url: stem + ".jpg",
            thumb_url: stem + "-thumb.jpg",
            w: 3840, h: 2160,
            aspect: "16:9",
            artist: "Colosseum",
            attribution: "Colosseum",
            query: "",
            title: title,
            spec: "3840x2160 - Still - Colosseum"
        };
    });
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
