// ContinueCovers.js — fallback cover art for Continue entries saved WITHOUT a cover (e.g. a manga
// recorded before its art loaded, like the One Piece tile). Given a title, resolve the AniList
// cover by name. Keyless (the standing sourcing law). Results are cached per title for the session.
.pragma library

var ANILIST = "https://graphql.anilist.co";
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
        cache[title] = url;
        done(url);
    };
    xhr.open("POST", ANILIST);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("Accept", "application/json");
    xhr.send(JSON.stringify({ query: query, variables: { q: title } }));
}
