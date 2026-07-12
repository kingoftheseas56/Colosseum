// SagaApi.js — live data for a SAGA universe page (book-first IPs: Harry Potter, Lord of the
// Rings, A Song of Ice and Fire, Dune). Born from the 2026-07-12 correction: a name-search is
// ASSEMBLY, not curation — the LOTR page put a yaoi anthology behind the Read button. Here the
// canon comes from Universes.js (novels/films/shows — exact, ordered, Wikipedia-checked) and the
// live sources only DRESS it:
//   BOOKS → Apple Books via BiblioApi.lookupBook per curated novel title, IN READING ORDER.
//           Each tile is a real Biblio book object — routes straight into win.openBook.
//   FILMS/SHOWS → Cinemeta search (keyless, the Theatre's own source), then filtered AND
//           ordered by the curated canon list — never more entries than the canon says.
// Progressive like UniverseApi: push fires per response; slots fill in canon order.
.pragma library
.import "Universes.js" as UDB
.import "BiblioApi.js" as Biblio

var CINEMETA = "https://v3-cinemeta.strem.io";

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

function normArt(url) {
    if (!url) return "";
    return String(url)
        .replace("https://images.metahub.space/", "https://live.metahub.space/")
        .replace("/poster/small/", "/poster/medium/");
}

// canon matching: normalized (lowercase, punctuation collapsed) so "Part 1"/"Part I" or
// ampersand variants still land on their canon slot. PURE — headless-tested.
function normTitle(s) {
    return String(s || "").toLowerCase()
        .replace(/&/g, " and ")
        .replace(/\bpart i\b/g, "part 1").replace(/\bpart ii\b/g, "part 2")
        .replace(/[^a-z0-9]+/g, " ").replace(/\s+/g, " ").trim();
}

// place search hits into canon-ordered slots: result[i] corresponds to canon[i], null when
// nothing matched that canon entry (slot stays empty — honest, never a fuzzy stand-in).
// PURE — headless-tested.
function slotByCanon(canon, metas) {
    var slots = new Array(canon.length);
    for (var i = 0; i < canon.length; i++) {
        var want = normTitle(canon[i]);
        for (var j = 0; j < metas.length; j++) {
            var m = metas[j];
            if (normTitle(m.name || m.title) === want) { slots[i] = m; break; }
        }
    }
    return slots;
}

function mapWatch(meta) {
    return {
        id: meta.id || "",
        type: meta.type || "movie",
        title: meta.name || meta.title || "Untitled",
        cover: normArt(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art:   normArt(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : ""))
    };
}

// loadSaga("Harry Potter", push) — push({ name, blurb, banner, metaline, books[], films[],
// shows[] }) once per response. books = full Biblio objects in reading order; films/shows =
// Cinemeta items in canon order.
function loadSaga(name, push) {
    var cfg = UDB.configFor(name);
    var novels = cfg.novels || [];
    var filmCanon = cfg.films || [];
    var showCanon = cfg.shows || [];

    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        books: new Array(novels.length),    // reading-order slots (null until its lookup lands)
        films: [], shows: []
    };
    function emit() {
        push({
            name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
            books: out.books.filter(function(b) { return !!b; }),
            films: out.films, shows: out.shows
        });
    }

    // --- BOOKS: one Apple lookup per curated novel, slotted in reading order ---
    novels.forEach(function(title, i) {
        Biblio.lookupBook(title, function(book) {
            if (book) { out.books[i] = book; emit(); }
        });
    });

    // --- FILMS: pooled Cinemeta searches → canon-ordered slots ---
    if (filmCanon.length) {
        var filmPool = [];
        var queries = (cfg.movieQueries && cfg.movieQueries.length) ? cfg.movieQueries : [name];
        queries.forEach(function(q) {
            requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(q) + ".json",
                function(json) {
                    filmPool = filmPool.concat((json && json.metas) ? json.metas : []);
                    out.films = slotByCanon(filmCanon, filmPool)
                        .filter(function(m) { return !!m; }).map(mapWatch);
                    emit();
                });
        });
    }

    // --- SHOWS: same treatment on the series catalog ---
    if (showCanon.length) {
        var showPool = [];
        var sQueries = (cfg.seriesQueries && cfg.seriesQueries.length) ? cfg.seriesQueries : [name];
        sQueries.forEach(function(q) {
            requestJson(CINEMETA + "/catalog/series/top/search=" + encodeURIComponent(q) + ".json",
                function(json) {
                    showPool = showPool.concat((json && json.metas) ? json.metas : []);
                    out.shows = slotByCanon(showCanon, showPool)
                        .filter(function(m) { return !!m; }).map(mapWatch);
                    emit();
                });
        });
    }

    emit();   // paint banner/blurb/metaline instantly from curation, before any network lands
}

// every remote cover, for prefetch / disk-cache warming
function imageUrls(u) {
    var urls = [];
    if (u.banner) urls.push(u.banner);
    var groups = [u.books, u.films, u.shows];
    for (var g = 0; g < groups.length; g++)
        for (var i = 0; i < (groups[g] || []).length; i++)
            if (groups[g][i].cover && urls.indexOf(groups[g][i].cover) === -1)
                urls.push(groups[g][i].cover);
    return urls;
}
