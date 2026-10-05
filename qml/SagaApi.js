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

// UPCOMING law (Hemanth 2026-07-13): the metadata id is the gate, never the release date —
// future work STAYS in its room wearing a small tag. Year > now ⇒ upcoming outright; a
// same-year entry is ambiguous from catalog releaseInfo alone, so loadEras fires ONE
// full-meta check per such id (cached below) and re-slots when a future date confirms.
var _upcomingById = {};   // id → true (confirmed future) | false (probed: out or unknown)

// probe same-year items' full meta ONCE each (cached above); onConfirm fires only when the
// meta confirms the future — a released-date ahead of today, OR Cinemeta's own verdict
// (status "Upcoming": unaired shows often carry NO released field at all — VisionQuest).
function probeUpcoming(items, onConfirm) {
    var nowYr = new Date().getFullYear();
    (items || []).forEach(function(m) {
        if (!m || !m.id || m.year !== nowYr || (m.id in _upcomingById)) return;
        _upcomingById[m.id] = false;
        requestJson(CINEMETA + "/meta/" + m.type + "/" + m.id + ".json", function(j) {
            var meta = j && j.meta;
            if (!meta) return;
            var rel = meta.released;
            if ((rel && new Date(rel).getTime() > Date.now()) || meta.status === "Upcoming") {
                _upcomingById[m.id] = true;
                onConfirm();
            }
        });
    });
}

function metaYear(meta) {
    var m = /(\d{4})/.exec(String((meta && meta.releaseInfo) || ""));
    return m ? parseInt(m[1], 10) : 0;
}

function mapWatch(meta) {
    var yr = metaYear(meta);
    return {
        id: meta.id || "",
        type: meta.type || "movie",
        title: meta.name || meta.title || "Untitled",
        cover: normArt(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art:   normArt(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : "")),
        year: yr,
        upcoming: yr > new Date().getFullYear() || _upcomingById[meta.id] === true
    };
}

// Preserve every curated slot even when its provider has no listing yet.
// Unresolved entries are informational and cannot open a media detail.
function sagaSlots(canon, resolved, kind) {
    return canon.map(function(entry, index) {
        var item = {};
        var hit = resolved[index];
        if (hit) Object.keys(hit).forEach(function(k) { item[k] = hit[k]; });
        item.title = item.title || (entry && entry.t !== undefined ? entry.t : entry);
        item.canonIndex = index;
        item.medium = kind;
        item.resolved = !!hit;
        item.upcoming = item.upcoming === true || !!(entry && entry.upcoming)
            || !!(entry && entry.releaseDate && Date.parse(entry.releaseDate) > Date.now());
        return item;
    });
}

// THE ONE CANON (2026-10-04): a book-first saga and an anime universe resolve to the same
// three shelves. A saga reads its curated novels/films/shows as today. A manga universe
// (cfg.manga) is canonized the same way: books are the manga ({ t, q?, cover }), films
// are every filmEras group flattened in order, shows are the seven-series saga (Dragon
// Ball) or the anime followed by its adaptations (One Piece). Screen entries stay pinned
// { t, id, year }; nothing here touches the network. PURE.
function sagaCanon(cfg) {
    cfg = cfg || {};
    if (!cfg.manga)
        return { books: cfg.novels || [], films: cfg.films || [], shows: cfg.shows || [] };
    var films = [];
    (cfg.filmEras || []).forEach(function(group) {
        (group.films || []).forEach(function(f) { films.push(f); });
    });
    var shows = cfg.saga ? cfg.saga.slice()
               : (cfg.anime ? [cfg.anime] : []).concat(cfg.adaptations || []);
    return { books: cfg.manga, films: films, shows: shows };
}

// The branches: a saga curates its own (cfg.sagaBranches). A manga universe gets one per
// filmEras group, titled with its era, holding that group's films by flattened index.
// PURE.
function sagaBranchesFor(cfg) {
    cfg = cfg || {};
    if (cfg.sagaBranches) return cfg.sagaBranches;
    if (!cfg.manga) return [];
    var branches = [], flat = 0;
    (cfg.filmEras || []).forEach(function(group) {
        var films = [];
        for (var i = 0; i < (group.films || []).length; i++) films.push(flat + i);
        flat += (group.films || []).length;
        if (films.length) branches.push({ title: group.era, films: films });
    });
    return branches;
}

