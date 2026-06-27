// GenreApi.js — live data for a Colosseum genre BROWSE page (Tankoban / manga lane).
//
// Source: Jikan (api.jikan.moe/v4) — MyAnimeList's own keyless API. The genre page recreates MAL's
// genre listing (the reference Hemanth handed us: myanimelist.net/manga/genre/2/Adventure), so Jikan
// IS the right source — same data, no login, no key (the standing sourcing law). genre name → MAL id
// is fixed below (authoritative, pulled from /genres/manga). Cards route into A1's MangaSeries.qml by
// title. Comics lane is a DIFFERENT source (MAL is manga/anime only) — not handled here yet.
//
// loadGenre(name, sort, push) fetches once and calls push(payload) with { count, desc, cards, montage }.
// One Jikan call per page (well under the 3/sec · 60/min limit).
.pragma library

var JIKAN = "https://api.jikan.moe/v4";

// genre/theme/demographic share the manga `genres=` filter id-space (verified against /genres/manga).
var GENRE_IDS = {
    "Action": 1, "Adventure": 2, "Comedy": 4, "Drama": 8, "Fantasy": 10,
    "Horror": 14, "Mystery": 7, "Romance": 22, "Sci-Fi": 24, "Slice of Life": 36,
    "Sports": 30, "Supernatural": 37,
    // catalog demographics / themes (reachable from the GenreMosaic tiles)
    "Shounen": 27, "Seinen": 41, "Shoujo": 25, "Josei": 42,
    "Isekai": 62, "School": 23, "Magic": 10   // MAL has no "Magic" genre → nearest is Fantasy
};

// editorial standfirst per genre — the hero's "what this genre IS" line (MAL's voice). Missing → no line.
var GENRE_DESC = {
    "Action": "Fists, blades, and the will to use them. Action lives in the clash itself — the choreography of conflict, where every fight comes down to who wants it more.",
    "Adventure": "Whether aiming for a specific goal or just struggling to survive, the hero is thrust into unfamiliar lands and continuously faces unexpected dangers. The story is always how they react to the journey's trials — growth or setback by the choices they make. Simply seeing foreign worlds is not adventure; the change is.",
    "Comedy": "Built to make you laugh — through timing, absurdity, or the slow burn of a running gag. The plot is a stage; the joke is the point.",
    "Drama": "The weight of being human. Drama leans into conflict that's emotional rather than physical — relationships, loss, ambition, and the long cost of a single choice.",
    "Fantasy": "Worlds that run on rules that aren't ours. Magic, myth, and invented orders — fantasy asks what changes when the impossible becomes ordinary.",
    "Horror": "Made to unsettle. Horror works the dread before the reveal — the wrongness at the edge of the page, and the things that don't stay hidden.",
    "Mystery": "A question the reader is invited to solve. Mystery withholds, plants, and pays off — the pleasure is piecing it together a step behind the detective.",
    "Romance": "The pull between two people, and everything in the way. Romance follows the distance closing — or not — and the feeling that carries the whole story.",
    "Sci-Fi": "What-if, made rigorous. Science fiction extrapolates from technology and its consequences — the future as a lens on the present.",
    "Slice of Life": "The ordinary, paid attention to. Slice of life finds its drama in small days — routines, friendships, and the quiet texture of being somewhere real.",
    "Sports": "The discipline of getting better. Sports manga is the grind toward a goal — training, rivalry, and the team that forms around the chase.",
    "Supernatural": "The everyday world with something extra in it. Spirits, powers, and the unexplained — the uncanny walking among the ordinary.",
    "Shounen": "Aimed at a young male readership — and grown far past it. Shounen prizes growth, friendship, and the next impossible goal, told at a propulsive pace.",
    "Seinen": "Written for adult readers. Seinen takes its time and its subjects seriously — darker, knottier, and more willing to sit with consequence.",
    "Shoujo": "Aimed at a young female readership. Shoujo centers emotion and relationship, with an eye for interior life and the look of feeling on the page.",
    "Josei": "Written for adult women. Josei trades fantasy for the textured real — work, love, and the complicated adult life shoujo grows into.",
    "Isekai": "Carried into another world. The hero arrives from our reality into a second one — and the story is what they make of a clean slate with old memories.",
    "School": "Set where so much of life is decided — the classroom, the club, the years between childhood and after. Ordinary stakes at human scale.",
    "Magic": "Power with rules and a cost. Magic turns on systems of the impossible — what it can do, what it demands, and who is willing to pay."
};

