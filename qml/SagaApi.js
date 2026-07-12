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
// A canon entry is a string (matched by normalized name) OR { t, id } — the id form pins
// an exact imdb id for titles whose name-search ranks a remake first (Ghibli's Grave of
// the Fireflies sits BEHIND its 2024 remake; the canon must not grab the remake).
// PURE — headless-tested.
function slotByCanon(canon, metas) {
    var slots = new Array(canon.length);
    for (var i = 0; i < canon.length; i++) {
        var entry = canon[i];
        var wantId = (entry && entry.id) ? entry.id : "";
        var want = normTitle(entry && entry.t !== undefined ? entry.t : entry);
        for (var j = 0; j < metas.length; j++) {
            var m = metas[j];
            if (wantId ? (m.id === wantId) : (normTitle(m.name || m.title) === want)) {
                slots[i] = m; break;
            }
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

// loadGalaxy(name, push) — the GALAXY template's loader (Star Wars): the Skywalker Saga as
// three curated trilogies, the standalone stories, and the series in live-action + animated
// rails. Same law as the saga: canon lists from Universes.js, Cinemeta only DRESSES the
// slots — an era panel can never grow a film the canon doesn't name.
function loadGalaxy(name, push) {
    var cfg = UDB.configFor(name);
    var trilogies   = cfg.trilogies || [];
    var standalones = cfg.standalones || [];
    var liveShows   = cfg.liveShows || [];
    var animShows   = cfg.animatedShows || [];

    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        trilogies: trilogies.map(function(t) { return { era: t.era, films: [] }; }),
        standalones: [], liveShows: [], animatedShows: [],
        firstWatch: null
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
               trilogies: out.trilogies.map(function(t) { return { era: t.era, films: t.films }; }),
               standalones: out.standalones, liveShows: out.liveShows,
               animatedShows: out.animatedShows, firstWatch: out.firstWatch });
    }

    // --- FILMS: one pooled movie search set → every trilogy + the standalones slot from it ---
    var moviePool = [];
    var mQueries = (cfg.movieQueries && cfg.movieQueries.length) ? cfg.movieQueries : [name];
    mQueries.forEach(function(q) {
        requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(q) + ".json",
            function(json) {
                moviePool = moviePool.concat((json && json.metas) ? json.metas : []);
                out.trilogies = trilogies.map(function(t) {
                    return { era: t.era,
                             films: slotByCanon(t.films, moviePool)
                                    .filter(function(m) { return !!m; }).map(mapWatch) };
                });
                out.standalones = slotByCanon(standalones, moviePool)
                                  .filter(function(m) { return !!m; }).map(mapWatch);
                // the golden path: the saga's declared beginning (cfg.firstWatch, a canon title)
                if (cfg.firstWatch) {
                    var fw = slotByCanon([cfg.firstWatch], moviePool);
                    if (fw[0]) out.firstWatch = mapWatch(fw[0]);
                }
                emit();
            });
    });

    // --- SERIES: one pooled series search set → live + animated rails slot from it ---
    var seriesPool = [];
    var sQueries = (cfg.seriesQueries && cfg.seriesQueries.length) ? cfg.seriesQueries : [name];
    sQueries.forEach(function(q) {
        requestJson(CINEMETA + "/catalog/series/top/search=" + encodeURIComponent(q) + ".json",
            function(json) {
                seriesPool = seriesPool.concat((json && json.metas) ? json.metas : []);
                out.liveShows = slotByCanon(liveShows, seriesPool)
                                .filter(function(m) { return !!m; }).map(mapWatch);
                out.animatedShows = slotByCanon(animShows, seriesPool)
                                    .filter(function(m) { return !!m; }).map(mapWatch);
                emit();
            });
    });

    emit();   // banner/blurb/metaline paint instantly from curation
}

// loadEras(name, push) — the ERAS template's loader (James Bond, Star Trek, DCAU, Avatar):
// the universe as ordered ERA GROUPS, each group a curated list of films OR shows (its
// `kind` picks the Cinemeta catalog). Same law as saga/galaxy: canon slots, search dresses.
//   cfg.eras  = [ { era, kind: "movie"|"series", titles: [canon names] }, ... ]
//   cfg.rails = [ { title, kind, titles } ]  (optional flat rails below the era columns)
function loadEras(name, push) {
    var cfg = UDB.configFor(name);
    var eras = cfg.eras || [];
    var rails = cfg.rails || [];

    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        kicker: cfg.eraKicker || "THE ERAS",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        eras: eras.map(function(e) { return { era: e.era, items: [] }; }),
        rails: rails.map(function(r) { return { title: r.title, items: [] }; }),
        firstWatch: null,
        firstWatchLabel: cfg.firstWatchLabel || ""
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, kicker: out.kicker,
               metaline: out.metaline,
               eras: out.eras.map(function(e) { return { era: e.era, items: e.items }; }),
               rails: out.rails.map(function(r) { return { title: r.title, items: r.items }; }),
               firstWatch: out.firstWatch, firstWatchLabel: out.firstWatchLabel });
    }

    var moviePool = [], seriesPool = [];
    function reslot() {
        out.eras = eras.map(function(e) {
            return { era: e.era,
                     items: slotByCanon(e.titles, e.kind === "series" ? seriesPool : moviePool)
                            .filter(function(m) { return !!m; }).map(mapWatch) };
        });
        out.rails = rails.map(function(r) {
            return { title: r.title,
                     items: slotByCanon(r.titles, r.kind === "series" ? seriesPool : moviePool)
                            .filter(function(m) { return !!m; }).map(mapWatch) };
        });
        if (cfg.firstWatch) {
            var kind = cfg.firstWatchKind || "movie";
            var fw = slotByCanon([cfg.firstWatch], kind === "series" ? seriesPool : moviePool);
            if (fw[0]) out.firstWatch = mapWatch(fw[0]);
        }
        emit();
    }

    (cfg.movieQueries || []).forEach(function(q) {
        requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(q) + ".json",
            function(json) { moviePool = moviePool.concat((json && json.metas) ? json.metas : []); reslot(); });
    });
    (cfg.seriesQueries || []).forEach(function(q) {
        requestJson(CINEMETA + "/catalog/series/top/search=" + encodeURIComponent(q) + ".json",
            function(json) { seriesPool = seriesPool.concat((json && json.metas) ? json.metas : []); reslot(); });
    });

    emit();   // curation paints instantly
}

// loadStudio(name, push) — the STUDIO template's loader (Studio Ghibli): one chronological
// FILMOGRAPHY (the studio's whole body of work as a wall), canon-slotted like everything else.
//   cfg.filmography = [ canon film names, chronological ]
function loadStudio(name, push) {
    var cfg = UDB.configFor(name);
    var filmography = cfg.filmography || [];
    var out = {
        name: name, blurb: cfg.blurb || "", banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        films: [], firstWatch: null, firstWatchLabel: cfg.firstWatchLabel || ""
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
               films: out.films, firstWatch: out.firstWatch, firstWatchLabel: out.firstWatchLabel });
    }
    var pool = [];
    (cfg.movieQueries || []).forEach(function(q) {
        requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(q) + ".json",
            function(json) {
                pool = pool.concat((json && json.metas) ? json.metas : []);
                out.films = slotByCanon(filmography, pool)
                            .filter(function(m) { return !!m; }).map(mapWatch);
                if (cfg.firstWatch) {
                    var fw = slotByCanon([cfg.firstWatch], pool);
                    if (fw[0]) out.firstWatch = mapWatch(fw[0]);
                }
                emit();
            });
    });
    emit();
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
