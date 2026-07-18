// WallpaperApi.js - zero-auth wallpaper search for Colosseum.
// Small-scope contract: SFW, wide still wallpapers only.
//
// Sources (both keyless, per standing sourcing law):
//   Wallhaven - the base pool. Paginated (24/page server-side); relevance /
//               toplist / random sorting; random rides the API's seed so
//               load-more never repeats a page.
//   Konachan  - the anime board (konachan.net = the SFW mirror), rating:s
//               forced. Titles are board tags ("one piece" -> "one_piece"),
//               so series-shaped queries - this app's main shape - map clean.
// (Reddit's wallpaper subs were probed 2026-07-18 and are 403-blocked from
// this network at the HTTP layer, browser UA included - not a source here.)
//
// The QML owns ONE opaque search state from freshState(); fetchPage() pulls
// the current page from every source still serving, advances the state, and
// hands back merged rows. hasMore() drives the Load More affordance.
.pragma library

var WALLHAVEN = "https://wallhaven.cc/api/v1";
var KONACHAN = "https://konachan.net";
var PAGE_LIMIT = 24;   // both sources serve 24/page

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
    }];
}

function isNativePick(url) {
    return String(url || "").indexOf("native:") === 0;
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

// Konachan file/preview urls are absolute today but were protocol-relative
// historically - normalize both shapes.
function _absUrl(u) {
    var s = String(u || "");
    if (s.indexOf("//") === 0)
        return "https:" + s;
    return s;
}

function mapKonachan(post, query) {
    var w = Number(post.width || 0);
    var h = Number(post.height || 0);

    return {
        source: "Konachan",
        source_id: "konachan-" + post.id,
        source_url: KONACHAN + "/post/show/" + post.id,
        image_url: _absUrl(post.file_url),
        thumb_url: _absUrl(post.preview_url || post.file_url),
        w: w,
        h: h,
        aspect: aspectLabel(w, h),
        attribution: "Konachan / original uploader",
        query: query,
        title: "Konachan " + post.id,
        spec: w + "x" + h + " - Still - Konachan"
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

// Board tags: a multi-word series title is ONE underscored tag ("one piece" ->
// "one_piece") - the dominant query shape here. rating:s pins the SFW gate on
// top of the SFW mirror; width filter narrows server-side.
function konachanTags(query, sorting) {
    var tags = [String(query || "").trim().toLowerCase().replace(/\s+/g, "_"),
                "rating:s", "width:>=1920"];
    if (sorting === "top")
        tags.push("order:score");
    else if (sorting === "random")
        tags.push("order:random");
    return tags.join(" ");
}

function searchKonachan(query, sorting, page, done) {
    var url = KONACHAN + "/post.json?tags=" + encodeURIComponent(konachanTags(query, sorting))
            + "&limit=" + PAGE_LIMIT + "&page=" + Math.max(1, page);
    requestJson(url, "Konachan", function(json, error) {
        if (!json || !json.length) {
            done([], true, error);      // empty page = this lane is done
            return;
        }
        var out = [];
        for (var i = 0; i < json.length; i++) {
            var p = json[i];
            if (isWideEnough(Number(p.width || 0), Number(p.height || 0)))
                out.push(mapKonachan(p, query));
        }
        done(out, json.length < PAGE_LIMIT, "");
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
        whPage: 1, whLastPage: -1, whSeed: "",   // -1 = unknown until the first reply
        koPage: 1, koDone: false
    };
}

function hasMore(state) {
    if (!state)
        return false;
    var whMore = state.whLastPage < 0 || state.whPage <= state.whLastPage;
    return whMore || !state.koDone;
}

// Interleave so the grid mixes flavors instead of stacking one source first.
function interleave(a, b) {
    var out = [];
    var n = Math.max(a.length, b.length);
    for (var i = 0; i < n; i++) {
        if (i < a.length) out.push(a[i]);
        if (i < b.length) out.push(b[i]);
    }
    return out;
}

// Fetch the CURRENT page of every source still serving, advance the state,
// return merged rows. Errors only surface when BOTH sources come back empty-
// handed - one healthy source carries the grid.
function fetchPage(state, done) {
    var whWanted = state.whLastPage < 0 || state.whPage <= state.whLastPage;
    var koWanted = !state.koDone;
    var pendingCount = (whWanted ? 1 : 0) + (koWanted ? 1 : 0);
    if (pendingCount === 0) {
        done([], state, "");
        return;
    }

    var whRows = [], koRows = [], errors = [];

    function finishOne() {
        pendingCount -= 1;
        if (pendingCount > 0)
            return;
        var rows = interleave(whRows, koRows);
        var error = (!rows.length && errors.length) ? errors.join(" · ") : "";
        done(rows, state, error);
    }

    if (whWanted) {
        searchWallhaven(state.query, state.sorting, state.whPage, state.whSeed, function(rows, meta, err) {
            whRows = rows;
            if (err) {
                errors.push(err);
                state.whLastPage = 0;          // don't hammer a failing source on Load more
            } else {
                state.whLastPage = meta.lastPage;
                state.whSeed = meta.seed || state.whSeed;
                state.whPage += 1;
            }
            finishOne();
        });
    }
    if (koWanted) {
        searchKonachan(state.query, state.sorting, state.koPage, function(rows, lastPage, err) {
            koRows = rows;
            if (err) {
                errors.push(err);
                state.koDone = true;
            } else {
                state.koDone = lastPage;
                state.koPage += 1;
            }
            finishOne();
        });
    }
}

// (kept for callers that only ever wanted one shot of results)
function search(query, done) {
    var state = freshState(query, "relevance");
    fetchPage(state, function(rows, st, error) { done(rows, error); });
}
