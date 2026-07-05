// ContinueCovers.js — fallback cover art for Continue entries saved WITHOUT a cover (e.g. a manga
// recorded before its art loaded, like the One Piece tile). Keyless ladder (the standing sourcing
// law): AniList by name first, Kitsu when AniList misses — AniList disabled its whole public API
// 2026-07 ("severe stability issues"), which blanked every fallback tile until Kitsu stepped in.
// Both hosts ride the QML factory's IPv4 pins (dead-IPv6 machine). Results cached per title for
// the session; auto-heals to AniList quality when their API returns.
.pragma library

var ANILIST = "https://graphql.anilist.co";
var KITSU   = "https://kitsu.io/api/edge/manga";
var cache = {};   // title → cover url ("" = looked up, none found)

function fetch(title, done) {
    if (!title) { done(""); return; }
    if (cache[title] !== undefined) { done(cache[title]); return; }
    var query = "query($q:String){Media(search:$q,type:MANGA,sort:SEARCH_MATCH){coverImage{extraLarge large}}}";
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var url = "";
        try {
            var j = JSON.parse(xhr.responseText);
            var ci = j && j.data && j.data.Media && j.data.Media.coverImage;
            if (ci) url = ci.extraLarge || ci.large || "";
        } catch (e) { url = ""; }
        if (url) { cache[title] = url; done(url); return; }
        kitsuFetch(title, done);                 // AniList missed/disabled → second rung
    };
    xhr.open("POST", ANILIST);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("Accept", "application/json");
    xhr.send(JSON.stringify({ query: query, variables: { q: title } }));
}

function kitsuFetch(title, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var url = "";
        try {
            var j = JSON.parse(xhr.responseText);
            var pi = j && j.data && j.data.length
                     && j.data[0].attributes && j.data[0].attributes.posterImage;
            if (pi) url = pi.original || pi.large || "";
        } catch (e) { url = ""; }
        cache[title] = url;
        done(url);
    };
    xhr.open("GET", KITSU + "?filter%5Btext%5D=" + encodeURIComponent(title)
                  + "&page%5Blimit%5D=1&fields%5Bmanga%5D=posterImage");
    xhr.setRequestHeader("Accept", "application/vnd.api+json");
    xhr.send();
}
