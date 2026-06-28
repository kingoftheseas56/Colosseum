// BiblioGenreApi.js - live data for Biblio's genre BROWSE page.
//
// Mirrors GenreApi.js' public shape exactly: loadGenre(name, sort, push) calls
// push({ count, desc, cards, montage }); cards feed BiblioGenrePage's cloned manga layout.
// Source: Apple Books RSS top ebooks by genre, keyless/no-login, same mzstatic cover infra as BiblioApi.
.pragma library
.import "BiblioApi.js" as BiblioApi

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
    "Fiction & Literature": "The broad country of made lives. Fiction and literature follow people through desire, damage, wit, and consequence - the ordinary world made strange by attention.",
    "Mysteries & Thrillers": "A secret, a threat, and the pressure of time. Mysteries and thrillers turn pages by withholding the truth, then tightening every room around it.",
    "Sci-Fi & Fantasy": "The impossible with rules. Science fiction and fantasy build other orders - future, magical, alien, or mythic - then ask what humans become inside them.",
    "Romance": "Two people moving toward each other, and everything that says they cannot. Romance lives in longing, friction, trust, and the courage to choose feeling.",
    "Biographies & Memoirs": "A life looked at directly. Biography and memoir turn memory, evidence, and voice into a portrait - the person, the era, and the cost of becoming.",
    "History": "The past as a living argument. History follows power, accident, invention, and witness - not just what happened, but why it still presses on us.",
    "Young Adult": "Stories at the edge of becoming. Young adult fiction carries first stakes at full volume - identity, loyalty, danger, love, and the future arriving fast.",
    "Comics & Graphic Novels": "Sequential art in book form. Comics and graphic novels make image and text move together - panels, rhythm, silence, and impact on the turn.",
    "Humor": "Built around the release of a laugh. Humor sharpens absurdity, embarrassment, and social truth until the joke becomes the cleanest way to say it.",
    "Travel & Adventure": "Motion with consequence. Travel and adventure leave the familiar behind - across maps, cultures, dangers, and the self that changes on the road."
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

function loadGenre(name, sort, push) {
    var id = idFor(name);
    if (!id) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
    requestJson(FEED + id + "/json", function(j) {
        var entries = entriesOf(j);
        if (entries.length === 0) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
        var cards = entries.map(function(e, i) { return toCard(e, i, name); });
        var montage = cards.slice(0, 7).map(function(c) { return c.cover; }).filter(function(u) { return u; });
        push({ count: cards.length, desc: descFor(name), cards: cards, montage: montage });
    });
}

function imageUrls(payload) {
    return (payload && payload.cards ? payload.cards : []).map(function(c) { return c.cover; })
           .filter(function(u) { return u; });
}
