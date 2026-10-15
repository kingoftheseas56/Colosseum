// BiblioGenreApi.js - live data for Biblio's genre BROWSE page.
//
// Mirrors GenreApi.js' public shape exactly: loadGenre(name, sort, push) calls
// push({ count, desc, cards, montage }); cards feed BiblioGenrePage's cloned manga layout.
// Source: Apple Books RSS top ebooks by genre, keyless/no-login, same mzstatic cover infra as BiblioApi.
.pragma library
.import "BiblioApi.js" as BiblioApi
.import "ExplicitContentPolicy.js" as Policy

var COUNTRY = "us";
var FEED = "https://itunes.apple.com/" + COUNTRY + "/rss/topebooks/limit=100/genre=";

var GENRE_IDS = {
    "Fiction & Literature": 9031,
    "Mysteries & Thrillers": 9032,
    "Sci-Fi & Fantasy": 9020,
    "Romance": 9003,
    "Biographies & Memoirs": 9008,
    "History": 9015,
    "Young Adult": 11165,
    "Comics & Graphic Novels": 9026,
    "Humor": 9012,
    "Travel & Adventure": 9004
};

var GENRE_DESC = {
    "Fiction & Literature": "Stories where character runs deeper than plot. Fiction and literature trade in the interior weight of ordinary lives - the slow press of memory, the friction between what is said and what is meant. These books earn their pace by refusing to look away from what other genres skim past.",
    "Mysteries & Thrillers": "A puzzle and a pulse. Mysteries bait the mind with a hidden truth; thrillers grip the body with escalating danger. Both begin with something broken - a crime, a lie, a disappearance - and make the reader a second investigator, sifting evidence alongside the detective until the room shrinks to one answer.",
    "Sci-Fi & Fantasy": "Reality with a lever pushed. Science fiction tests what might be true if technology or time took one more step; fantasy builds worlds from myth and magic that never were but feel whole. Both genres measure humanity against unfamiliar orders - the alien, the ancient, the yet-to-be.",
    "Romance": "The architecture of falling. Romance novels build toward a promise kept - an emotionally just finish - but earn it through friction: the wrong person in the right light, a wall two people chip at from opposite sides. At its centre is the radical act of trusting another person with the self you barely trust alone.",
    "Biographies & Memoirs": "A single life held up to the light. Biographies build from the outside in - documents, witnesses, the grain of an era - while memoirs speak from inside the skin, narrowing focus to a season or a wound. Together they ask the same unanswerable question: how does one life add up to something that means?",
    "History": "What happened and why it still moves. History reconstructs the dead from what they left behind - letters, ledgers, ruins, silence - then argues with itself about cause and meaning. Every generation questions the archive anew and gets different answers, because the present is always the hidden co-author of the past.",
    "Young Adult": "The loud years. Young adult fiction puts a teenage mind at the centre of the frame and refuses to patronize it - first love, first danger, first betrayal arrive at full voltage because they are happening for the first time. The door between childhood and the rest of the world swings one way only, and these books live in its hinge.",
    "Comics & Graphic Novels": "Stories told in panels - a medium where time lives in the space between frames. A graphic novel is a self-contained work of sequential art: image and text share every decision about pacing, silence, and revelation. What a page shows in one glance might take a prose novel three chapters to arrive at, and what it hides between panels belongs to the reader alone.",
    "Humor": "The release valve and the scalpel. Comic writing turns awkwardness, pain, and the unspoken into shared recognition - a sudden exhale that means you are not alone in what you noticed. The best humor does not dodge gravity; it slips under its guard faster than sincerity can, and lands the truth while you are still laughing.",
    "Travel & Adventure": "A departure that changes the departee. Travel and adventure writing charts the space between leaving and arriving - the foreign street, the wrong turn, the stranger whose kindness rearranges your sense of scale. The outer journey is never the whole story; every mile outward is also a mile inward, measured in the self that comes back different."
};

var SIBLINGS = [
    "Fiction & Literature", "Mysteries & Thrillers", "Sci-Fi & Fantasy", "Romance",
    "Biographies & Memoirs", "History", "Young Adult", "Comics & Graphic Novels",
    "Humor", "Travel & Adventure"
];

