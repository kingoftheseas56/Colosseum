// CosmereApi — the Cognitive Atlas resolver. Universes.js owns the map and exact Apple
// lookup queries; Biblio only dresses those declared slots with the full objects accepted
// by Main.openBook. Missing provider results remain missing. No name search can add a world.
.pragma library
.import "Universes.js" as UDB
.import "BiblioApi.js" as Biblio

function isBook(book) {
    return !!book && typeof book === "object" && !!book.id && !!book.title
        && String(book.author || "").toLowerCase().indexOf("brandon sanderson") >= 0;
}

function portal(entry, resolved) {
    var book = resolved[entry.query];
    if (!isBook(book)) return null;
    return {
        label: entry.label || book.title,
        short: entry.short || "",
        note: entry.note || "",
        query: entry.query,
        book: book
    };
}

// Pure, behavior-harnessed view builder. Declared array order is the only order; a missing
// lookup is filtered out instead of replaced by a fuzzy hit or a bare title.
function snapshot(cfg, resolved) {
    cfg = cfg || ({});
    resolved = resolved || ({});
    var starters = [];
    var starterCfg = cfg.cosmereStarters || [];
    for (var i = 0; i < starterCfg.length; i++) {
        var s = portal(starterCfg[i], resolved);
        if (s) starters.push(s);
    }

    function resolveSections(configs, fallback) {
        var sections = [];
        for (var w = 0; w < configs.length; w++) {
            var wc = configs[w];
            var books = [];
            var declared = wc.books || [];
            for (var b = 0; b < declared.length; b++) {
                var p = portal(declared[b], resolved);
                if (p) books.push(p);
            }
            sections.push({
                name: wc.name || fallback,
                epithet: wc.epithet || "",
                accent: wc.accent || "#78cfe3",
                books: books
            });
        }
        return sections;
    }

    var worlds = resolveSections(cfg.cosmereWorlds || [], "Unknown system");
    var series = resolveSections(cfg.cosmereSeries || [], "Untitled series");

    return {
        name: cfg.name || "Cosmere",
        blurb: cfg.blurb || "",
        banner: cfg.banner || "",
        metaline: (cfg.chips || []).map(function(c) { return c.t; }).join("   ·   "),
        starters: starters,
        worlds: worlds,
        series: series
    };
}

function queriesFor(cfg) {
    var seen = {};
    var out = [];
    function add(entry) {
        var q = String((entry && entry.query) || "");
        if (!q || seen[q]) return;
        seen[q] = true;
        out.push(q);
    }
    (cfg.cosmereStarters || []).forEach(add);
    (cfg.cosmereWorlds || []).forEach(function(world) { (world.books || []).forEach(add); });
    (cfg.cosmereSeries || []).forEach(function(series) { (series.books || []).forEach(add); });
    return out;
}

// Progressive live loader: paint the authored atlas immediately, then re-emit as exact
// Apple Books objects land. Duplicate portals share one network lookup by query.
function loadAtlas(name, push) {
    var cfg = UDB.configFor(name);
    var resolved = {};
    function emit() { push(snapshot(cfg, resolved)); }
    emit();
    queriesFor(cfg).forEach(function(query) {
        Biblio.lookupBook(query, function(book) {
            if (isBook(book)) resolved[query] = book;
            emit();
        });
    });
}