function sagaCollections(uni, cfg) {
    var source = { books: uni.books || [], films: uni.films || [], shows: uni.shows || [] };
    var used = { books: {}, films: {}, shows: {} };
    var branches = (sagaBranchesFor(cfg) || []).map(function(branch) {
        var result = { title: branch.title, books: [], adaptations: [] };
        ["books", "films", "shows"].forEach(function(kind) {
            source[kind].forEach(function(item, index) {
                var position = item.canonIndex === undefined ? index : item.canonIndex;
                if ((branch[kind] || []).indexOf(position) < 0) return;
                used[kind][position] = true;
                (kind === "books" ? result.books : result.adaptations).push(item);
            });
        });
        return result;
    }).filter(function(branch) { return branch.books.length || branch.adaptations.length; });
    function core(kind) {
        return source[kind].filter(function(item, index) {
            return !used[kind][item.canonIndex === undefined ? index : item.canonIndex];
        });
    }
    return { books: core("books"), adaptations: core("films").concat(core("shows")),
        branches: branches,
        upcoming: source.books.concat(source.films, source.shows).filter(function(item) {
            return item.upcoming === true;
        }) };
}

// THE SAGA GRID (Arc 58) — a column per story, a row per form, so a book and its adaptation
// carry the same weight. The curated pairing (cfg.sagaGrid, by canon index) says which screen
// work adapts which story; this turns it into drawable rows. PURE — headless-tested.
//   stories  [{ n, short, col, span }]            one header per story
//   rows     [{ kind, label, cells: [{ item, col, span, form }] }]
//            form: "tile" | "center" (a book over a story told in two films) |
//                  "banner" (a screen work stretched across the stories it covers)
//   cols     grid width in tiles
//   blocks   [{ title, shelves: [{ label, items }] }]  branches, plus whatever the pairing
//            left out ("On screen": adaptations that go their own way)
// A cfg without sagaGrid still works: every core book becomes a story, every adaptation
// lands in "On screen". An unmatched slot stays EMPTY — never a stand-in.
function sagaGrid(uni, cfg) {
    var coll = sagaCollections(uni, cfg);
    function canonOf(kind, item) {
        return item.canonIndex === undefined ? (uni[kind] || []).indexOf(item) : item.canonIndex;
    }
    var pair = cfg.sagaGrid || { stories: coll.books.map(function(b) { return { book: canonOf("books", b) }; }) };
    // row labels come from the data: the read row is MANGA when its items are manga; a
    // curated sagaGrid.labels may override any row (Dragon Ball's shows row reads ANIME)
    var canonBooks = sagaCanon(cfg).books;
    var manga = (uni.books || []).length > 0 && uni.books[0].medium === "manga";
    var overrides = (cfg.sagaGrid && cfg.sagaGrid.labels) || {};
    function rowLabel(kind, fallback) {
        if (overrides[kind]) return overrides[kind];
        return kind === "books" && manga ? "MANGA" : fallback;
    }
    var kinds = [{ kind: "films", label: "FILMS" }, { kind: "shows", label: "SERIES" }];
    function at(kind, index) {
        var list = uni[kind] || [];
        for (var i = 0; i < list.length; i++)
            if (canonOf(kind, list[i]) === index) return list[i];
        return null;
    }
    var placed = { books: {}, films: {}, shows: {} };
    var stories = [], col = 0;
    (pair.stories || []).forEach(function(s, i) {
        var span = Math.max(1, (s.films || []).length, (s.shows || []).length);
        var book = s.book === undefined || s.book === null ? null : at("books", s.book);
        if (book) placed.books[s.book] = true;
        // the header wears the CURATED title — a store's listing name can run to a paragraph
        var canon = s.book === undefined || s.book === null ? null : canonBooks[s.book];
        var canonTitle = canon && canon.t !== undefined ? canon.t : canon;
        stories.push({ n: i + 1, col: col, span: span, book: book, pair: s,
                       short: s.short || canonTitle || (book ? book.title : "") });
        col += span;
    });
    var rows = [{ kind: "books", label: rowLabel("books", "BOOKS"), cells: stories.filter(function(s) { return !!s.book; })
        .map(function(s) { return { item: s.book, col: s.col, span: s.span, form: s.span > 1 ? "center" : "tile" }; }) }];
    // a book may span stories, exactly as a screen work already can — the original Dragon
    // Ball manga covers both Dragon Ball and Z. A story covered by a spanning book has no
    // book of its own (the pairing says so; nothing is invented here).
    (pair.spans || []).forEach(function(sp) {
        if (sp.books === undefined) return;
        var item = at("books", sp.books), a = stories[sp.from - 1], b = stories[sp.to - 1];
        if (!item || !a || !b) return;
        placed.books[sp.books] = true;
        var span = b.col + b.span - a.col;
        rows[0].cells.push({ item: item, col: a.col, span: span, form: span > 1 ? "center" : "tile" });
    });
    rows[0].cells.sort(function(x, y) { return x.col - y.col; });
    kinds.forEach(function(k) {
        var cells = [];
        stories.forEach(function(s) {
            (s.pair[k.kind] || []).forEach(function(index, j) {
                var item = at(k.kind, index);
                if (!item) return;
                placed[k.kind][index] = true;
                cells.push({ item: item, col: s.col + j, span: 1, form: "tile" });
            });
        });
        (pair.spans || []).forEach(function(sp) {
            if (sp[k.kind] === undefined) return;
            var item = at(k.kind, sp[k.kind]), a = stories[sp.from - 1], b = stories[sp.to - 1];
            if (!item || !a || !b) return;
            placed[k.kind][sp[k.kind]] = true;
            var span = b.col + b.span - a.col;
            cells.push({ item: item, col: a.col, span: span, form: span > 1 ? "banner" : "tile" });
        });
        cells.sort(function(x, y) { return x.col - y.col; });
        if (cells.length) rows.push({ kind: k.kind, label: rowLabel(k.kind, k.label), cells: cells });
    });
    if (!rows[0].cells.length) rows.shift();

    function loose(kind, list) {
        return list.filter(function(item) { return !placed[kind][canonOf(kind, item)]; });
    }
    // what is still to come is listed once, under Upcoming — a block never repeats it
    function out_(list) { return list.filter(function(x) { return x.upcoming !== true; }); }
    function shelves(books, films, shows) {
        var out = [];
        books = out_(books); films = out_(films); shows = out_(shows);
        if (books.length) out.push({ label: rowLabel("books", "BOOKS"), items: books });
        if (films.length) out.push({ label: "FILMS", items: films });
        if (shows.length) out.push({ label: rowLabel("shows", "SERIES"), items: shows });
        return out;
    }
    function isSeries(x) { return x.medium ? x.medium === "series" : x.type === "series"; }
    function ofType(list, type) { return list.filter(function(x) { return isSeries(x) === (type === "series"); }); }
    var blocks = [];
    var looseScreen = loose("films", ofType(coll.adaptations, "movie"))
        .concat(loose("shows", ofType(coll.adaptations, "series")));
    if (looseScreen.length)
        blocks.push({ title: "On screen", shelves: shelves([], ofType(looseScreen, "movie"), ofType(looseScreen, "series")) });
    var looseBooks = loose("books", coll.books);
    if (looseBooks.length) blocks.push({ title: manga ? "Spin-offs" : "More books", shelves: shelves(looseBooks, [], []) });
    coll.branches.forEach(function(b) {
        blocks.push({ title: b.title, shelves: shelves(b.books, ofType(b.adaptations, "movie"), ofType(b.adaptations, "series")) });
    });
    blocks = blocks.filter(function(blk) { return blk.shelves.length > 0; });
    return { stories: stories, rows: rows, cols: col, blocks: blocks, upcoming: coll.upcoming };
}