var palette = [
    ["#6a4a2c", "#1d1209"], ["#5a3a3f", "#180d10"], ["#3f5a4a", "#101a14"],
    ["#4a4063", "#13101f"], ["#6a5a2c", "#1d1809"], ["#3f4a63", "#0e121b"],
    ["#5a4a3a", "#181210"], ["#634050", "#1b0d14"], ["#3a5a5a", "#0e1a1a"],
    ["#5a5a3a", "#181810"]
];

function tone(i) { return palette[i % palette.length]; }
function idFor(name) { return GENRE_IDS[name] !== undefined ? GENRE_IDS[name] : 0; }
function descFor(name) { return GENRE_DESC[name] || ""; }
function siblings() { return SIBLINGS.slice(); }

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

function labelOf(node) {
    return (node && node.label) ? node.label : "";
}

function entriesOf(json) {
    if (!json || !json.feed || !json.feed.entry) return [];
    var e = json.feed.entry;
    return (e instanceof Array) ? e : [e];
}

function clean(s) {
    return BiblioApi.stripHtml(String(s || ""))
        .replace(/\s+/g, " ")
        .trim();
}

function clamp(s, max) {
    s = clean(s);
    if (s.length <= max) return s;
    var cut = s.substring(0, max);
    var end = Math.max(cut.lastIndexOf(". "), cut.lastIndexOf("! "), cut.lastIndexOf("? "));
    if (end > max * 0.55) return cut.substring(0, end + 1).trim();
    var sp = cut.lastIndexOf(" ");
    return cut.substring(0, sp > 0 ? sp : max).trim() + "...";
}

function yearOf(entry) {
    var label = entry["im:releaseDate"] && entry["im:releaseDate"].attributes
              ? entry["im:releaseDate"].attributes.label : "";
    var m = String(label).match(/\b(18|19|20)\d\d\b/);
    return m ? m[0] : "";
}

function unique(list) {
    var out = [];
    for (var i = 0; i < list.length; i++) {
        var v = list[i];
        if (v && out.indexOf(v) < 0) out.push(v);
    }
    return out;
}

function toCard(entry, i, genreName) {
    var t = tone(i);
    var title = labelOf(entry["im:name"]) || "Untitled";
    var author = labelOf(entry["im:artist"]);
    var sub = entry.category && entry.category.attributes ? (entry.category.attributes.label || "") : "";
    var year = yearOf(entry);
    var cover = BiblioApi.coverOf(entry);
    var synopsis = clamp(labelOf(entry.summary), 240);
    var genres = unique([sub, genreName]).slice(0, 5);
    return {
        id: entry.id && entry.id.attributes ? entry.id.attributes["im:id"] : "",
        title: title,
        author: author,
        authors: author,
        year: year,
        cover: cover,
        c1: t[0],
        c2: t[1],
        type: author,
        status: sub,
        metaCounts: "",
        score: null,
        members: "",
        genres: genres,
        genreLine: [sub, author, year].filter(function(s) { return s; }).join("  -  "),
        tagline: "",
        synopsis: synopsis,
        rating: 0,
        ratingCount: 0
    };
}

// Task 9: `showExplicit` carries the global Explicit Content preference. Apple Books
// RSS is a commercial storefront (store policy already excludes sexually-explicit
// material), so this is defense-in-depth — Policy.visible gates ONLY sexually-explicit
// classifications; mainstream adult fiction (horror, romance, "Adult" audiences) stays
// visible regardless of the setting.
function loadGenre(name, sort, push, showExplicit) {
    var id = idFor(name);
    if (!id) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
    requestJson(FEED + id + "/json", function(j) {
        var entries = entriesOf(j);
        if (entries.length === 0) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
        var visible = entries.filter(function(e) { return Policy.visible("biblio", e, showExplicit); });
        var cards = visible.map(function(e, i) { return toCard(e, i, name); });
        var montage = cards.slice(0, 7).map(function(c) { return c.cover; }).filter(function(u) { return u; });
        push({ count: cards.length, desc: descFor(name), cards: cards, montage: montage });
    });
}

function imageUrls(payload) {
    return (payload && payload.cards ? payload.cards : []).map(function(c) { return c.cover; })
           .filter(function(u) { return u; });
}