// the in-page hop row — canonical core manga genres (all real MAL genres, all with descriptions).
var SIBLINGS = ["Action", "Adventure", "Comedy", "Drama", "Fantasy", "Horror",
                "Mystery", "Romance", "Sci-Fi", "Slice of Life", "Sports", "Supernatural"];

// warm/cool tints shown behind a cover while it loads (mirrors UniverseApi.tone)
var palette = [ ["#5d4633","#18110c"], ["#33445d","#0c1118"], ["#5b3a64","#170d1b"],
                ["#3f5640","#111b12"], ["#5a3a3f","#160d0b"], ["#3c4a63","#0e121b"] ];
function tone(i) { return palette[i % palette.length]; }

function idFor(name)   { return GENRE_IDS[name] !== undefined ? GENRE_IDS[name] : 0; }
function descFor(name) { return GENRE_DESC[name] || ""; }
function siblings()    { return SIBLINGS.slice(); }

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

// "Miura, Kentarou" → "Kentarou Miura"
function flipName(n) {
    var p = String(n).split(",");
    return p.length === 2 ? (p[1].trim() + " " + p[0].trim()) : n;
}
function members(m) { return m >= 1000 ? Math.round(m / 1000) + "K" : String(m || 0); }
function counts(c) {                              // "37 vol · 327 ch" / "ongoing" / "—"
    var bits = [];
    if (c.volumes)  bits.push(c.volumes + " vol");
    if (c.chapters) bits.push(c.chapters + " ch");
    return bits.length ? bits.join(" · ") : (c.status === "Publishing" ? "ongoing" : "—");
}

// one Jikan manga entry → the card model the page renders (mirrors mocks/genre.html exactly)
function toCard(m, i) {
    var t = tone(i);
    var img = (m.images && m.images.jpg && (m.images.jpg.large_image_url || m.images.jpg.image_url)) || "";
    var year = (m.published && m.published.prop && m.published.prop.from) ? m.published.prop.from.year : null;
    var syn = (m.synopsis || "").replace(/\s*\[Written by MAL Rewrite\]\s*$/, "").replace(/\s+/g, " ").trim();
    if (syn.length > 240) syn = syn.slice(0, 240) + "…";
    return {
        malId: m.mal_id,
        title: m.title_english || m.title,
        cover: img, c1: t[0], c2: t[1],
        type: m.type || "", year: year, status: m.status || "",
        metaCounts: counts(m),
        score: (m.score !== null && m.score !== undefined) ? m.score : null,
        members: members(m.members),
        authors: (m.authors || []).map(function(a) { return flipName(a.name); }).slice(0, 2).join(", "),
        genres: (m.genres || []).map(function(g) { return g.name; }).slice(0, 5),
        synopsis: syn
    };
}

// load a genre page. sort: "readers" (default) | "score". push gets one payload object.
function loadGenre(name, sort, push) {
    var id = idFor(name);
    if (!id) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
    var order = (sort === "score") ? "score&sort=desc" : "popularity&sort=asc";   // popularity asc = MAL "by members"
    var url = JIKAN + "/manga?genres=" + id + "&order_by=" + order + "&limit=24&sfw=true";
    requestJson(url, function(j) {
        if (!j || !j.data) { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); return; }
        var cards = j.data.map(toCard);
        var total = (j.pagination && j.pagination.items && j.pagination.items.total) || cards.length;
        var montage = cards.slice(0, 7).map(function(c) { return c.cover; }).filter(function(u) { return u; });
        push({ count: total, desc: descFor(name), cards: cards, montage: montage });
    });
}

// covers to warm into the disk cache for a genre (boot/idle prefetch parity with UniverseApi.imageUrls)
function imageUrls(payload) {
    return (payload && payload.cards ? payload.cards : []).map(function(c) { return c.cover; })
           .filter(function(u) { return u; });
}