// Where the keyboard lands from one grid cell: left/right walk the story within a row,
// up/down move between the forms of the same story, skipping a form that has nothing
// there. Returns { row, cell } or null at a boundary (the arrow then leaves the grid).
// PURE — headless-tested.
function gridMove(grid, row, cell, dx, dy) {
    var cur = grid.rows[row] && grid.rows[row].cells[cell];
    if (!cur) return null;
    if (dx) {
        var next = cell + dx;
        return grid.rows[row].cells[next] ? { row: row, cell: next } : null;
    }
    var mid = cur.col + (cur.span - 1) / 2;
    for (var r = row + dy; r >= 0 && r < grid.rows.length; r += dy) {
        var best = -1, bestGap = 1e9;
        grid.rows[r].cells.forEach(function(c, i) {
            if (c.col + c.span <= cur.col || c.col >= cur.col + cur.span) return;   // not under this story
            var gap = Math.abs(c.col + (c.span - 1) / 2 - mid);
            if (gap < bestGap) { bestGap = gap; best = i; }
        });
        if (best >= 0) return { row: r, cell: best };
    }
    return null;
}

// The facts a tile cannot carry: synopsis, runtime, rating, makers. One full-meta fetch per
// screen work, cached for the process; onUpdate fires as each lands.
var _detailById = {};
var _detailRevision = 0;
function detailFor(id) { return _detailById[id] || null; }
function enrichWatch(items, onUpdate) {
    (items || []).forEach(function(m) {
        if (!m || !m.id || m.resolved === false || (m.id in _detailById)) return;
        _detailById[m.id] = null;
        requestJson(CINEMETA + "/meta/" + m.type + "/" + m.id + ".json", function(j) {
            var meta = j && j.meta;
            if (!meta) { delete _detailById[m.id]; return; }      // failures are not cached
            var now = new Date().toISOString(), seasons = {};
            (meta.videos || []).forEach(function(v) {
                if (v.season && (!v.released || String(v.released) <= now)) seasons[v.season] = true;
            });
            var people = [];
            (m.type === "series" ? [] : (meta.director || [])).forEach(function(n) { people.push({ name: n, role: "Director" }); });
            (m.type === "series" ? (meta.writer || []).slice(0, 2) : []).forEach(function(n) { people.push({ name: n, role: "Writer" }); });
            _detailById[m.id] = {
                summary: meta.description || "", runtime: meta.runtime || "",
                rating: meta.imdbRating || "", span: String(meta.releaseInfo || ""),
                seasons: Object.keys(seasons).length, people: people
            };
            onUpdate();
        });
    });
}

