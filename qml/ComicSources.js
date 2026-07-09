// ComicSources.js — the comic-source registry (spec 2026-07-09). Each source is one
// .pragma library file answering the 5-verb contract (searchSeries / explore /
// exploreItems / issues / pages). Peers, never merged — the Cinemeta/Kitsu model.
.pragma library
.import "XoxoApi.js" as Xoxo
.import "ComicsApi.js" as GetComics

var sources = [
    { key: "xoxo",      name: "XOXO",      kind: "issues",   api: Xoxo },
    { key: "getcomics", name: "GetComics", kind: "archives", api: GetComics }
];
// kind - "issues": per-issue page reading (manga-like flow via Downloads.downloadPages)
//        "archives": whole-archive releases (existing ComicDownloader flow)
function byKey(k) {
    for (var i = 0; i < sources.length; i++) if (sources[i].key === k) return sources[i];
    return null;
}
