// BiblioSeriesFold — collapse a flat list of Apple book cards into the "Books vs Series"
// shape: books that belong to a series fold into ONE series tile (with the total series size),
// standalones pass through as single book cards. This is what makes a genre/search grid show
// "Throne of Glass · 8 books" as one stack instead of eight loose tiles.
//
// `idx` is the SeriesIndex context object, passed in because a .pragma library JS file has no
// access to the QML context. idx.lookup(title, author) -> {found, series, position, rating};
// idx.seriesEntries(series) -> the full ordered roster (its .length is the "N books" count).
.pragma library

function authorOf(c) { return (c && (c.authors || c.author)) || ""; }

function asBook(c) {
    return { kind: "book", title: c.title, authors: authorOf(c), author: authorOf(c),
             year: c.year, genres: c.genres, synopsis: c.synopsis,
             cover: c.cover, c1: c.c1, c2: c.c2 };
}

function foldSeries(cards, idx) {
    if (!cards || !cards.length) return [];
    if (!idx) return cards.map(asBook);                    // bridge missing -> everything stays a single book

    var out = [], byKey = {};
    for (var i = 0; i < cards.length; i++) {
        var c = cards[i];
        var info = idx.lookup(c.title || "", authorOf(c));
        if (info && info.found && info.series) {
            var key = info.series;
            var pos = parseFloat(info.position); if (isNaN(pos)) pos = 999;
            if (!(key in byKey)) {
                byKey[key] = { kind: "series", series: key, author: authorOf(c),
                               cover: c.cover || "", c1: c.c1, c2: c.c2,
                               rating: info.rating || 0, count: 0, _bestPos: pos };
                out.push(byKey[key]);                      // first appearance fixes grid order
            }
            var t = byKey[key];
            if (pos < t._bestPos) {                        // the lowest-position member's cover leads the stack
                t._bestPos = pos;
                if (c.cover) t.cover = c.cover;
            }
        } else {
            out.push(asBook(c));
        }
    }
    // count = the WHOLE series size from the index (the "8 books"), not just what's charting here
    for (var k in byKey) {
        var entries = idx.seriesEntries(k);
        byKey[k].count = (entries && entries.length) ? entries.length : 0;
    }
    return out;
}