// loadSaga("Harry Potter", push) — push({ name, blurb, banner, metaline, books[], films[],
// shows[] }) once per response. books = full Biblio objects in reading order; films/shows =
// Cinemeta items in canon order. An anime universe (cfg.manga) takes its own path below —
// the loader picks it; the page and the push shape stay the same.
function loadSaga(name, push) {
    var cfg = UDB.configFor(name);
    if (cfg.manga) return loadMangaUniverse(name, cfg, push);
    var novels = cfg.novels || [];
    var filmCanon = cfg.films || [];
    var showCanon = cfg.shows || [];

    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        books: new Array(novels.length),    // reading-order slots (null until its lookup lands)
        films: sagaSlots(filmCanon, [], "film"), shows: sagaSlots(showCanon, [], "series"),
        comics: cfg.comics || null          // curated GC archive pin → the comics door
    };
    function emit() {
        push({
            name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
            books: sagaSlots(novels, out.books, "book"),
            films: out.films, shows: out.shows,
            comics: out.comics,
            detailRevision: ++_detailRevision      // a detail landed → the page re-reads detailFor()
        });
    }

    // --- BOOKS: one Apple lookup per curated novel, slotted in reading order ---
    novels.forEach(function(title, i) {
        Biblio.lookupBook(title && title.t !== undefined ? title.t : title, function(book) {
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
                    function refilm() {
                        out.films = sagaSlots(filmCanon, slotByCanon(filmCanon, filmPool)
                            .map(function(m) { return m ? mapWatch(m) : null; }), "film");
                        emit();
                    }
                    refilm();
                    probeUpcoming(out.films, refilm);   // same-year boundary (Part Three case)
                    enrichWatch(out.films, emit);
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
                    function reshow() {
                        out.shows = sagaSlots(showCanon, slotByCanon(showCanon, showPool)
                            .map(function(m) { return m ? mapWatch(m) : null; }), "series");
                        emit();
                    }
                    reshow();
                    probeUpcoming(out.shows, reshow);   // same-year boundary (the HBO HP show)
                    enrichWatch(out.shows, emit);
                });
        });
    }

    emit();   // paint banner/blurb/metaline instantly from curation, before any network lands
}

// loadMangaUniverse — the anime universes' loader (One Piece, Dragon Ball): every work is
// PINNED in curation, so nothing is ever searched — no Biblio lookup, no catalog search,
// no manga provider. Manga items come straight from the curated list (Tankoban opens them
// by their query); films and shows are built from their pinned ids, mapWatch dressing each
// with the metahub poster/backdrop, and enrichWatch fetching the one full record per work
// for the facts a tile cannot carry. Same push shape as loadSaga.
function loadMangaUniverse(name, cfg, push) {
    var canon = sagaCanon(cfg);
    function pinned(entry, type) {
        return entry && entry.id
            ? mapWatch({ id: entry.id, type: type, name: entry.t, releaseInfo: entry.year })
            : null;
    }
    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        books: sagaSlots(canon.books, canon.books.map(function(m) {
            return m ? { title: m.t, cover: m.cover || "", query: m.q || m.t } : null;
        }), "manga"),
        films: sagaSlots(canon.films, canon.films.map(function(e) { return pinned(e, "movie"); }), "film"),
        shows: sagaSlots(canon.shows, canon.shows.map(function(e) { return pinned(e, "series"); }), "series"),
        comics: cfg.comics || null
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
               books: out.books, films: out.films, shows: out.shows, comics: out.comics,
               detailRevision: ++_detailRevision });
    }
    enrichWatch(out.films, emit);
    enrichWatch(out.shows, emit);
    emit();   // curation paints instantly; the records dress in as they land
    return null;
}

