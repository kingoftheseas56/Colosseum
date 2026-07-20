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
    return "";
}

// The backdrop the app boots to (assets/wallpaper/captured-motion.jpg), offered
// back as a pick (2026-07-20). It shipped as the hardcoded default and was
// unreachable once you picked anything else. Turns out it IS an OS wallpaper: the
// official Windows 11 "Captured Motion" theme by Six N. Five for Microsoft, 4K
// 3840x2400 — which is why it reads as a desktop shell. A plain bundled image on
// the same relative path Main resolves for the boot wallpaper.
function houseDefaultPick() {
    return {
        source: "Captured Motion",
        source_id: "colosseum-motion",   // stable id (kept; predates the Win11 identification)
        source_url: "",
        image_url: "../assets/wallpaper/captured-motion.jpg",
        thumb_url: "../assets/wallpaper/captured-motion.jpg",
        w: 3840, h: 2400,
        aspect: "16:10",
        attribution: "Windows 11 'Captured Motion' (img25) - Six N. Five / Microsoft",
        query: "",
        title: "Captured Motion 1",
        spec: "Windows 11 'Captured Motion' - 3840x2400 (the app default)"
    };
}

// ---- Captured Motion shelf (2026-07-20) ----
// The one wallpaper the app boots to (houseDefaultPick, bundled) turned out to be
// img25 of Windows 11's "Captured Motion" theme by Six N. Five — dark, abstract,
// warm iridescent ribbons, which is exactly the desktop-shell look Hemanth wanted.
// So the shelf is that theme and nothing else (the earlier literal Windows/macOS/
// Linux defaults were pulled 2026-07-20 — none fit). The other three of the four
// (img24/26/27) come from a stable archive repo over the jsDelivr CDN, and both
// the full image and the grid thumbnail ride the keyless wsrv.nl proxy over that
// origin — a 4K source lands as a crisp capped jpeg and a ~40 KB thumb, no repo
// bloat, the same remote-pick path Wallhaven uses. (c) Six N. Five / Microsoft.

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

function _cdnPick(id, repo, branch, path, title, spec) {
    var encPath = path.split("/").map(_encSeg).join("/");
    var origin = "cdn.jsdelivr.net/gh/" + repo + "@" + branch + "/" + encPath;
    return {
        source: "Captured Motion",
        source_id: id,
        source_url: "https://github.com/" + repo,
        image_url: _wsrv(origin, 3840),   // applied wallpaper, capped at a crisp 4K
        thumb_url: _wsrv(origin, 600),    // grid thumbnail
        w: 3840, h: 2400,
        aspect: "16:10",
        attribution: title + " - Six N. Five / Microsoft",
        query: "",
        title: title,
        spec: spec
    };
}

// The other three of Windows 11's four Captured Motion wallpapers (the fourth,
// img25, is houseDefaultPick — bundled, the boot backdrop). Theme B == Captured
// Motion at 21H2. All three origins verified 200 image/jpeg 2026-07-20.
function capturedMotionPicks() {
    return [
        _cdnPick("cm-img24", "viridivn/windows11wallpapers", "master", "ThemeB/img24.jpg", "Captured Motion 2", "Windows 11 'Captured Motion' - 3840x2400"),
        _cdnPick("cm-img26", "viridivn/windows11wallpapers", "master", "ThemeB/img26.jpg", "Captured Motion 3", "Windows 11 'Captured Motion' - 3840x2400"),
        _cdnPick("cm-img27", "viridivn/windows11wallpapers", "master", "ThemeB/img27.jpg", "Captured Motion 4", "Windows 11 'Captured Motion' - 3840x2400")
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
