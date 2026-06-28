// WallpaperApi.js - zero-auth Wallhaven adapter for Colosseum wallpaper search.
// Small-scope contract: SFW, born-wide still wallpapers only.
.pragma library

var WALLHAVEN = "https://wallhaven.cc/api/v1";

function defaultQueryFor(world) {
    if (world === "Tankoban")
        return "one piece";
    if (world === "Biblio")
        return "books library";
    if (world === "Theatre")
        return "cinema";
    return "landscape";
}

function requestJson(url, done) {
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
            finish(null, "Wallhaven returned " + xhr.status);
            return;
        }
        try {
            finish(JSON.parse(xhr.responseText), "");
        } catch (e) {
            finish(null, "Could not read Wallhaven response");
        }
    };
    xhr.onerror = function() { finish(null, "Could not reach Wallhaven"); };
    xhr.ontimeout = function() { finish(null, "Wallhaven request timed out"); };
    xhr.open("GET", url);
    xhr.timeout = 9000;
    xhr.send();
}

function isBornWide(item) {
    if (!item || !item.path)
        return false;

    var w = Number(item.dimension_x || 0);
    var h = Number(item.dimension_y || 0);
    if (w < 1920 || h < 1080)
        return false;

    return Math.abs((w / h) - (16 / 9)) < 0.01;
}

function mapItem(item, query) {
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
        aspect: "16:9",
        attribution: "Wallhaven / original uploader",
        query: query,
        title: "Wallhaven " + item.id,
        spec: resolution + " - Still - Wallhaven"
    };
}

function search(query, done) {
    var q = String(query || "").trim();
    if (!q)
        q = "landscape";

    var params = [
        "purity=100",
        "ratios=16x9",
        "atleast=1920x1080",
        "sorting=relevance",
        "order=desc",
        "q=" + encodeURIComponent(q)
    ];

    requestJson(WALLHAVEN + "/search?" + params.join("&"), function(json, error) {
        if (!json || !json.data) {
            done([], error || "No wallpaper found");
            return;
        }

        var out = [];
        for (var i = 0; i < json.data.length; i++) {
            if (isBornWide(json.data[i]))
                out.push(mapItem(json.data[i], q));
        }
        done(out, out.length ? "" : "No wallpaper found");
    });
}