// THE SHELVES (Arc 58 / 58-E). One universe page serves every universe that is not a book
// saga: the page divides by MEDIUM, one shelf per medium, the franchise's own grouping as
// labelled runs. A universe declares its shelves in curation (`shelves`, see the generated
// block at the end of Universes.js); an anime universe that predates that shape (curated
// `manga` + `saga`/`anime` + `filmEras`) has them DERIVED here. Every work is a pinned
// provider identity and nothing is ever searched.

// shelfSpecsFor(cfg) -> [{ title, kind, tab?, pill?, form?, order?, runs: [{ label, works }] }]. PURE.
function shelfSpecsFor(cfg) {
    cfg = cfg || {};
    if (cfg.shelves) return cfg.shelves;
    if (!cfg.manga) return [];
    var canon = sagaCanon(cfg);
    // the manga the story pairing names are THE SERIES; the rest are spin-offs
    var main = {}, pair = cfg.sagaGrid || {};
    (pair.stories || []).forEach(function(st) {
        if (st.book !== undefined && st.book !== null) main[st.book] = true;
    });
    (pair.spans || []).forEach(function(sp) {
        if (sp.books !== undefined) main[sp.books] = true;
    });
    var series = [], spinoffs = [];
    canon.books.forEach(function(m, i) { (main[i] ? series : spinoffs).push(m); });
    return [
        { title: "Manga", kind: "manga",
          runs: [ { label: "The series", works: series }, { label: "Spin-offs", works: spinoffs } ] },
        { title: "Anime", kind: "series", runs: [ { label: "", works: canon.shows } ] },
        { title: "Films", kind: "movie",
          runs: (cfg.filmEras || []).map(function(g) { return { label: g.era, works: g.films || [] }; }) }
    ];
}

function shelfKeyOf(title, index) {
    var k = String(title || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
    return k || ("shelf-" + index);
}

// one curated work -> the tile the page draws. PURE.
function shelfItem(work, kind, key, index) {
    var item;
    work = work || {};
    if (kind === "movie" || kind === "series") {
        item = work.id ? mapWatch({ id: work.id, type: kind, name: work.t, releaseInfo: work.year })
                       : { title: work.t || "", cover: "", art: "", year: 0 };
        if (!work.id) item.resolved = false;
        if (work.upcoming === true) item.upcoming = true;
        if (work.live === true) item.live = true;
    } else if (kind === "manga") {
        item = { title: work.t || "", cover: work.cover || "", query: work.q || work.t || "",
                 al: work.al || 0, mal: work.mal || 0, by: work.by || "",
                 from: work.from || 0, to: work.to || 0, volumes: work.volumes || 0 };
    } else {
        // a book: the object BiblioBook renders, keyed by its Apple Books id; resolveBook
        // fills the synopsis and facts by ID when the tile is opened
        item = { id: work.apple || 0, apple: work.apple || 0, title: work.t || "",
                 cover: work.cover || "", author: work.author || "", year: String(work.year || ""),
                 genres: [], genreLine: work.author || "", tagline: "", synopsis: "",
                 rating: 0, ratingCount: 0 };
        if (!work.apple) item.resolved = false;
    }
    item.medium = kind;
    item.canonIndex = index;
    item.key = key + ":" + index;
    if (item.resolved !== false) item.resolved = true;
    return item;
}

// loadShelves(name, push) -> push({ name, category, blurb, blurbSource, banner, wallpaper,
// color, shelves: [{ title, tab, pill, key, kind, form, order, asOf, runs: [{ label, items }] }] })
// exactly ONCE: curation is complete, so the page never rebuilds under the reader.
function loadShelves(name, push) {
    var cfg = UDB.configFor(name);
    var shelves = shelfSpecsFor(cfg).map(function(spec, si) {
        var key = shelfKeyOf(spec.title, si), index = 0;
        return { title: spec.title || "", tab: spec.tab || "", pill: spec.pill || "", key: key,
                 kind: spec.kind || "movie", form: spec.form || "", order: spec.order || "",
                 asOf: spec.asOf || "",
                 runs: (spec.runs || []).map(function(run) {
                     return { label: run.label || "",
                              items: (run.works || []).map(function(w) {
                                  var item = shelfItem(w, spec.kind || "movie", key, index++);
                                  item.shelfTitle = spec.title || "";
                                  return item;
                              }) };
                 }) };
    });
    push({ name: name, category: cfg.shelves ? (cfg.category || "") : "anime",
           blurb: cfg.blurb || "", blurbSource: cfg.blurbSource || "",
           banner: cfg.banner || "", wallpaper: cfg.wallpaper || "", color: cfg.c1 || "#221c30",
           shelves: shelves, detailRevision: ++_detailRevision });
    return null;
}

// resolveBook(item, done) -> the full Biblio book for a pinned Apple Books id. A lookup BY ID,
// never a title search; on any failure the pinned tile itself opens (it is already a book).
function resolveBook(item, done) {
    if (!item || !item.apple) { done(item); return; }
    requestJson("https://itunes.apple.com/lookup?id=" + encodeURIComponent(item.apple), function(json) {
        var r = json && json.results && json.results[0];
        if (!r || String(r.trackId) !== String(item.apple)) { done(item); return; }
        var full = Biblio.fullBook(r);
        full.title = item.title || full.title;     // the curated title: store names run long
        full.cover = full.cover || item.cover;
        done(full);
    });
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
        comics: cfg.comics || null,          // curated GC archive pin → the comics door
        firstWatch: null
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
               trilogies: out.trilogies.map(function(t) { return { era: t.era, films: t.films }; }),
               standalones: out.standalones, liveShows: out.liveShows,
               animatedShows: out.animatedShows, comics: out.comics, firstWatch: out.firstWatch });
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
    var novels = cfg.novels || [];       // optional BOOKS shelf (Bond's Fleming shelf,
                                         // Avatar's Chronicles) — same lane as loadSaga

    var out = {
        name: name,
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        kicker: cfg.eraKicker || "THE ERAS",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        eras: eras.map(function(e) { return { era: e.era, items: [] }; }),
        rails: rails.map(function(r) { return { title: r.title, items: [] }; }),
        books: new Array(novels.length),   // reading-order slots (null until lookup lands)
        novelsTitle: cfg.novelsTitle || "The Novels",
        comics: cfg.comics || null,      // curated GC archive pin → the COMICS column
        firstWatch: null,
        firstWatchLabel: cfg.firstWatchLabel || ""
    };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, kicker: out.kicker,
               metaline: out.metaline,
               eras: out.eras.map(function(e) { return { era: e.era, items: e.items }; }),
               rails: out.rails.map(function(r) { return { title: r.title, items: r.items }; }),
               books: out.books.filter(function(b) { return !!b; }),
               novelsTitle: out.novelsTitle,
               comics: out.comics,
               firstWatch: out.firstWatch, firstWatchLabel: out.firstWatchLabel });
    }

    // --- BOOKS: one Apple lookup per curated novel, slotted in reading order ---
    novels.forEach(function(title, i) {
        Biblio.lookupBook(title, function(book) {
            if (book) { out.books[i] = book; emit(); }
        });
    });

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
        // UPCOMING boundary: a same-year entry can't be judged from releaseInfo alone
        out.eras.concat(out.rails).forEach(function(group) {
            probeUpcoming(group.items, reslot);
        });
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

// The covers a shelves page shows first: the opening `perShelf` works of every shelf. The
// Universes world warms these when a banner is pointed at, so the page opens with its posters
// already in the cache. PURE: curation only, no request.
function shelfImageUrls(name, perShelf) {
    var urls = [], seen = {};
    shelfSpecsFor(UDB.configFor(name)).forEach(function(spec, si) {
        var taken = 0, key = shelfKeyOf(spec.title, si);
        (spec.runs || []).forEach(function(run) {
            (run.works || []).forEach(function(w) {
                if (taken >= perShelf) return;
                ++taken;
                var cover = shelfItem(w, spec.kind || "movie", key, taken).cover;
                if (cover && !seen[cover]) { seen[cover] = true; urls.push(cover); }
            });
        });
    });
    return urls;
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
